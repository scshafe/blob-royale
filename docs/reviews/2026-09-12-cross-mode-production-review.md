# Step 23 production acceptance review

Parent checkpoint `0bb089b`. Contract:
[`2026-09-12-cross-mode-production-contract.md`](2026-09-12-cross-mode-production-contract.md).
All results are Mac/arm64-hosted Docker Linux/amd64 **advisory**.

## Checks

| Gate | Result |
|---|---|
| `verify-focused 'integration\|fixtures'` | GCC 69/69 |
| Same selection, `linux-clang-asan-ubsan` | 69/69 |
| `run-linux-toolchain -- verify-web` | 986/986; format, generation, 83 schemas / 33 examples, typecheck, lint, build passed |
| `run-linux-toolchain -- verify-browser-e2e` | Chromium 24/24; no retries, skips, or flakes |

The exact CTest regex also matches four unit cases whose names contain `integration` or
`fixtures`; the 69 total is 56 fixture cases, nine process/session cases, and those four matches.
Each of the five new replay cases checks independently derived behavior and 100 fresh runs of
the full committed snapshot sequence. These are deterministic behavior checks, not capacity data.

## New evidence

- Five authored replays prove first-quantum charged body contact, a charged hole crossing whose
  free endpoint is supported, three race gates and a fractional finish before a later contact,
  earlier race gates followed by a fall that suppresses later finish/contact, and default shield
  contacts exactly at perfect expiry, the last protected tick, and protection expiry.
- Eleven new browser cases cover running peer tuning, random-roam hill/unsafe terrain and return,
  first-quantum body contact, race fall/progress and finish, perfect/ordinary/late shield,
  hazard stun/lifetime, and tactical offense plus shield prediction at deployment drag 2.
- The tuning case uses three real clients and a post-commit directory tick as the other room's
  freshness barrier. The held steering command stays unchanged while committed acceleration
  changes; both peers agree on values/revision/effective tick. Dirty drafts survive and require
  review. The high speed ceiling is synchronized, not physically reached at this fixture's drag.
- Perfect parry starts with positively observed propulsion, then verifies persistent input
  invalidation through stun expiry and repeated keydown before a fresh activation. The new
  zero-acceleration terrain fixtures isolate launch geometry; they do not independently prove
  the absence of resumed propulsion. Positive-acceleration combat and existing cursor/return
  cases own that behavior.
- The existing canvas recorder now records stroked arcs, making actual perfect/ordinary/stun
  marks observable. Moving hill paint is matched to recent published centers. These are canvas
  command and geometry observations, not a claim of pixel clarity or visual approval.
- All thirteen existing browser cases remain. Their assertions are unchanged; the tactical
  movement case only imports its snapshot decoder from the shared observation owner.

## Repairs and verification history

The first C++ build rejected a new helper's temporary `PhysicsBody` access because its rvalue
accessors are deliberately deleted. Returning the body by reference to the retained snapshot
fixed the helper. The five new replay oracles then passed without changing their numeric claims.

The initial full GCC gate exposed a missed Step 16 migration: the backpressure fixture authored
512 physical bodies while the accepted live solver admits 256. It could not start. The migrated
fixture uses 256 and preserves every transport condition and assertion, including 60 Hz, a
1,024-byte unread receive buffer, twelve seconds, mandatory actual 1013 close, healthy-client
sequence/tick progress, readiness, and clean shutdown. The GCC run observed 721 healthy frames,
14 fully validated samples, six readiness observations, and one correlated slow-consumer close.
The reduced payload volume is disclosed; it is not identical historical offered load or a native
simulation-capacity certification.

Three initial terrain cases passed; the one-seat hill immediately ended under attrition. Adding
a second stationary real browser and safe spawn corrected the fixture. Review then strengthened
painted-center motion and fresh tick boundaries for repeated-input observations. All six combat
cases and the initial tuning case passed on their first targeted run.

The first complete browser gate passed 23/24 and exposed a race-test observation gap: the exact
transition-to-running tick legitimately precedes RaceProgress initialization on the next lifecycle
pass. The supported-finish case now waits for published initialization, then still requires zero
gates before charging. It does not coerce missing progress to zero or change the finish oracle.

Playwright nests the new fixture groups in its report. The gate's prior file-level-only traversal
would omit those results and fail its total comparison. Recursive traversal now applies the same
count, status, retry, skip, and flake rejections to every declared test.

The first full web run crashed in the host emulator. `file frontend-react/core` identified an ARM
aarch64 core from `/mnt/lima-rosetta/rosetta /usr/local/bin/node ...`, not an x86 server core.
Root retained it at `/tmp/blob-step23-rosetta.core` and launched a fresh pinned-container gate.
No test assertion, worker setting, retry policy, dependency pin, or production behavior changed
to bypass that host failure. This infrastructure interruption remains part of the evidence.

## Limits and continuation

20 Hz browser publications cannot expose every 400 Hz contact tick. Exact replays own that
chronology; broad browser shield/stun windows make states observable and are not balance tuning.
Late shield means protected ordinary contact near the end of the active guard, not an expired
shield. Actual bot charge, knockback, fall, shield, and parry are observed at drag 2; Keeper's
arrival brake is deliberately absent from those fixtures.

The independent [release review](2026-09-12-release-review.md) found a real contradiction in that
brake's no-overshoot claim. The owner selected current controls with an explicit acceptable-overshoot contract; its production-loop verification belongs to Step 24.
Native release/performance evidence also remains outstanding. Step 24 is not certified by this
acceptance checkpoint. The [human balance agenda](2026-09-12-combat-balance-playtest-agenda.md)
is separate; no push, deployment, or human playtest was performed.

Transient logs are `/tmp/blob-step23-*.log`. The committed evidence artifact records the final
input hashes and gate results; local handoff/prompt files are excluded from all commits.
