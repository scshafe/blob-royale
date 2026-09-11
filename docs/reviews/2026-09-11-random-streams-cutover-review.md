# Named random streams cutover review, 2026-09-11

Status: source review and final Step 9 verification complete. Root owns toolchain evidence. This
is the Step 9 review, separate from the proposed Step 10 movement-tuning boundary review.

## Reviewed contract

Independent cross-reviews covered simulation/hazard ownership and protocol/browser admission;
the author of each implementation slice reviewed a different slice. No remaining source finding
affects deterministic stream initialization, hazard behavior, rollback, or server count encoding.
Root corrected three stale API comments and added the missing v3 development-release row.

- Stable registry ordinals are hazards 0, hill 1. Hazards keep the old seed/state and draw order.
  Hill uses the specified unsigned mixing derivation. Initialization advances neither stream.
- Both streams are value-owned by the working world. A failed real tick after both draws leaves
  the previous snapshot unchanged; retry and subsequent full-bit words match a clean control
  and independent frozen generators. No production fault injection or transaction seam was added.
- The frozen production hazard proof retains its independent 280-tick schedule, declaration ties,
  reservation gap, and newborn component bits. Seven extra hill draws per tick change no hazard.
- Snapshot counts are owned uint64 values, not seeds or state. The actual shared encoding helper
  tests zero, the safe-integer maximum, maximum plus one, and uint64 maximum for both stream keys.
  Real world draws also pass through the complete encoder, canonical byte order, and byte budget.
- Generated closed schemas and client fixtures agree. Validation precedes sequence-state update
  and publication to callbacks. Existing v1 assets and accepted motion/replay oracles are unchanged.

## Inherited browser numeric-decoding limitation

The browser still uses its existing `JSON.parse` to binary64, then schema/semantic-validation
boundary in `SimulationApi.handleSessionFrame`. A malicious or faulty server can send a decimal
token whose mathematical value is fractional but rounds to a permitted integer before schema
validation. Local read-only Node reproduction:

```text
token                     JSON.parse result     Number.isSafeInteger
9007199254740991.1         9007199254740991       true
1e-400                    0                      true
```

The new parsed-value tests do not prove lexical rejection of these tokens. This is an inherited
general parser limitation, not a weakened encoder check: the C++ writer emits exact unsigned
integers and rejects every uint64 count above 2^53-1. Ordinary malformed shape/type/range checks
remain covered, but this review does not claim arbitrary-precision decimal validation or exact
lexical browser conformance. Lossless numeric parsing is not introduced as an incidental Step 9
change. Carry the limitation into the final protocol review before any release-conformance claim.

## Evidence

Pre-delegation gates: 803/803 GCC and 803/803 Clang ASan/UBSan, with old generator/world/hazard
readers still unchanged. Final exact commands:

```sh
./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|fixtures'
./scripts/verify-focused 'unit.simulation|unit.gameplay|unit.protocol|fixtures' linux-clang-asan-ubsan
./scripts/run-linux-toolchain -- ./scripts/verify-web
```

Both C++ lanes passed 974/974 with zero failures. Web passed 438/438 with zero failed/skipped/todo,
plus installation, formatting, generation drift, schema examples, type checking, lint, and production
build. Pinned C++ formatter dry-run and `git diff --check` passed. No acceptance test was skipped or
retried. Only three API comments and documentation were corrected after the first final build began;
no behavior or test expectation changed.

All results are advisory Mac/arm64 hosting Docker Linux/amd64, not native performance or release
certification. Logs are retained under `/tmp/blob-royale-random-streams.60XDcn` as
`preproof-gcc.log`, `preproof-clang.log`, `final-gcc.log`, `final-clang.log`, `final-web.log`,
`final-generation-format.log`, and `final-format-check.log`.
