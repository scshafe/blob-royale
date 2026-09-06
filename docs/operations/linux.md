<!-- canonical: linux_operations -- build, deployment, and incident runbook -->

# Linux operations runbook

Blob Royale is supported as a foreground process in the digest-pinned Ubuntu 24.04 `linux/amd64`
OCI userspace defined by `ci/linux/Dockerfile`. A native Linux/amd64 host is required to certify a
release. The process does not daemonize, restart itself, terminate TLS, reload configuration, or
write persistent state.

The playable tailnet deployment on `cole-ubuntu-pc` has its own runbook in [`tailnet.md`](tailnet.md);
this document remains the general contract it builds on.

## Build and evidence

Run the release gate from a clean checkout on the native Linux runner:

```sh
./scripts/verify-linux release
```

Preserve the complete output of `scripts/report-toolchain`, the tested source state, image content
ID, test and sanitizer results, versioned deployables, dependency evidence, and benchmark report
with the release record. A Docker-on-macOS run is advisory even when every check succeeds. The
Docker client OS/architecture and the connected daemon OS/architecture participate in authority
classification and are recorded in every source stamp, layout, evidence manifest, and publication
record. A remote context cannot borrow authority from a Linux/x86_64 client: the release profile
refuses to publish `current` unless both the client and actual Docker daemon are native
Linux/x86_64.

After the release gate succeeds, build the deployable image with:

```sh
./scripts/build-release-linux
```

The command is a safe standalone entry point for `./scripts/verify-linux release`; it does not build
from whichever files happen to be present. The in-toolchain gate captures the tracked/unignored
source identity before its first check and refuses to certify if that identity changes during the
profile. Its final deterministic stamp also binds the GCC release executable and production web
tree. It records the full Git commit and clean/modified state; the release profile rejects a
modified checkout, while PR/nightly diagnostics still bind their exact working tree. The host
rejects changed inputs, stages a content-addressed version, builds
`blob-royale:linux-amd64-local`, records its Docker content ID, saves the final image archive, and
has pinned Syft 1.44.0 and Grype 0.112.0 scan that archive. Scanning the archive inventories the
actual base and transitive runtime packages rather than reconstructed package metadata. The
separate web CycloneDX document is deterministically rendered from npm's complete production graph,
with versions, package URLs, licenses, distribution sources, and integrity hashes supplied by the
committed lockfile. Its graph is scanned independently and excludes build/test tooling.

One exclusive `out/release/.pipeline-lock` serializes the complete source-check-through-publication
transaction. A second invocation fails closed; if an invocation was forcibly terminated, confirm
that no quality/release process remains before removing that generated lock. Immutable layouts and
evidence are staged without changing a current pointer. After image smoke, both scans, npm audit,
manifest/hash validation, and an in-lock final freshness check pass, one atomic rename switches
`out/release/current` to an immutable publication record. That record binds the release ID,
evidence ID, image content ID, stamp, client/daemon identities, and authority. During the one-time
legacy-alias conversion, publication refuses to touch an existing noncanonical reader path unless
the operator has stopped all readers and explicitly acknowledges the maintenance window:

```sh
BLOB_ROYALE_OFFLINE_ALIAS_MIGRATION=readers-stopped ./scripts/verify-linux release
```

The publisher keeps recovery copies and rolls back preparation failures. It installs and validates
both stable aliases while readers are stopped, rechecks source/artifact freshness under the pipeline
lock, and performs the same final atomic `current` rename as a fresh or steady-state publication.
Do not set this variable during ordinary publication or restart readers before the command passes.

`out/release/blob-royale` is a stable alias through `out/release/current`; its selected layout
contains two independent deployables:

- `server/` is the runtime-image build context: the executable, examples, notices, and licenses;
- `web/` is the static production site for the deployment-managed HTTPS reverse proxy.

The server image intentionally contains no web assets or self-referential SBOM. Its project files
are root-owned and non-writable, and the configured runtime identity is numeric user 65532. The
external `out/security/current` path is another stable alias through the same publication pointer;
it therefore selects matching content-addressed evidence containing the saved Docker archive,
runtime and web CycloneDX SBOMs, both Grype reports, the npm production audit, and a manifest binding
their hashes to the image content ID and Linux verification stamp. Preserve the selected publication
directory, layout, and evidence directory with the release record. The mutable
`blob-royale:linux-amd64-local` tag is a convenience; the publication's immutable content ID is the
deployment identity.

On an Apple Silicon development host, the pinned amd64 Syft/Grype binaries may fault inside QEMU
while reading an image archive even when image build and process smoke checks succeed. That is an
advisory-host limitation: do not substitute a host-native scanner, change the pinned scanner, or
publish partial evidence. Re-run the unchanged profile on native Linux/amd64.

## Process contract

Start exactly one foreground process with explicit paths:

```sh
out/build/linux-gcc-release/blob-royale \
  --config /run/blob-royale/blob-royale.cfg \
  --scenario /run/blob-royale/scenario.csv
```

Mount both files read-only. Invalid or missing input fails before listeners or worker threads start.
The supervisor owns restart policy, CPU/memory limits, stdout/stderr collection, and signal delivery.
Send `SIGTERM` for graceful shutdown and allow the bounded server/session deadline to finish before
using `SIGKILL`. `SIGINT` is equivalent for interactive operation. `SIGHUP` reload is unsupported;
replace the process to change configuration or scenario.

Use a restart policy that distinguishes configuration failures from transient host failures. A
repeatable nonzero startup exit is operator action, not a crash loop to retry indefinitely.

## Network boundary

The checked-in development file binds `127.0.0.1:8000`. Production may bind `0.0.0.0:8000` only on
an unreachable private OCI network with explicit `allowed_hosts`, `allowed_origins`, and
`trusted_proxy_addresses`. Never publish the application port directly to an untrusted network.

A deployment-managed reverse proxy must:

- serve the matching staged `web/` deployable as the site root and route same-origin `/api` traffic
  to the private application listener;
- terminate current HTTPS/WSS and forward only to the private application listener;
- preserve the exact Host, Origin, target, and WebSocket Upgrade fields expected by policy;
- strip or validate forwarding and inbound request-ID headers, which grant no application authority;
- disable response buffering and application compression for the snapshot stream;
- apply external-client-aware request, connection, header, and upgrade limits; and
- use idle timeouts longer than the server's WebSocket keepalive interval.

Protocol v1 is read-only and unauthenticated. It is appropriate only for public simulation state;
accounts, secrets, private matches, or commands require a threat-model and protocol amendment.

## Health and shutdown

Use `GET /api/v1/health/live` for process liveness and `GET /api/v1/health/ready` for publication
readiness. Readiness is false until a complete post-start snapshot exists and becomes false when the
runtime is quiescent, stopping, terminal, or failed. Do not use liveness as a substitute for
readiness during rollout.

During shutdown, first remove the instance from serving traffic, then deliver `SIGTERM`, wait for
zero exit, and only then dispose of the container. A nonzero exit or deadline breach is an incident
even if the supervisor subsequently restarts the process.

## Logs and diagnostics

Application logs are newline-delimited JSON on standard error. Collect each line atomically and
index `severity`, `event`, `error_code`, lifecycle state, request/connection ID, and tick when those
fields apply. Do not log request bodies, headers, full targets, configuration contents, or scenario
rows. Protocol request IDs are correlation values, not authenticated identities.

For an incident, retain the process exit code, final structured events, image/toolchain identity,
configuration hash, scenario hash, and supervisor reason. Reproduce with the exact files and image;
do not repair a Linux failure by adding a macOS-specific branch.

## Resource and performance policy

Connection, request, message, rate, response-queue, and snapshot-pending bounds are part of the
server contract. An apparent throughput improvement that removes one of those bounds is a
correctness regression. Use `./scripts/run-benchmarks-linux` on a dedicated native Linux runner to
compare deterministic scenarios; shared-runner and emulated results remain advisory until an
explicit budget is accepted.

The service currently stores no durable state. Backup and restore therefore apply only to the
versioned configuration/scenario inputs and release evidence.
