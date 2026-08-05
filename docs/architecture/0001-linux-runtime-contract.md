<!-- canonical: linux_runtime_contract -- release authority and deployment boundary -->

# 1. Adopt a Linux/amd64 OCI release authority

* **Status:** Accepted
* **Date:** 2026-08-04
* **Deciders:** Project owner

## Context and Problem Statement

Blob Royale has no recorded production host or release platform. Its current bootstrap is not portable: the server defaults to a developer LAN address (`src/boost-headers/boost-program-options.cpp:14`), uses working-directory-relative config and fixture paths (`src/boost-headers/boost-program-options.cpp:17`, `src/boost-headers/boost-program-options.cpp:18`), and embeds one developer's Linux home path (`main.cpp:95`). Native macOS results therefore cannot establish whether the intended Linux service builds, shuts down, or behaves correctly. The project needs one explicit release authority while deployment-host facts remain unknown.

## Considered Options

* **A. Digest-pinned Ubuntu 24.04 LTS OCI image on native Linux/amd64.** Use one pinned userspace and toolchain for local verification and CI, but accept release evidence only when that image runs on a real Linux host.
* **B. Direct execution on an Ubuntu host.** This removes the container boundary but makes the host package set and supervisor part of the build contract before either is known.
* **C. Equal Linux and macOS authority.** This broadens the support matrix and permits Apple-specific behavior to influence a Linux service without a demonstrated production need.

## Decision Outcome

**Chosen: Option A.**

Until observed production facts supersede this decision, the authoritative baseline is an Ubuntu 24.04 LTS OCI image for `linux/amd64`. The image must use an immutable manifest digest rather than a floating tag. Step 3 of the feature-ready foundation plan records that digest and the exact tool versions; no release is authoritative until those pins exist (`.claude/plans/2026-08-04-feature-ready-foundation.md` Step 3).

The runtime contract is:

| Concern | Accepted contract |
|---|---|
| Distribution | Ubuntu 24.04 LTS userspace from the digest-pinned OCI base image. |
| Architecture | `linux/amd64` only. `arm64` and other architectures require a later amendment and their own native evidence. |
| Compiler and libc | GCC is the primary release compiler. Clang supplies an independent warnings-and-sanitizers check. The glibc shipped by the pinned Ubuntu image is authoritative; musl and host libc variants are unsupported. The verification report must emit exact compiler, linker, CMake, Boost, and libc versions. |
| Execution | The supported release artifact runs in OCI. Direct-host execution is development-only until an observed deployment requirement amends this ADR. |
| Process supervision | The external OCI runtime or orchestrator owns start, restart, resource limits, log collection, and signal delivery. Blob Royale remains a foreground process and does not daemonize or supervise itself. No specific orchestrator is selected yet. |
| Application port | The process has one configurable HTTP/WebSocket listener, defaulting to TCP `8000` to preserve the existing CLI contract (`src/boost-headers/boost-program-options.cpp:16`). Direct and developer execution binds loopback by default. An OCI deployment may bind `0.0.0.0:8000` only on a private container network; it must not publish that port directly to an untrusted network. No additional production port is accepted by this ADR. |
| TLS boundary | Blob Royale does not terminate TLS. A deployment-managed reverse proxy or ingress terminates HTTPS/WSS and forwards to the private application listener. Until that boundary exists, supported use is loopback-only. The proxy product and certificate source remain deployment facts to record later. |
| Signals and exit | `SIGTERM` is the authoritative graceful-stop signal; `SIGINT` provides the same behavior for interactive use. Graceful stop closes network acceptance and sessions, stops and joins the simulation runtime, stops I/O, and exits zero. Initialization or runtime failure exits nonzero. `SIGKILL` is not graceful, and runtime configuration reload by `SIGHUP` is unsupported. The current bootstrap handles `SIGINT`/`SIGTERM` only for the I/O context and then returns failure, which does not satisfy this contract (`main.cpp:31`, `main.cpp:99`). |
| Release evidence | Release status requires the pinned image to run on a real Linux `x86_64` host. The primary GCC build and checks must pass there; Clang sanitizer checks provide independent evidence on native Linux. The evidence must include the image digest and reported host/toolchain facts. |
| macOS | Native macOS checks and Linux containers hosted by macOS are advisory. They may reveal portability defects but cannot publish or weaken the Linux-authoritative release status, because their host kernel and possible CPU emulation differ from the production authority. |

### Proportional threat boundary

The assets currently requiring protection are process availability and simulation integrity, not accounts or stored secrets. The application listener is trusted only behind loopback or a private OCI network. The TLS proxy or ingress is the outer network trust boundary. Direct public exposure is unsupported: current lifecycle routes are unauthenticated and allow any origin (`src/server/helpers.hpp:139`, `src/server/helpers.hpp:143`), and WebSocket upgrades apply no application path or origin decision (`src/server/my_http_server.cpp:90`). Authentication and player authorization remain deferred until shared-player semantics exist, but any deployment beyond a trusted local/private boundary requires a protocol and threat-model amendment before release.

### Unknowns and amendment trigger

The actual production host, cloud or on-premises location, kernel, container runtime/orchestrator, CPU model, resource limits, reverse proxy, certificate source, DNS, and public exposure are unknown. The exact OCI digest and compiler/tool versions remain a Step 3 implementation decision. Discovery that production requires a different distribution, architecture, libc, direct-host execution, in-process TLS, or a different trust boundary must amend or supersede this ADR before a release artifact is produced. It must not produce a silent fallback or an Apple-specific production path.

## Consequences

* **Positive:** One platform decides release readiness, so platform disagreement has a deterministic resolution.
* **Positive:** The OCI boundary makes userspace, compiler, libc, and dependencies recordable and reproducible across native Linux hosts.
* **Positive:** Foreground execution and external supervision establish a conventional RAII shutdown boundary for the application.
* **Negative:** `arm64`, direct-host Linux, musl, and native macOS are not release targets.
* **Mitigation:** Add a target only after a concrete deployment need identifies its native runner and compatibility obligations.
* **Negative:** macOS-hosted container checks cannot complete release certification.
* **Mitigation:** The authoritative CI aggregate must run on native Linux/amd64 infrastructure.
* **Operational:** Base-image and toolchain security updates require deliberate digest/version changes, a regenerated toolchain report, and a complete Linux verification run.
* **Reversibility:** A verified deployment requirement may supersede this ADR; the application remains standard C++20 and should not acquire OCI-vendor APIs.

## Follow-up

* `ci/linux/Dockerfile` — Step 3 must add the immutable Ubuntu base-image digest and pinned toolchain packages.
* `scripts/report-toolchain` — Step 3 must report the host kernel/architecture, image digest, compilers, linker, CMake, Boost, and libc used as evidence.
* `docs/architecture/0001-linux-runtime-contract.md` — amend or supersede this ADR when observed production-host or trust-boundary facts conflict with the accepted baseline.

## Related

* [`../../.claude/plans/2026-08-04-feature-ready-foundation.md`](../../.claude/plans/2026-08-04-feature-ready-foundation.md) — accepted recovery plan and Linux-authority execution contract.
* [`../PROJECT_DEEP_DIVE.md`](../PROJECT_DEEP_DIVE.md) — repository-wide evidence for current portability, lifecycle, and exposure defects.
* [`../../main.cpp`](../../main.cpp) — current process bootstrap, signal handling, and hard-coded document root.
* [`../../src/boost-headers/boost-program-options.cpp`](../../src/boost-headers/boost-program-options.cpp) — current address, port, config, and fixture defaults.
* [`../../src/server/helpers.hpp`](../../src/server/helpers.hpp) — current unauthenticated lifecycle and state routes.
