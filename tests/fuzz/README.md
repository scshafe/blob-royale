# Boundary fuzzing

These targets exercise the exact production input boundaries with Clang libFuzzer, ASan, and
UBSan. They never replace the deterministic unit suites. Pull requests replay every checked-in
corpus member once through `scripts/verify-fuzz-regressions`; nightly and release profiles also run
a time-bounded mutating campaign through `scripts/verify-fuzz-bounded`.

The fuzz workspace deliberately writes a process-local regular file because configuration and
scenario acquisition checks are part of the production contract. Any minimized crash input belongs
in the matching `corpus` directory with a descriptive stable filename. A crash must be fixed; do not
delete, skip, retry, or suppress the input.

The native targets cover command-line shape, INI configuration, scenario CSV, untrusted
protocol-v1 request IDs, and raw HTTP header preflight plus Beast header parsing. They link
`blob_application_input`, `blob_protocol`, and `blob_server_http_preflight`, so every production
translation unit still has one canonical CMake owner. Protocol-v1 JSON is inbound only at the
browser boundary; its deterministic malformed message corpus lives in
`frontend-react/src/features/simulation/protocolMutationCorpus.test.ts` and runs in the blocking
Vitest gate. The current server has no client-supplied JSON body parser to fuzz.
