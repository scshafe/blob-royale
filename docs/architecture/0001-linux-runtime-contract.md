<!-- canonical: linux_runtime_contract -- release authority and deployment boundary -->

# 1. Adopt a Linux/amd64 OCI release authority

* **Status:** Accepted
* **Date:** 2026-08-04
* **Deciders:** Project owner

## Context and Problem Statement

Blob Royale has no recorded production host or release platform. At the reviewed prototype commit,
the bootstrap defaulted to a developer LAN address, used working-directory-relative inputs, and
embedded one developer's Linux home path. Those deleted sources remain identified in
`docs/PROJECT_DEEP_DIVE.md` and Git history. Native macOS results therefore cannot establish whether
the intended Linux service builds, shuts down, or behaves correctly. The project needs one explicit
release authority while deployment-host facts remain unknown.

## Considered Options

* **A. Digest-pinned Ubuntu 24.04 LTS OCI image on native Linux/amd64.** Use one pinned userspace and toolchain for local verification and CI, but accept release evidence only when that image runs on a real Linux host.
* **B. Direct execution on an Ubuntu host.** This removes the container boundary but makes the host package set and supervisor part of the build contract before either is known.
* **C. Equal Linux and macOS authority.** This broadens the support matrix and permits Apple-specific behavior to influence a Linux service without a demonstrated production need.

## Decision Outcome

**Chosen: Option A.**

The authoritative baseline is an Ubuntu 24.04 LTS OCI image for `linux/amd64`. The image uses the immutable manifest digest and exact tool versions recorded in `ci/linux/Dockerfile`; `scripts/report-toolchain` verifies and reports those pins for every release gate. The production facts observed on 2026-09-06 confirm that baseline rather than superseding it: the deployment host runs the same Ubuntu 24.04 LTS release on native `x86_64`, executes the release image under Docker, and terminates TLS outside the process. They are recorded in the contract below.

The runtime contract is:

| Concern | Accepted contract |
|---|---|
| Distribution | Ubuntu 24.04 LTS userspace from the digest-pinned OCI base image. The observed deployment host runs Ubuntu 24.04.4 LTS on kernel 6.17.0-40-generic, so host and image userspace are the same release. |
| Architecture | `linux/amd64` only. `arm64` and other architectures require a later amendment and their own native evidence. |
| Deployment host | `cole-ubuntu-pc`: Ubuntu 24.04.4 LTS, kernel 6.17.0-40-generic, `x86_64` (AMD Ryzen 5 5600G, 12 CPUs, 125 GiB RAM), Docker Engine 29.6.1 with the containerd image store and overlayfs snapshotter, Tailscale 1.102.2, MagicDNS name `cole-ubuntu-pc.colobus-stargazer.ts.net`. The same host is the authoritative release runner: its Docker client and daemon are both native Linux/`x86_64`, so `./scripts/verify-linux release` there reports `verification_authority=authoritative` under the release-evidence rule below. A second deployment host requires its own observed facts and an amendment. |
| Compiler and libc | GCC is the primary release compiler. Clang supplies an independent warnings-and-sanitizers check. The glibc shipped by the pinned Ubuntu image is authoritative; musl and host libc variants are unsupported. The verification report must emit exact compiler, linker, CMake, Boost, and libc versions. |
| Sanitizer host prerequisite | ThreadSanitizer in the pinned LLVM 18 re-executes itself with `personality(ADDR_NO_RANDOMIZE)` whenever the host kernel's `vm.mmap_rnd_bits` exceeds 28, and Docker's default seccomp profile blocks that call, so a native Linux runner must set `vm.mmap_rnd_bits = 28`. `cole-ubuntu-pc` sets it persistently in `/etc/sysctl.d/60-blob-royale-sanitizers.conf`; GitHub-hosted runners already use that value. This is a host prerequisite for running the TSan lane inside the pinned toolchain container, not a change to the gate. |
| Execution | The supported release artifact runs in OCI. Direct-host execution is development-only until an observed deployment requirement amends this ADR. The observed deployment runs the versioned runtime image built by the release profile under Docker with `--network host --read-only --cap-drop ALL --security-opt no-new-privileges:true --memory 1g --cpus 2`, as numeric user 65532, with its configuration and scenario mounted read-only at `/run/blob-royale`. Deployment happens only through `scripts/deploy-tailnet`, which runs the release profile first, so nothing unverified can be served. |
| Process supervision | The external OCI runtime or orchestrator owns start, restart, resource limits, log collection, and signal delivery. Blob Royale remains a foreground process and does not daemonize or supervise itself. The observed supervisor is Docker itself: the `--restart unless-stopped` policy owns restart, the run flags above own resource limits, and the json-file driver collects logs into five 50 MiB files. `docker stop` delivers the `SIGTERM` defined below. No orchestrator beyond that restart policy is used. |
| Application port | The process has one configurable HTTP/WebSocket listener, defaulting to TCP `8000`. Direct and developer execution binds loopback by default. An OCI deployment may bind `0.0.0.0:8000` only on a private container network; it must not publish that port directly to an untrusted network. No additional production port is accepted by this ADR. The observed deployment binds `127.0.0.1:8000` inside the host network namespace, which is the loopback case above and the safest binding the server accepts; the `0.0.0.0` container-network option remains permitted but unused. The proxy's HTTPS port is a proxy fact, not a second application port. |
| TLS boundary | Blob Royale does not terminate TLS. A deployment-managed reverse proxy or ingress terminates HTTPS/WSS and forwards to the private application listener. The observed boundary is `tailscale serve` on the deployment host: it terminates HTTPS on port 8444 with Tailscale-issued certificates, proxies `/api` to `http://127.0.0.1:8000/api` (serve strips the mount prefix, so the backend target carries `/api`), and serves the static `web/` deployable from `/srv/blob-royale/web` at `/`. Ports 443 and 8443 on that host belong to unrelated services, hence 8444. Exposure is tailnet-only and Tailscale Funnel is never enabled; enabling it is a trust-boundary change that must amend this ADR first. |
| Signals and exit | `SIGTERM` is the authoritative graceful-stop signal; `SIGINT` provides the same behavior for interactive use. Graceful stop closes network acceptance and sessions, stops and joins the simulation runtime, stops I/O, and exits zero. Initialization or runtime failure exits nonzero. `SIGKILL` is not graceful, and runtime configuration reload by `SIGHUP` is unsupported. `BlobRoyaleApplication` implements this process policy. |
| Release evidence | Release status requires the pinned image to run on a real Linux `x86_64` host. The primary GCC build and checks must pass there; Clang sanitizer checks provide independent evidence on native Linux. The evidence must include the image digest and reported host/toolchain facts. Two native runners now supply it: GitHub Actions on `ubuntu-24.04` runs `./scripts/verify-linux pr` as the pull-request gate `quality/linux-authoritative` (`.github/workflows/quality.yml`), whose first native run passed every build and test lane (GCC, Clang ASan/UBSan, and Clang TSan at 253 tests each, process smoke, web, Chromium end-to-end, and fuzz regressions) and exposed one archive-layout assumption in the dependency-evidence step that was corrected the same day; `cole-ubuntu-pc` runs the release profile that publishes. |
| macOS | Native macOS checks and Linux containers hosted by macOS are advisory. They may reveal portability defects but cannot publish or weaken the Linux-authoritative release status, because their host kernel and possible CPU emulation differ from the production authority. Observed on Apple Silicon: the GCC and Clang ASan/UBSan lanes do run under Rosetta emulation, but ThreadSanitizer cannot run there at all, because Rosetta rejects the same `personality(ADDR_NO_RANDOMIZE)` call, so the TSan lane is native-only. |

### Proportional threat boundary

The assets currently requiring protection are process availability and simulation integrity, not
accounts or stored secrets. The application listener is trusted only behind loopback or a private
OCI network. The TLS proxy or ingress is the outer network trust boundary. Direct public exposure is
unsupported. Protocol v1 removes network lifecycle mutation, applies exact Host/Origin/route policy,
and bounds transport resources, but it deliberately provides no authentication or confidentiality.
Authentication and player authorization remain deferred until shared-player semantics exist; any
deployment beyond the accepted local/private boundary requires a protocol and threat-model amendment
before release.

In the observed deployment that outer boundary is the tailnet: WireGuard device identity and
Tailscale ACLs admit every client, and no port is published to the public internet.
`tailscale serve` adds `Tailscale-User-Login`, `Tailscale-User-Name`, and `X-Forwarded-For`
headers; protocol v1 ignores them, and they grant no application authority. Because the proxy
connects from loopback, the server currently accounts every tailnet player as one loopback
principal for connection and rate limits. Proxy-supplied per-client accounting is planned for a
later protocol version and is not decided here. (Amended 2026-09-09: decided by ADR 0006 and
enabled at plan Step 10; see § "Consequences".)

### Unknowns and amendment trigger

The production host, on-premises location, kernel, container runtime, CPU model, resource limits, reverse proxy, certificate source, DNS name, and exposure were unknown at acceptance and are now observed and recorded above for `cole-ubuntu-pc`. What remains unknown is any second deployment host and any orchestrator beyond a single Docker restart policy; the per-client accounting identity behind the proxy is now the `X-Forwarded-For` address the trusted loopback proxy sets (amended 2026-09-09). The repository's current OCI digest and compiler/tool versions are pinned in `ci/linux/Dockerfile`; remaining deployment-specific facts stay the amendment trigger. Discovery that production requires a different distribution, architecture, libc, direct-host execution, in-process TLS, or a different trust boundary — including Tailscale Funnel or any other internet exposure — must amend or supersede this ADR before a release artifact is produced. It must not produce a silent fallback or an Apple-specific production path.

## Consequences

* **Positive:** One platform decides release readiness, so platform disagreement has a deterministic resolution.
* **Positive:** The OCI boundary makes userspace, compiler, libc, and dependencies recordable and reproducible across native Linux hosts.
* **Positive:** Foreground execution and external supervision establish a conventional RAII shutdown boundary for the application.
* **Positive:** The release authority and the deployment host are the same machine, so release evidence, the published image, and the running service share one kernel, userspace, and CPU instead of being compared across platforms.
* **Negative:** `arm64`, direct-host Linux, musl, and native macOS are not release targets.
* **Mitigation:** Add a target only after a concrete deployment need identifies its native runner and compatibility obligations.
* **Negative:** macOS-hosted container checks cannot complete release certification.
* **Mitigation:** The authoritative CI aggregate must run on native Linux/amd64 infrastructure.
* **Negative:** One host is both the authoritative release runner and the deployment, with no failover, and `scripts/deploy-tailnet` deliberately refuses to run anywhere else.
* **Mitigation:** Keep the deployment reproducible rather than redundant: the script is idempotent from a clean checkout at any certified commit, and the GitHub Actions gate remains a second, independent native Linux runner.
* **Negative:** A loopback proxy collapses every tailnet player into one accounting principal at the server's connection and rate limits.
* **Mitigation:** Keep the tailnet perimeter and the existing per-principal bounds until a later protocol version defines proxy-supplied per-client accounting; do not widen the bounds to hide the collapse.
* **Amended 2026-09-09 (ADR 0006, plan Step 10):** Per-client accounting is enabled. `deploy/ubuntu-pc/blob-royale.cfg` sets `trusted_proxy_addresses=127.0.0.1`, so every connection through `tailscale serve` is accounted to the `X-Forwarded-For` address the proxy sets and the bounds above apply per player rather than per proxy; no bound was widened. The residual is the one protocol v2 accepts for a single-operator host: a local process on `cole-ubuntu-pc` can forge both the principal and the display name by connecting to loopback with those headers, which is why this deployment is supported only where every local process is the operator's (`docs/operations/tailnet.md` § "Limits behind the proxy"; `docs/protocol/v2.md` § "Abuse cases and controls").
* **Operational:** Base-image and toolchain security updates require deliberate digest/version changes, a regenerated toolchain report, and a complete Linux verification run.
* **Operational:** The deployment host must keep `vm.mmap_rnd_bits = 28`; a re-imaged or reconfigured host that loses it fails the TSan lane inside the pinned container rather than skipping it silently.
* **Operational:** Docker's restart policy, run flags, and bounded json-file logs are the whole supervisor contract, so a repeatable nonzero startup exit is operator action, not a crash loop for an orchestrator to reschedule.
* **Reversibility:** A verified deployment requirement may supersede this ADR; the application remains standard C++20 and should not acquire OCI-vendor, Docker, or Tailscale APIs. The observed host is deployment configuration, not a build or runtime dependency of the process.

## Follow-up

* `ci/linux/Dockerfile` — owns the immutable Ubuntu base-image digest and pinned toolchain packages.
* `scripts/report-toolchain` — reports the host kernel/architecture, image digest, compilers, linker, CMake, Boost, and libc used as evidence.
* `scripts/deploy-tailnet` and `deploy/ubuntu-pc/` — own this host's deployment facts, refuse any other host, and run the release profile before replacing the container or the serve handlers.
* `docs/architecture/0001-linux-runtime-contract.md` — amend or supersede this ADR when observed production-host or trust-boundary facts conflict with the accepted baseline.

## Related

* [`../../.claude/plans/2026-08-04-feature-ready-foundation.md`](../../.claude/plans/2026-08-04-feature-ready-foundation.md) — accepted recovery plan and Linux-authority execution contract.
* [`../PROJECT_DEEP_DIVE.md`](../PROJECT_DEEP_DIVE.md) — repository-wide evidence for current portability, lifecycle, and exposure defects.
* [`../../src/application/README.md`](../../src/application/README.md) — current process composition and signal contract.
* [`../../src/server/README.md`](../../src/server/README.md) — current listener and reverse-proxy trust boundary.
* [`../operations/linux.md`](../operations/linux.md) — current build, deployment, shutdown, and incident runbook.
* [`../../.claude/plans/2026-09-06-playable-prototype-tailnet.md`](../../.claude/plans/2026-09-06-playable-prototype-tailnet.md) — accepted plan that observed and verified the deployment-host facts recorded here.

**Amended 2026-09-06:** The deployment host, supervisor, listener binding, and TLS boundary that
this ADR left open are now observed on `cole-ubuntu-pc` and recorded in the contract table, threat
boundary, and consequences. They confirm the accepted baseline rather than conflicting with it, so
the decision and its `Accepted` status are unchanged.
