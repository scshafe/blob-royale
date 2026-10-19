<!-- canonical: tailnet_operations -- deploying and operating the tailnet game node -->

# Tailnet operations runbook

The playable Blob Royale deployment is the `blob-royale` stack on the scshafe-dev **runner lane**
(`dev.toml [deploy]`, `deploy/stack/`). It has its own tailnet node and is reachable only inside the
tailnet at `https://blob-royale.colobus-stargazer.ts.net/`, with no port. [`linux.md`](linux.md)
remains the general Linux contract, and
[`../architecture/0001-linux-runtime-contract.md`](../architecture/0001-linux-runtime-contract.md)
records the accepted runtime facts and the 2026-10-18 amendment that moved the deployment here.

This replaces the hand-made container on `cole-ubuntu-pc` behind `tailscale serve --https=8444`,
which `scripts/deploy-tailnet` and `scripts/reconfigure-tailnet` operated. Both scripts are gone.

## What runs

| Concern | Value |
|---|---|
| Lane | `runner`, host `pc`, stack `blob-royale`, door `none`, verify on a GitHub-hosted runner |
| Containers | `ts-blob-royale` (Tailscale sidecar, node `blob-royale`), `blob-royale` (simulation server), `blob-royale-web` (static web server), all in the sidecar's network namespace |
| Server image | The root `Dockerfile`: the server built from source (GCC, Release) on the pinned Ubuntu 24.04 base, with `deploy/config/blob-royale.cfg` at `/etc/blob-royale/` and `maps/` at `/usr/share/blob-royale/maps` baked in. Runs as 65532 on a read-only root, all capabilities dropped, 1 GiB, 2 CPUs |
| Web image | `deploy/web/Dockerfile`: `npm ci && npm run build` of `frontend-react/`, served by `nginxinc/nginx-unprivileged` pinned by digest, on `127.0.0.1:8081` |
| Routing | `deploy/stack/serve.json`: `/api` to `http://127.0.0.1:8000/api` (serve strips the mount prefix, so the target carries `/api`), `/` to `http://127.0.0.1:8081`. HTTPS on 443 only; Funnel is never enabled |
| State | None. The only bind is `./state/tailscale` (the node identity) |
| Secret | `TS_AUTHKEY`, placed on the host by the owner's provisioning; never in the repository |

The host builds both images at the merged commit; nothing is mounted from `/srv/blob-royale`.

## Deploy

A merge to `main` deploys. `.github/workflows/deploy.yml` (rendered from `dev.toml`; change that, not
the workflow) runs `./scripts/verify-linux pr` on a GitHub-hosted runner, then the host's deploy
entrypoint on the `blob-royale-prod` self-hosted runner builds both images, validates
`deploy/stack/` against the platform allowlist (`dev check .` runs the same validator), starts a
candidate, probes `/api/v1/health/ready` through the tailnet name, and keeps or rolls back. Then a
health job checks the live commit and that the node is tailnet-only.

```sh
gh run list -w deploy
gh run watch <id>
```

Merge one change at a time and watch its run. A deploy takes as long as the hosted gate plus the two
image builds; there is no separate deployment-host verification.

## Change balance

Bot roster, lobby seat count, drag, zone shrink, and every other match number live in
`deploy/config/blob-royale.cfg`, not in code. There is no live reconfiguration any more: a balance
change is a pull request like any other, merged and deployed. Try the value first on a local server:

```sh
out/build/linux-gcc-debug/blob-royale --config deploy/config/blob-royale.cfg
```

The process validates every value at startup and fails closed, so a bad number fails the candidate
container's health probe and the deploy rolls back instead of serving a broken match. The hosted gate
parses this file through the production loader (`tests/fixtures/deployment_fixture_tests.cpp`), so
most mistakes fail before the merge.

The number that decides who a match waits for is `[match] lobby_seat_count`: it is how many seats
the pre-match lobby is created with, and a match starts only when every seat is filled and somebody
presses Start. The shipped value is four seats. `[match] bots` declares NPCs into the first seats of
that lobby and the server creates a bot for every declared seat, so the roster is part of the field
the lobby waits for and a person who joins a full lobby takes an NPC's seat. `[lobbies] count` is
how many rooms the process runs, each playing that match on its own thread with its own seed; the
shipped value is four, and the current directory route is `GET /api/v3/lobbies`.

## Verify

```sh
curl -fsS https://blob-royale.colobus-stargazer.ts.net/api/v1/health/ready
curl -fsS https://blob-royale.colobus-stargazer.ts.net/api/v3/lobbies
curl -fsSI https://blob-royale.colobus-stargazer.ts.net/
```

Check the directory twice and require all four rooms to report `healthy: true` with advancing
`tick_sequence` values; readiness alone checks only the first room. Open
`https://blob-royale.colobus-stargazer.ts.net/` from any tailnet device and join a room. The room
page must report "Connected to the match session." and advance its tick counter.

## Roll back

Merge a revert, or dispatch the deploy workflow at an older commit on `main`:

```sh
gh workflow run deploy.yml -f sha=<older commit on main> -f allow_rollback=true
```

There is no durable state to migrate; the configuration is in the image. A scenario seeds exactly
one world, so the deployment, which runs more than one room, passes none; the loader refuses the
pair with `APPLICATION.LOBBIES.SCENARIO_REQUIRES_ONE_LOBBY`.

## Logs and diagnostics

Application logs are JSON lines on the server container's standard error, collected by the host's
Docker. On the host (owner only; the agent account has no Docker route to it):

```sh
docker logs --since 10m blob-royale
docker logs --since 10m blob-royale-web
docker exec ts-blob-royale tailscale serve status
```

Index `severity`, `event`, `error_code`, lifecycle state, `lobby_id`, and request ID as described in
[`linux.md`](linux.md). A repeatable nonzero startup exit is a configuration error to fix, not a
crash loop for the restart policy to absorb.

## Proxy behavior

The sidecar preserves the browser `Host` header, so the deployment configuration allowlists
`blob-royale.colobus-stargazer.ts.net` (no port) and the matching `https://` origin. It adds
`X-Forwarded-For` and `Tailscale-User-*` headers. Trusted-proxy client-address validation and
accounting apply to v1 routes too, including health checks: exactly one canonical IP address must
be supplied in `X-Forwarded-For`. A missing value returns HTTP 400 with `PROTOCOL.INVALID_REQUEST`
and reason `forwarded_client_absent` on a v1 route, even when the server is ready. The server
container's healthcheck in `deploy/stack/compose.yaml` supplies the local probe's address
explicitly; ordinary HTTP readiness GETs need no `Origin` header. Protocol v1 does not use
`Tailscale-User-*` identity headers; the v3 session uses the validated `Tailscale-User-Name` for its
display name.

## Limits behind the proxy

Every tailnet client reaches the server from the sidecar's loopback address. The deployment names
that proxy in `[server] trusted_proxy_addresses=127.0.0.1`, so the server credits the
`X-Forwarded-For` that the sidecar adds and accounts each tailnet client as its own principal: at
most 8 concurrent WebSocket sessions, 4 upgrades per burst refilling one every 5 seconds, and 20
HTTP requests per burst refilling 2 per second, **per client** rather than shared by everyone.
Without that line every player would be one loopback principal and a lobby directory that four
people poll would rate-limit all of them at once.

The residual is the one protocol v2 § "Abuse cases" accepts for a single-operator host: the trusted
proxy *is* loopback, so any process in the same network namespace can set the same header, mint
principals, and appear under any name. Here that namespace holds only the sidecar, the server, and
the static web server, not every process on the host, so the exposure is smaller than on the old
host-network deployment. Do not widen the server bounds, do not add a container to the stack that
runs third-party code, and do not add a second address to the trusted list without reading that
section.

## Stop and retire

Stopping is the owner's host operation (`docker compose` on the stack, or removing the stack through
the platform's tools). `docker stop` delivers `SIGTERM`; the process exits zero within its
five-second shutdown deadline. To take the game off the tailnet, remove the node in the Tailscale
admin console as well.

## What the move gave up

- **Live reconfiguration.** `scripts/reconfigure-tailnet` restarted the server with a new config in
  seconds, without a merge. A balance change now costs a pull request, the hosted gate, and a deploy.
- **A release gate on the deployment host.** `scripts/deploy-tailnet` ran `verify-linux release`
  on the host that served the game and refused to run elsewhere. The deploy now trusts the hosted
  `verify-linux pr` gate plus the platform's candidate health probe and rollback.
- **A scripted pre-flight** of the snapshot mirror and the readiness loop. The platform's
  `ready_attempts` and rollback replace the readiness loop.
- **Editing a value in place on the host** to try it. Try it locally instead.
- **The port-less URL's old companion facts:** the `cole-ubuntu-pc` ssh alias, the
  `vm.mmap_rnd_bits` sysctl note, and the host-wide `tailscale serve` handlers no longer apply to the
  game. The sysctl still matters to anyone running the TSan lane natively on that host
  ([`../architecture/0001-linux-runtime-contract.md`](../architecture/0001-linux-runtime-contract.md)).
- **The old URL.** `https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444` stops working when the owner
  retires the old container; its Host and Origin are no longer in the server allowlists.
