# Boundary fuzzing

These targets exercise the exact production input boundaries with Clang libFuzzer, ASan, and
UBSan. They never replace the deterministic unit suites. Pull requests replay every checked-in
corpus member once through `scripts/verify-fuzz-regressions`; nightly and release profiles also run
a time-bounded mutating campaign through `scripts/verify-fuzz-bounded`.

The fuzz workspace deliberately writes a process-local regular file because configuration and
scenario acquisition checks are part of the production contract. The map harness writes the mutable
`fuzz-map/map.cfg` beside fixed valid `static_bodies.csv` and `markers.csv` headers; its declared map
name must be `fuzz-map` to reach terrain parsing and value validation. Any minimized crash input belongs
in the matching `corpus` directory with a descriptive stable filename. A crash must be fixed; do not
delete, skip, retry, or suppress the input.

The native targets cover command-line shape, INI configuration, map-directory terrain declarations,
scenario CSV, untrusted
protocol-v1 request IDs, protocol-v2 client command envelopes, and raw HTTP header preflight plus
Beast header parsing. The command harness checks the decoder's session-stamped identities,
accepted-command invariants, and early oversized-frame rejection. They link
`blob_application_input`, `blob_protocol`, and `blob_server_http_preflight`, so every production
translation unit still has one canonical CMake owner. Protocol-v1 JSON is inbound only at the
browser boundary; its deterministic malformed message corpus lives in
`frontend-react/src/features/simulation/protocolMutationCorpus.test.ts` and runs in the blocking
Vitest gate. Protocol-v2 command JSON enters through the WebSocket session and is exercised by
`protocol_command_fuzzer.cpp` with the production decoder.
