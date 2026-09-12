# Step 15 tactical-profile preflight

Status: read-only preflight findings, not implementation approval or verification evidence.
Scope: plan `.claude/plans/2026-09-10-dynamic-arenas-and-combat.md`, Step 15, and
ADR 0008 § “Tactical personalities without a class per mood”. No Phase C work is authorized here.
Step 14's shared stun/input-lock and input-generation/echo contract is a supplied dependency,
not a design reopened by this review. No builds or tests were run for this preflight.

## Owner scope resolved (2026-09-11)

The owner explicitly selected **objective-seeking, reaction timing, aim error, and target
persistence now**, with aggression/charge/shield settings deferred until their combat behavior
exists. This resolves the earlier scope question; the engineering prerequisites below still
need a concrete implementation contract and verification.

ADR 0008:416 lists parameter categories, but gives no concrete Step 15 fields, bounds, defaults,
candidate ranking, or no-candidate behavior. Step 22 owns the combat choices and named
Keeper/Bully/Opportunist/Cautious Racer profiles. Before source release, settle the active subset
and its actual behavior; do not accept silently unused combat knobs as though implemented.

Also settle:

- Profile-bearing startup roster syntax. Existing `kind:count` terms and duplicate-kind rejection
  live in `src/application/match_configuration.cpp:79`; multiple tactical profiles need an explicit
  identity and duplicate rule without reinterpreting legacy terms.
- Whether the welcome's existing kind list is replaced or extended, exact profile/count budgets,
  and whether a tactical kind without configured profiles is advertised as selectable.
- Tactical behavior before the first running phase, on countdown cancellation, on unsupported
  mode state, and on repeated/stale observations. Idempotence must specify returned commands as
  well as unchanged internal state; duplicate observations must not reissue activation pulses.
- Sandbox: it has no seats and uses `Room::seat_configured_bots`
  (`src/application/room.cpp:35`). Either reject tactical rosters there explicitly or approve a
  stable authored roster-slot identity; ControllerId/allocation order is not silently a seat.

## Required safeguards if Step 15 proceeds

1. **Preserve diagnostic bots exactly.** The room seed is `match_seed + (lobby_id - 1)`
   (`src/application/room.cpp:16`); both startup construction there at line 45 and
   `src/application/bot_reconciliation.cpp:110` pass it unchanged. Preserve existing seeds,
   personalities, draws, fixtures, and repeated-observation behavior. Wanderer explicitly advances
   per call (`src/controllers/wanderer_controller.cpp:48`), while HillSeeker draws twice even at
   zero jitter (`hill_seeker_controller.cpp:130`). Existing repeated-snapshot tests pin this
   (`tests/unit/controllers/wanderer_controller_tests.cpp:111`,
   `hill_seeker_controller_tests.cpp:199`). New identity derivation and idempotence are tactical-only,
   not changes to `Controller` or `ControllerHost` that alter old kinds.
2. **Preserve complete declaration identity.** Carry profile identity through configuration,
   `SeatNpcCommand`, `NpcSeat`, factory, reconciliation, welcome admission, and published seats.
   NPC reconstruction on declaration/join/leave currently copies only kind/controller
   (`src/simulation/game_simulation.cpp`, `apply_lobby_command`, `apply_join`, `apply_leave`).
   Reconciliation's ownership check and failed-creation cache currently omit profile
   (`src/application/bot_reconciliation.cpp:20,83,129`). A profile change must not reuse a failed or
   hosted bot for another declaration; preserve first-wins seating and in-flight join semantics.
3. **Make round identity explicit without reseating.** Hosted bots remain while their seats retain
   their controllers. `MatchState::running_started_tick` is set on entry to running and retains its
   previous value in lobby/countdown (`src/simulation/match_state.hpp:67`,
   `match_lifecycle_system.cpp:79`). There is no round counter. If this public tick is the selected
   identity, reseed tactical state once per newly observed start, not by closing/rejoining sessions
   and changing controller/entity allocation order.
4. **Keep one admitted catalogue and public-only decisions.** Today welcome, decoder, and menu share
   the kind vocabulary (`src/protocol/session_welcome.hpp:37`, `command_decoding.cpp:180`,
   `frontend-react/src/features/simulation/LobbyPanel.tsx:256`). Extend that same authority for
   profiles; reject missing/unknown/mismatched selections without fallback. Controllers must retain
   their simulation/runtime-only dependency (`src/controllers/CMakeLists.txt:19`).

## Proposed implementation choices, not decisions

- Controllers own immutable validated `TacticalProfile` values/catalogue. Simulation owns only a
  bounded profile-name value where a seat/command must retain it. Extend the existing strict
  `[family.name]` parser (`src/application/application_config_loader.cpp:156`) with
  `[bot_profile.<name>]`; every declared profile requires its complete active field set. Zero
  profiles can remain valid for unchanged legacy configurations.
- Extend the registry factory context once. A seatable option could be
  `{npc_kind, profile: string|null}`: tactical requires a known profile; diagnostic kinds use null.
  Keep catalogue membership, order, and budget ownership singular rather than authoring two lists.
- Use the existing mode-state schema IDs for a closed objective-provider table producing one
  bounded candidate shape (`src/simulation/mode_match_state_registry.hpp`). Reuse canonical terrain
  queries; Step 15 must not imply Phase C motion, shove, charge, or shield prediction exists.
- Derive tactical seeds with specified unsigned tuple/ASCII-name encoding and the existing
  `DeterministicRandom::mix_bits`, never `std::hash`, clocks, allocation order, or simulation RNG
  draws. Fix the room/seat/profile/round tuple order. A 64-bit hash does not establish universal
  collision freedom; a literal distinct-seed requirement needs a bounded guarantee or explicit
  collision policy.

## Pure-move proof before reader cutover

Freeze independent full old Chaser/HillSeeker references and validate actual old command sequences
against them; retain the existing full Racer oracle
(`tests/unit/controllers/racer_projection_promotion_tests.cpp`) and independent frozen RNG. Prove
unused promoted helpers against those references on both required lanes before switching readers;
retain the proof unchanged afterward. Compare command count/kind/address/component bits, targets,
draw counts, bodyless/spawn retry, no-target/coincident behavior, duplicate observations, ordered
ties, personality extremes, and racer caution/finish behavior.

Arithmetic traps: Chaser computes `delta * (weight / magnitude)` while Racer computes
`delta / magnitude` (`src/controllers/chaser_controller.cpp:137`, `racer_controller.cpp:96`).
HillSeeker uses `max(distance, radius)`, approach scaling, two ordered jitter draws, then clamping
(`hill_seeker_controller.cpp:119`). Preserve written operations and strict ties, not algebraic
equivalents. Keep coordinate differences as raw doubles: constructing a `Vector2` delta can tighten
the old signed-extreme domain. Do not replace `players()` with all bodies; it is the published
controllable/body join.

## Likely independent ownership

- Controllers: profile value, seed derivation, pure promotions/proofs, provider pipeline, registry.
- Application: strict authoring/roster grammar, startup validation, Room and reconciliation.
- Simulation/runtime/protocol: profile-name storage and seat preservation, catalogue capability,
  encoder/decoder, schemas/examples and budget checks.
- Frontend: catalogue validation/retention, selection and seat labels, room/reconnect tests.

Next action: amend Step 15's implementation/proof contract for the approved subset before release.
Root owns that amendment, manifests, generation, and serial verification.
