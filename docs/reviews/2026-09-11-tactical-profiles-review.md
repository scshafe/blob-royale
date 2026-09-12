# Step 15 verification and source review

Status: Step 15 complete and verified on 2026-09-11; advisory execution evidence, not release certification.

The owner's approved four-setting scope and implementation contract are recorded in
[`2026-09-11-tactical-profile-contract.md`](2026-09-11-tactical-profile-contract.md).
All execution below uses Mac/arm64-hosted Docker Linux/amd64 and is advisory, not native
performance evidence. Step 5 remains unapproved; Phase C, push, and deployment remain out of scope.

## Pre-cutover source review

The independent helper review found no arithmetic or lookup-domain divergence in the unused
helpers or complete Chaser/HillSeeker candidates. It identified a missing complete helper-backed
Racer comparison: raw division checks and the earlier projection-only oracle were insufficient.
The new frozen Racer path now compares unchanged production, independent old operations, and
helper-backed operations across targeting, recovery, alternate road bindings, repeats, spawning,
stun, and replacement publications. A follow-up read-only review closed this finding. The original
projection oracle and production readers stayed unchanged throughout the prerequisite.

A separate boundary review found no blocking issue in catalogue partitions, exact optional
profile admission, guarded joins, or publication. The reviewer flagged one default-empty-runtime
test helper advertising plain kinds independently; it now publishes an empty catalogue too.
These are source findings, not substitutes for execution.

## Preliminary corrections

- The preliminary prerequisite GCC builds stopped on new lobby/runtime-test range loops copying
  command variants and declarations under `-Werror`. Borrow the initializer-list entries;
  expected commands stay unchanged. Neither run reached its test selection.
- Ajv's strict-required check rejected the first conditional schema form for forbidding profile
  metadata on non-NPC seats. A false property schema expresses the same prohibition correctly.
  Generation and example validation then passed: 78 schemas and 28 examples.
- Pinned formatting was applied to the new browser slice. Typechecking then found two new test
  defects: an unsupported role-query option and a missing required controller-kind field. An
  anchored name matcher and the complete fixture corrected them without weakening assertions.
- The subsequent full web gate reached tests and failed. The canonical gate deletes its JSON
  report on exit. A separate verbose diagnostic passed 741/742 tests and identified one new API
  fixture permitting only lobby 1 while its test intentionally reconnected to lobby 2. Admit both
  fixture URLs; preserve the room switch and catalogue assertions. A targeted API diagnostic
  then passed 79/79; the fresh full gate passed 742/742 with no skipped/todo tests.
- The first final GCC build stopped on a root-authored integration test including the Royale
  mode header without its `royale/` directory. Correct the include to the existing public path;
  no production code or assertion changed. The fresh full selection is required after this fix.
- The first runtime/server/fixture supplement passed 261/261 on GCC, but Clang passed only
  258/261: three existing session-lifecycle/display-name tests observed no open controller.
  The chain stopped before corpus/browser checks. A direct unchanged Clang diagnostic passed
  two of those three tests and reproduced the ownership failure. Investigation identified the
  local harness's never-ready snapshot publication: a presentation callback can retire the
  newly opened controller while `io_context::poll()` drains work. The failing trace recorded
  `session.opened` before the directory assertion observed retirement; the close handshake had
  not completed, so no `session.closed` event was yet available. A separate running publication
  owner now supplies readiness through the ordinary runtime, while the existing unstarted
  command fixture preserves deterministic admission/overlap assertions. Integrated lifecycle
  tests retain their original live-runtime harness. Add an explicit unready-publication close
  regression for wire code 1013 and `service_not_ready`; no production change, readiness bypass,
  deadline increase, or assertion weakening. Focused and all final gates must run after this fix.

## Execution evidence

Logs: `/tmp/blob-royale-step15.whkhDu`.

Both exact controller prerequisites passed 95/95: GCC (`prerequisite-gcc-copies-fixed.log`) and
Clang ASan/UBSan (`prerequisite-clang.log`). All 55 changed/new C++ paths remained hash-identical
across these passing runs. The reader cutover and remaining implementation were released only then.
The original selection before the server-test harness correction passed 1,014/1,014 on both GCC (`final-gcc-include-fixed.log`) and
Clang ASan/UBSan (`final-clang.log`), including 124 controller tests per lane. All 95 changed/new
C++ paths remained hash-identical across those passing runs. The later correction changes only
`tests/unit/server/session_websocket_session_tests.cpp`; fresh final evidence is required below.
Full `./scripts/run-linux-toolchain -- ./scripts/verify-web` passed both before and after the final
C++ pair (`web-full.log`, `web-final.log`): 742/742,
formatting, generated drift, 78 schemas/28 examples, strict typecheck, lint, and production build.
The unchanged dependency install reports four existing audit findings; no dependency update was
performed. The session-harness correction and fresh complete results below supersede those earlier
runs as final step-completion evidence.

After the session-harness correction, the focused advisory sanitizer command
`./scripts/verify-focused 'unit.server.SessionWebSocketSession' linux-clang-asan-ubsan` passed
10/10, including the explicit unready/1013 regression (`session-fixture-corrected-clang.log`).
Independent read-only review found no new lifetime, isolation, or cleanup issue. All 95 changed/new
C++ paths were frozen again before restarting the complete final chain, serially:

```sh
./scripts/verify-focused 'unit.controllers|unit.application|unit.simulation|unit.protocol'
./scripts/verify-focused 'unit.controllers|unit.application|unit.simulation|unit.protocol' linux-clang-asan-ubsan
./scripts/run-linux-toolchain -- ./scripts/verify-web
./scripts/verify-focused 'unit.runtime|unit.server|fixtures'
./scripts/verify-focused 'unit.runtime|unit.server|fixtures' linux-clang-asan-ubsan
./scripts/verify-fuzz-regressions
./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e
```

Fresh advisory results after the harness correction (same directory):

| Gate | Result | Log |
| --- | --- | --- |
| Original GCC selection | 1,014/1,014 | `complete-gcc.log` |
| Original Clang ASan/UBSan selection | 1,014/1,014 | `complete-clang.log` |
| Complete web gate | 742/742; 78 schemas/28 examples; production build | `complete-web.log` |
| Runtime/server/fixture GCC supplement | 262/262 | `complete-supplement-gcc.log` |
| Runtime/server/fixture Clang ASan/UBSan supplement | 262/262 | `complete-supplement-clang.log` |
| Fixed-corpus replay | 91 executions across seven harnesses | `complete-fuzz.log` |
| Complete production-browser gate | 13/13; zero retries, flakes, or skips | `complete-browser.log` |

The C++ supplements include the three previously failing session cases and the new explicit
unready-publication regression. Corpus counts: application 15, map 10, CLI 3, HTTP 3, protocol
command 29, request id 29, scenario 2. This is fixed-corpus replay, not an exploratory fuzz campaign.
The browser gate includes both new tactical specs: exact profile selection/publication across a
new session, and profile-dependent movement through real public snapshots. All 95 changed/new C++
paths remained hash-identical through the complete final serial chain. Pinned C++ formatting
dry-run (`complete-format-check.log`) and `git diff --check` passed. Accepted replay/physical
fixtures, shipped maps, deployment files, and historical benchmark baselines remain unchanged.
Phase B is complete; Step 5 still requires human acceptance, and Phase C remains unauthorized.
Nothing was pushed or deployed, and no native performance or release claim is made.

## Application integration review

Independent read-only review found no introduced blocker in full-declaration replacement,
guarded joins, failed-creation caching, seed separation, profile ownership, or catalogue sharing.
Root added deterministic no-thread integration tests for queued joins after replacement,
immediate pending/seated retirement, clear/resize/human displacement, failed-profile cache
invalidation, abandonment, initial roster order, and runtime/session admission agreement.
Both original lanes passed. The review noted a preexisting allocation-failure rollback gap after
host registration and before reconciler bookkeeping; Step 15 did not change that ordering.

The configuration review found no actionable issue in required fields, finite/integer bounds,
catalogue ownership/order, direct-vector invariants, or metadata-driven mode/profile admission.
A separate integration-test reading caught two optional-return values mistakenly declared as
pointers before compilation; corrected. It also prompted explicit post-allocation registry-refusal
cleanup and pending-versus-seated join-budget boundary cases.

## Tactical source review

Independent production review found no concrete contract deviation in ordered draws, unit go/coast
arithmetic, target refresh and nonrenewing leases, body/generation/stun invalidation, seed mixing,
copy-then-commit policy state, pre-mutation observation admission, or canonical terrain screening.
The unchanged base thrust writer and frozen legacy references remain in place.

Near-maximum-tick tactical observations are not injected through a new kernel seam: simulation
construction commits tick zero and real snapshots require stepping. The existing direct TickWindow
overflow tests cover the checked arithmetic; controller failure-retry tests use bounded authored
public-state failures. This is a test-observation limitation, not a skipped supported failure path.

## Browser smoke review

Independent read-only review found no actionable issue in the isolated movement smoke. It resolves
identities from seat declarations and observes actual spawn assignment, retaining snapshots in a
bounded committed-tick window. Seeker approach/directed propulsion, stationary coaster and human,
stable identities, bounded displacement, and pair separation isolate profile-dependent motion.
The two profiles differ only in seek probability. The browser sends only Start; no test movement
command substitutes for the tactical controller. Browser execution remains required separately.
