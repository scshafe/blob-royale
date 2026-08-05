<!-- canonical: native_server_integration -- the complete real-process protocol-v1 test slice -->

# Native server integration tests

This directory owns the black-box native test boundary for the shipped `blob-royale`
executable. It does not instantiate server internals or maintain a second protocol
implementation.

CTest runs two independent ordered fixtures through the same exact-process supervisor:

1. `integration.server_process_fixture.setup` allocates a loopback port through an
   OS-assigned bind, writes strict configuration and scenario inputs, and starts the real
   target beneath a detached supervisor. The reservation remains inherited until `exec`.
   Because the production server does not accept an inherited listener, the supervisor does
   not assume that this handoff won a race: it recognizes the typed listener-bind failure and
   repeats the complete reservation/startup transaction within a fixed eight-attempt bound.
   Readiness is the proof that the final child actually owns the selected port.
2. `integration.server_protocol_contracts` validates HTTP envelopes and transport headers,
   consumes two complete monotonic protocol-v1 WebSocket snapshots, and asserts the one-way
   client-frame policy.
3. `integration.server_process_fixture.cleanup` signals the supervisor, which sends SIGTERM to
   the server process group and publishes its exact `waitpid` result. Cleanup requires a
   bounded, unforced exit status of zero.

The second fixture uses 512 stationary players and the maximum 60 Hz presentation cadence. Its
abuse/soak phase completes 128 TCP connect/EOF/close lifecycles, proves a raw obsolete-folded
header is rejected before HTTP routing, and leaves one WebSocket peer completely unread with a
constrained receive buffer. While that peer reaches the five-second write deadline, a second peer
must continuously drain bounded text frames for ten seconds. Fixed interval samples validate the
complete 512-player schema, exact message sequence, and monotonic tick while later sampled
sequences prove the intervening drained frames contained no transport gaps. Readiness must remain
true throughout. The stalled peer must then produce the structured `1013` slow-consumer terminal
observation while the client remains completely unread. A separate fixture cleanup again requires
exact SIGTERM exit zero, proving the abuse run leaves no process or session that can obstruct
bounded shutdown.

Each fixture directory is build-local and contains generated inputs plus separate server and
supervisor logs. Setup refuses to overwrite it while either recorded process is alive. CTest's
fixture cleanup runs even when a contract or abuse phase fails, so a failed test does not leak a
listener into later verification. All socket operations and CTest phases have hard deadlines;
the abuse assertions advance through observed socket traffic without correctness sleeps, retries,
skips, or test-result caches. The supervisor's bounded readiness polling and recovery from an
OS-assigned port handoff race are fixture infrastructure, not assertion retries.

The harness depends on Boost.Beast/Boost.JSON for independent wire observations and POSIX
process primitives for Linux-authoritative lifecycle proof. It depends on no production C++
library; `$<TARGET_FILE:blob-royale>` is its only application seam.
