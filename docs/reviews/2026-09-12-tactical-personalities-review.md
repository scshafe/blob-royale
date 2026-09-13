# Step 22c verification review

> **Superseded braking claim (2026-09-12):** The no-overshoot/all-drag stability and
> deadbeat trajectory claims below were disproved by release review. They remain
> here as historical reasoning. The owner chose current controls with accepted
> velocity reversal; [the current contract](2026-09-12-arrival-brake-contract.md)
> defines the conditional bound and its first-observation/timing limitations.
> The old single-command unit assertions did not prove a held trajectory.


The four named tactical personalities. Parent checkpoint `ee763c7`. Implementation contract:
[`2026-09-12-tactical-personalities-contract.md`](2026-09-12-tactical-personalities-contract.md).

**Every number below is Mac/arm64-hosted Docker Linux/amd64 advisory.**

## Completed checks

| Gate | Result |
|---|---|
| `verify-focused 'unit.controllers\|unit.gameplay\|unit.application\|fixtures'` (GCC) | 810 / 810 |
| the same filter on `linux-clang-asan-ubsan` | 810 / 810 |
| `verify-fuzz-regressions` | 99 executions over seven harnesses |
| `run-linux-toolchain -- verify-web` | 986 / 986 |
| `run-linux-toolchain -- verify-browser-e2e` | Chromium 13 / 13, 0 retries, 0 skips, 0 flakes |
| `clang-format --dry-run --Werror` over 17 changed C++ paths | clean |
| `git diff --check` | clean |

810 from Step 22b's 799. Both C++ lanes and the browser gate were re-run after `clang-format -i`
touched source, so the verified tree is the committed tree.

## The two headline proofs

* **`four authored personalities decide four different objectives on one identical observation`** —
  passing on both lanes. The profile *name* is held constant across all four, so `tactical_seed_for`
  cannot manufacture the difference; only the settings vary. This is the differentiation claim the
  plan has asked for since Step 15, and it could not have been made before Step 22b, because until
  an opponent-derived provider existed every mode produced exactly one candidate and selection never
  chose.
* **`the shipped configuration parses and carries exactly the five authored profiles`** — a new case
  in the `fixtures` lane, and the first time anything in this repository has parsed
  `config/blob-royale.cfg`.

## The gate hole this step closed

**Nothing in the tree parsed the shipped configuration.** `deployment_fixture_tests.cpp` loads
`BLOB_ROYALE_DEPLOYMENT_FIXTURE_DIRECTORY`, which is `deploy/ubuntu-pc`; the shipped file was
referenced only by `scripts/verify-linux` — Step 24's line — and by `assemble-release-linux`. So
Step 22b's three new `[bot_profile.steady]` keys, and the `steady` section since Step 15, shipped
with zero automated parse coverage, and the **Step 22b contract asserted the opposite**.

Root verified by hand after Step 22b that the shipped configuration loads and the server reaches
`running` with `lobby_count=1`, so nothing was ever broken — but that was a one-off, not a gate. This
step adds `BLOB_ROYALE_SHIPPED_CONFIGURATION_FIXTURE_DIRECTORY` and a case that loads the file
through `ApplicationConfigLoader`, and the false sentence in the 22b contract is corrected in place
with a dated note. The same false claim was in `src/controllers/README.md`; that is corrected too.

Because this is now the only lane that sees the shipped vectors, the case also asserts the two rules
no bound can enforce: **recovery weight is never below gate weight** for any profile, and **no
profile authors a zero weight on a kind its running mode produces**.

## The veto collapsed, and Step 22b's review was wrong about why

Root drafted a general zero-weight veto with an all-vetoed branch, a reason code and a fallback. The
preflight cancelled it and corrected the record:

* **There was never a crash.** `tactical_select_candidate` initialises `best` to `candidates.size()`
  and its loop's first test short-circuits, so on any non-empty set the first iteration assigns index
  0. The sentinel is unreachable. The defect was a *contract* permitting a value its one caller does
  not handle.
* **Step 22b's review called the hypothetical read "a container-overflow abort on the sanitizer
  lane". That was wrong, and the truth is worse.** The candidate vector is `reserve`d at the raw
  merged count and filled only with screen survivors, so whenever anything is screened out
  `capacity > size` and the read lands *inside* the live allocation with no sanitizer report — after
  which the lease copies a candidate whose `key.kind` is an arbitrary byte and the next pass indexes
  a five-element array with it. **Silent corruption, not an abort.** That review and the plan's
  Step 22b bullet are both corrected here.

What landed instead is two smaller things: `tactical_select_candidate` returns
`std::optional<std::size_t>`, so the compiler closes the hole at every future call site; and a zero
`objective_weight_shove_setup` **skips the shove provider** for that profile — a provider-level skip,
needing no reason code, no fallback branch, and no change to what a zero weight means for the four
mode kinds. Keeper is the one profile that authors it, and it is the honest spelling of "does not
hunt".

## The brake

Deadbeat and division-free:
`clamp_componentwise(-v_relative / (published_acceleration * hold_seconds) * arrival_brake_fraction)`.

* **No division by `|v|`, so no 0/0 and no NaN and no deadband.** The velocity *vector* is divided
  by a positive scalar componentwise, so the direction goes smoothly to `(0,0)` at rest. This
  matters more than it reads: a NaN reaching `Vector2::create` throws, `ControllerHost` catches, and
  `TacticalController` rolls state back without advancing `last_completed_tick` — so it repeats every
  pass and **the bot is permanently inert**, in the terminal state of every successful hill capture.
* **It cannot overshoot, so it is stable at every drag without reading drag.** It asks for exactly
  the thrust that nulls the relative velocity over one hold; drag only removes more, so a nonzero
  drag makes it undershoot, and undershoot self-corrects.
* Root's drafted `min(1, |v_rel| / normal_top_speed)` was rejected because it calibrates against an
  unreachable speed: at the e2e fixtures' published ceiling of 10000 with acceleration 400 and drag
  40 the terminal speed is 9 wu/s, so that law yields a brake three orders of magnitude too small —
  inert in exactly the configuration whose instability motivated it.
* **kHill only**, re-justified: a `kZone` candidate's arrival radius is the zone's own radius and a
  full zone is the arena half-diagonal, so a zone brake would be a permanent parking brake on every
  bot in the checked-in default configuration, which authors `mode=royale`.

## Decisions worth recording

* **The exposure quality is booleans, never a ratio.** Every ratio spelling divides by a legally-zero
  denominator, and two of those denominators are reachable only through a `mode_state` variant access
  that throws in the wrong mode — the permanently-inert failure again. `ZoneExposure` and
  `HillPresence` are erasure-based presence flags, so they are already booleans.
* **It folds into preference multiplicatively**, `weight * proximity * opening`, never additively. An
  additive term would break the commensurability the limits file and the scoring comment both rest
  on, and a maximally cautious profile could be made to prefer a cliff-blocked shove over a clean
  gate. The candidate field defaults to `{1.0}` — a `{}` would multiply every mode candidate's
  preference by zero, and the compiler cannot catch that one.
* **`road_caution_fraction` is strictly positive**, alone among this family's fractions, because zero
  *inverts* the recovery test rather than disabling it. It adopts `RacerController`'s existing pair
  rather than declaring a second domain, and strict positivity turns every missed positional
  construction site into a loud `create` throw instead of a silently inverted racer.
* **Cautious Racer authors a non-zero shove weight** (0.125). Its clause is "avoid *expensive*
  fights", not "never fight", and `minimum_opening` is what makes a fight cheap. Using the weight as
  a veto would have expressed a clause the ADR does not contain — and, because a charge is only ever
  aimed at a selected shove candidate, would have made "use bursts when aligned" unreachable for the
  one profile whose clause names it.

## Two contract divergences the workers reported rather than hid

* **The exposure quality has five terms, not four, and none of them is free.** The contract said only
  the stun term was free and the other three cost a scan. There is no free route to an opponent's
  stun, and the fifth component (`HillPresence`) was in the plan's list but not the contract's
  sentence. The header records five scans per kept opponent honestly. ADR 0008's amendment was
  written from the contract's four and has been corrected to the shipped five.
* **`minimum_opening` compares against the folded opening, not the raw exposure.** One quantity, one
  name, one domain. The consequence, worth stating: a profile with `exposure_preference = 0` cannot
  author an effective `minimum_opening`, because it has declared every fight equally valuable. Both
  vectors that need the filter author both.

## Named but not built

Recorded in ADR 0008 as deferred rather than amended away, on the line that a clause the boundary
*forbids* may be amended while a clause that is merely unbuilt may not:

* **Keeper's "defend a stable interior".** A hill candidate's arrival radius is the *full* published
  radius, so a bot reports arrived one world unit inside the rim, where it is trivially shoved out —
  and the brake makes that worse by stopping it there. The value-keyed fix is an
  `arrival_radius_fraction`; it is buildable from published state and is named as owed.
* **Bully's "acceptable recovery path".** The charge screen asks whether there is ground under the
  corridor, and its own header says it is never a claim the body can stop before leaving it.
* **Nothing makes a Bully shove with ordinary thrust.** The only push is the charge, so a Bully on
  cooldown stands at its standing point doing nothing. Stated because it reads as a bug.

## Execution corrections

**One assertion moved by design, not by weakening.** In the Step 22b differentiation case,
`screened_candidate_count` went 2 → 1, because a zero shove weight now skips the provider instead of
producing an unpreferred candidate. `raw_candidate_count` assertions were added on both bots and the
racer still selects the hill.

**`tactical_select_candidate({}, ...) == 0` could not survive the type change** and became
`CHECK_FALSE(...has_value())` plus a positive `has_value()` on a non-empty set — strictly stronger
than the sentinel form it replaced.

**Root owned three things no worker could.** The `tests/fixtures/CMakeLists.txt` define; ADR 0008's
amendment and its correction from four exposure booleans to five; and the two "sanitizer abort"
corrections in the plan and the Step 22b review.

## Exact acceptance commands

```
./scripts/verify-focused 'unit.controllers|unit.gameplay|unit.application|fixtures'
./scripts/verify-focused 'unit.controllers|unit.gameplay|unit.application|fixtures' linux-clang-asan-ubsan
./scripts/verify-fuzz-regressions
./scripts/run-linux-toolchain -- ./scripts/verify-web
./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e
git diff --check
```

Transient logs are `/tmp/s22c-*.log` and are not committed.
