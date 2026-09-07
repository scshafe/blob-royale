<!-- canonical: tailnet_operations -- deploying and operating the tailnet game host -->

# Tailnet operations runbook

The playable Blob Royale deployment runs on `cole-ubuntu-pc` and is reachable only inside the
tailnet at `https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444/`. This runbook is the operating
contract for that host. [`linux.md`](linux.md) remains the general Linux contract, and
[`../architecture/0001-linux-runtime-contract.md`](../architecture/0001-linux-runtime-contract.md)
records the accepted host facts.

## Host facts

| Concern | Value |
|---|---|
| Host | `cole-ubuntu-pc`, Ubuntu 24.04.4 LTS, x86_64, Docker Engine 29.6.1 (containerd image store), Tailscale 1.102.2 |
| Access | `ssh ubuntu-tailscale` from the maintainer's Mac (alias in `~/.ssh/config`); user `cole` is in the `docker` group and has non-interactive sudo |
| Repository | `~/Projects/blob-royale`, an HTTPS clone of `origin`; deployments run from a clean checkout of a pushed commit |
| Authority | Docker client and daemon are both native Linux/x86_64, so `./scripts/verify-linux release` on this host is authoritative and publishes `out/release/current` |
| Runtime | Versioned release image under Docker with host networking, read-only root, all capabilities dropped, `no-new-privileges`, 1 GiB memory, 2 CPUs, `--restart unless-stopped` |
| Listener | `127.0.0.1:8000` in the host network namespace; never published beyond loopback |
| Proxy | `tailscale serve` on HTTPS port 8444, tailnet only; Funnel is never enabled |
| Inputs | `/srv/blob-royale/config/{blob-royale.cfg,scenario.csv}` (mode 0444) from `deploy/ubuntu-pc/` plus `/srv/blob-royale/config/maps/` rsynced from the repository's `maps/`, all mounted read-only at `/run/blob-royale`; the deployed configuration names `[match] maps_directory=/run/blob-royale/maps` |
| Web | `/srv/blob-royale/web/`, the staged `web/` deployable, served by `tailscale serve` at `/` |
| Sanitizer prerequisite | `/etc/sysctl.d/60-blob-royale-sanitizers.conf` sets `vm.mmap_rnd_bits = 28` so the TSan lane runs inside the pinned container |

Ports 443 and 8443 on this host belong to unrelated `tailscale serve` handlers; Blob Royale must
stay on 8444. The tailnet also exposes an alternate MagicDNS name for this host; only the
`colobus-stargazer.ts.net` name is in the server allowlists, so always use the URL above.

## Deploy

```sh
ssh ubuntu-tailscale
cd ~/Projects/blob-royale
git pull --ff-only
./scripts/deploy-tailnet
```

`scripts/deploy-tailnet` is the only deployment path and is idempotent. In order it:

1. refuses any host other than `cole-ubuntu-pc`, a dirty checkout, or missing `sudo`/`docker`/
   `rsync`/`tailscale`;
2. runs `./scripts/verify-linux release`, which must publish an `authoritative` record at
   `out/release/current/publication.env`;
3. installs the configuration and scenario under `/srv/blob-royale/config`, rsyncs the repository's
   `maps/` to `/srv/blob-royale/config/maps` after checking that the directory named by
   `[match] map=` exists, and syncs the staged web deployable to `/srv/blob-royale/web`;
4. replaces the `blob-royale` container with the published `runtime_image_reference` and waits up
   to ten seconds for `GET http://127.0.0.1:8000/api/v1/health/ready`;
5. applies the two `tailscale serve` handlers (`/api` to `http://127.0.0.1:8000/api`, `/` to the
   web directory) and checks liveness and the site root through the tailnet URL.

It prints `deploy_tailnet.step=<name> status=<running|passed>` lines and ends with
`deploy_tailnet.release_id`, `deploy_tailnet.runtime_image`, `deploy_tailnet.deployed_commit`,
`deploy_tailnet.url`, and `deploy_tailnet.status=passed`. A failure prints
`deploy_tailnet.error_code` and leaves the previous container running only if the failure happened
before the container step; treat any failure as an incident and rerun at a known-good commit.

The release profile rebuilds and re-verifies everything, so a deployment takes tens of minutes on
this host. Fresh Grype databases and npm advisories can fail the dependency-evidence step for a
commit that passed yesterday; fix the pin in a separate commit and redeploy, never bypass the gate.

## Change balance without redeploying

Bot roster, lobby minimum, drag, zone shrink, and every other match number live in
`deploy/ubuntu-pc/blob-royale.cfg`, not in code. Editing one and running the full deploy would
re-verify the entire tree, which is right for a code change and far too slow for a number. Use:

```sh
ssh ubuntu-tailscale
cd ~/Projects/blob-royale
git pull --ff-only          # or edit deploy/ubuntu-pc/blob-royale.cfg in place to try a value
./scripts/reconfigure-tailnet
```

It reuses the image the current publication already certified, refuses to run if none is published,
and cannot change code. The process validates every value at startup and fails closed, so a bad
number stops the container with a diagnostic naming the key rather than serving a broken match.
Restart takes seconds. Commit whatever value you settle on, so the running arena and the repository
agree.

The two numbers that decide who a match waits for are `[match] bots` and
`[royale] lobby_minimum_players`. The minimum counts every seated blob, bots included, so a minimum
above the bot count makes the arena wait for people rather than playing against itself. The shipped
values are one bot and a minimum of three, so the lobby holds until two people have joined.

## Verify

```sh
curl -fsS https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444/api/v1/health/ready
docker ps --filter name=^blob-royale$
docker inspect blob-royale --format '{{index .Config.Labels "blob-royale.deployed-commit"}}'
```

Open `https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444/` from any tailnet device. The page must
report "Connected to the read-only snapshot stream." and advance its tick counter.

## Roll back

Check out the previously certified commit and redeploy; the script re-verifies that commit and
replaces the container and web tree:

```sh
git checkout <previous-certified-commit>
./scripts/deploy-tailnet
git checkout main
```

There is no durable state to migrate; configuration and scenario inputs are versioned in
`deploy/ubuntu-pc/`.

## Logs and diagnostics

Application logs are JSON lines on the container's standard error, retained by the json-file driver
as five 50 MiB files:

```sh
docker logs --since 10m blob-royale
sudo tailscale serve status
```

Index `severity`, `event`, `error_code`, lifecycle state, and request ID as described in
[`linux.md`](linux.md). A repeatable nonzero startup exit is a configuration error to fix, not a
crash loop for the restart policy to absorb.

## Serve configuration

The two handlers persist in the tailscaled preferences across reboots. To remove them, repeat each
enabling command with `off` appended:

```sh
sudo tailscale serve --https=8444 --set-path=/api http://127.0.0.1:8000/api off
sudo tailscale serve --https=8444 /srv/blob-royale/web off
```

`tailscale serve` strips the mount prefix before proxying, which is why the backend target carries
`/api`. It preserves the browser `Host` header, so the deployment configuration allowlists
`cole-ubuntu-pc.colobus-stargazer.ts.net:8444` and the matching `https://` origin. It also adds
`X-Forwarded-For` and `Tailscale-User-*` headers; protocol v1 ignores them.

## Limits behind the proxy

Every tailnet client reaches the server from the proxy's loopback address, so today the server
accounts all players as one loopback principal: at most 8 concurrent WebSocket sessions, 4 upgrades
per burst refilling one every 5 seconds, and 20 HTTP requests per burst refilling 2 per second. A
later protocol version will define proxy-supplied per-client accounting. Do not widen the server
bounds to hide the collapse.

## Stop

```sh
docker stop blob-royale
docker rm blob-royale
```

`docker stop` delivers `SIGTERM`; the process exits zero within its five-second shutdown deadline.
Removing the serve handlers is separate and optional.
