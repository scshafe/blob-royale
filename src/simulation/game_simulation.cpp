#include "game_simulation.hpp"

#include "candidate_pair.hpp"
#include "command_registry.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "contact_effect_admission.hpp"
#include "contact_rule.hpp"
#include "continuous_motion.hpp"
#include "idle_match_objective.hpp"
#include "idle_spawn_policy.hpp"
#include "match_lifecycle_system.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "motion_body_envelope.hpp"
#include "physics.hpp"
#include "physics_body.hpp"
#include "seat_roster.hpp"
#include "simulation_limits.hpp"
#include "simulation_tolerance.hpp"
#include "simulation_validation_error.hpp"
#include "tick_context.hpp"
#include "world_event_registry.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::simulation {
namespace {

using BodyEntry = ComponentStore<PhysicsBody>::Entry;

// The canonical solver receives only immutable capabilities and returns all consequences as values.
PairMotionResponse<WorldEvent>
respond_live_pair(const GameWorld& world, const ContactRule::Subject& first,
                  const ContactRule::Subject& second, const PairContactObservation& observation,
                  const TickContext& context, const LiveMotionFacts& facts) {
  return facts.require_contacts().respond(world, first, second, observation, context);
}

// Phase 0. The batch arrives canonical -- despawns, then spawns, then every remaining kind, each
// group ascending by the identity it addresses -- so this is one forward pass that neither sorts,
// de-duplicates, nor range-checks (`input_batch.hpp`).
//
// A despawn destroys its entity before any pair is built. **A spawn draws an EntityId from this
// tick's reservation and writes the Controllable that links it to the asking controller**, which
// brings the entity into existence carrying no body: it is *unseated*, and the SpawnSystem that
// runs immediately after this pass offers it to the mode's SpawnPolicy. Drawing from the tick's
// reservation is what makes a replayed command log reproduce simulation-created ids exactly, and
// exhausting the reservation is a hard failure rather than a silent skip
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick").
//
// A command of any remaining kind is recorded into its entity's Controllable and is **not
// interpreted**, because command meaning is a system's job
// (`docs/architecture/0004-gameplay-architecture.md` § "Commands").
//
// **The exception, stated so it is not mistaken for a violation: a command whose meaning is the
// engine's own is applied by the engine, here.** That was already true of the two kinds above -- a
// despawn destroys an entity and a spawn creates one, and neither is recorded for a system to
// interpret -- and it is true of the four lobby kinds for the same reason: they write `MatchState`,
// which is engine-owned state gating an engine-owned transition. The rule the ADR is stating is
// that a *mode's* meaning is a mode's system, which is exactly why `thrust` is recorded rather than
// applied: normal propulsion is interpreted by the shared gameplay steering system, using the
// room's movement tuning. The tuning command itself is another engine-owned exception: it updates
// shared MatchState here and returns commit-only decisions. Nothing about a seat depends on the
// mode; a mode reads the roster through its objective and never writes one. A
// `leave` is the engine's own for the reason a despawn is: it removes what a departed controller
// drove and vacates its seat, and nothing about that depends on the mode either.
//
// A command that disagrees with committed world state -- a despawn for an entity that does not
// exist, a command recorded for an entity that is not live -- is ignored rather than failing the
// tick, because the command source is a network session and a hard failure would let one client
// stop the match (`docs/architecture/0003-deterministic-simulation-contract.md`
// § "Accepted simulation input").
// canonical: lobby_command_application -- phase 0's arm for the four commands that operate a lobby.
//
// **Applied here, at phase 0, in the batch's existing order, rather than in a system.** Everything
// determinism needs is already true of this pass -- the batch is canonical, the order is the one
// `docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick" fixes, and a
// replay reproduces it -- so a lobby that is operated here inherits replay and determinism instead
// of reinventing them. A lobby system would have had to be ordered against the lifecycle system,
// and getting that order wrong is a match that starts a tick late for reasons nobody can see.
//
// **Every one of them is refused outside `lobby`, and the guard is one line for all four.** A seat
// roster is only meaningful before a match: resizing the field mid-match, or seating a bot into a
// running game, are changes the arena has no way to represent. Refusing is silent because the
// alternative is loud in the wrong direction -- see the ignoring rule below.
//
// **Anyone may send any of them. That is a deliberate trust choice, and this is the comment that
// says so.** The owner chose it on 2026-09-08: no host, no ready check, no per-seat ownership. It
// is defensible because of what this deployment is -- a private tailnet where every peer is an
// identified person the owner invited, and where the worst outcome of a mis-click is a match that
// starts early or a bot that has to be re-seated. **What would have to change for a public
// deployment** is not this pass but the identity that reaches it: a lobby command would need an
// authority attached to its `ControllerId` -- host, or seat-owner, or first-joiner -- decided
// *above* the simulation and carried in as part of the command, because the tick knows nothing
// about people and must not start. Concretely: `MatchSessionContext` would grow that authority,
// `CommandSink::submit` would refuse a lobby command from a session that does not hold it, and this
// pass would be unchanged. Nothing here is load-bearing for that change, which is the property that
// makes shipping the trusting version now honest rather than lazy.
//
// **A command that disagrees with committed state is ignored, never fatal**, exactly as a despawn
// for an entity that does not exist is: the source is a network session whose view of the roster
// lags the world by a frame, and a hard failure would let one client stop the match. There are five
// such disagreements and each is a no-op -- the match is not in `lobby`, the seat index names no
// seat in this roster, the seat a `seat_npc` names is already occupied, a `set_seat_count` would
// shrink past somebody sitting down (`seat_roster.hpp`, `try_set_seat_count`), and a
// `set_seat_count` would grow past `seat_ceiling`.
//
// **`seat_ceiling` is the map's spawn-marker count**, handed in by the kernel from the map it was
// constructed with. A seat with no marker behind it is a player the field could never seat, which
// is exactly what `RoyaleMode::validate_map` refuses for the *configured* count at startup; the
// command that resizes a lobby at run time is held to the same bound here, in the one place that
// has both the roster and the map. It is a no-op rather than a rejection because the client cannot
// know the bound -- no frame publishes a marker count -- and the taxonomy's line is whether it
// could have (`docs/protocol/v2.md` § "The lobby commands"). It is not a `MatchState` member: the
// ceiling is a fact about the map, and a member would grow every snapshot for a number the map
// already owns.
//
// Returns true when the command was a lobby kind, whether or not it changed anything, so the caller
// stops considering it.
[[nodiscard]] bool apply_lobby_command(GameWorld& world, const Command& command,
                                       const std::size_t seat_ceiling) {
  const bool is_lobby_command = std::holds_alternative<SetSeatCountCommand>(command) ||
                                std::holds_alternative<ClearSeatCommand>(command) ||
                                std::holds_alternative<SeatNpcCommand>(command) ||
                                std::holds_alternative<StartMatchCommand>(command);
  if (!is_lobby_command) {
    return false;
  }
  MatchState& match = world.mutable_match();
  if (match.phase != MatchPhase::kLobby) {
    return true;
  }

  if (const auto* set_seat_count = std::get_if<SetSeatCountCommand>(&command);
      set_seat_count != nullptr) {
    // The range is `InputBatch::create`'s, checked before this batch existed, so the cast is on a
    // value already known to fit the roster's own bound. The map's tighter bound is this pass's
    // to apply, and above it nothing changes.
    const auto requested = static_cast<std::size_t>(set_seat_count->seat_count);
    if (requested > seat_ceiling) {
      return true;
    }
    static_cast<void>(match.seats.try_set_seat_count(requested));
    return true;
  }
  if (const auto* clear_seat = std::get_if<ClearSeatCommand>(&command); clear_seat != nullptr) {
    const std::size_t index = static_cast<std::size_t>(clear_seat->seat_index);
    if (index >= match.seats.seat_count()) {
      return true;
    }
    // Only a declared NPC is cleared. A seat a live controller holds is the runtime's to give and
    // take, and a tick that emptied one would be contradicted by the next reconciliation
    // (`commands/clear_seat_command.hpp`).
    if (std::holds_alternative<NpcSeat>(match.seats.seats()[index])) {
      match.seats.assign_seat(index, Seat{EmptySeat{}});
    }
    return true;
  }
  if (const auto* seat_npc = std::get_if<SeatNpcCommand>(&command); seat_npc != nullptr) {
    const std::size_t index = static_cast<std::size_t>(seat_npc->seat_index);
    if (index >= match.seats.seat_count()) {
      return true;
    }
    // First-wins: an occupied seat is left alone, so two clients seating one seat in one tick
    // resolve by this batch's order and the second press is a no-op rather than an eviction.
    if (!seat_is_occupied(match.seats.seats()[index])) {
      // The controller is absent because no bot exists yet. The runtime that creates one writes it
      // back; until it does, the seat is a declaration a client renders as joining
      // (`seat_roster.hpp`, NpcSeat).
      match.seats.assign_seat(index,
                              Seat{NpcSeat{seat_npc->kind, std::nullopt, seat_npc->profile_name}});
    }
    return true;
  }
  // `start_match`. It records a request and commits nothing: the transition is
  // `MatchLifecycleSystem`'s, at `kLifecycle`, and it happens only if the mode's objective also
  // says the field is complete (`commands/start_match_command.hpp`).
  match.seats.request_start();
  return true;
}

// True when some live entity's Controllable already names this controller. A controller drives at
// most one body at a time, so a spawn for one that already has a body is a duplicate.
[[nodiscard]] bool controller_holds_a_body(const GameWorld& world, const ControllerId controller) {
  for (const auto& entry : world.store<Controllable>().entries()) {
    if (entry.value.controller_id == controller) {
      return true;
    }
  }
  return false;
}

// Whether some seat already belongs to this controller, as a person or as a created bot.
[[nodiscard]] bool controller_holds_a_seat(const SeatRoster& seats, const ControllerId controller) {
  for (const Seat& seat : seats.seats()) {
    if (const auto* held = std::get_if<ControllerSeat>(&seat);
        held != nullptr && held->controller == controller) {
      return true;
    }
    if (const auto* declared = std::get_if<NpcSeat>(&seat);
        declared != nullptr && declared->controller == controller) {
      return true;
    }
  }
  return false;
}

// Phase 0's arm for a controller that wants a seat (`commands/join_command.hpp`).
//
// A bot's join names the seat that declared it and fills exactly that seat, only while it still
// declares a kind and has no controller; anything else -- cleared, resized away, already filled --
// changes nothing, and the reconciliation that created the bot retires it. A person's join names no
// seat: the lowest empty one, or, before a match has started, the lowest-indexed NPC seat, which
// the person takes from its bot. Displacement stops at `countdown` because a match in `running` or
// `ended` is a closed field: taking a bot's seat there would retire a bot that is playing.
//
// A controller already seated is left exactly where it is, so a session that keeps asking never
// moves, and a join that finds nothing to take is the no-op that lets it ask again.
void apply_join(GameWorld& world, const ControllerId controller,
                const std::optional<std::uint64_t> seat_index,
                const std::optional<NpcDeclaration>& expected_npc) {
  SeatRoster& seats = world.mutable_match().seats;
  if (controller_holds_a_seat(seats, controller)) {
    return;
  }
  if (seat_index.has_value()) {
    if (*seat_index >= seats.seat_count()) {
      return;
    }
    const auto index = static_cast<std::size_t>(*seat_index);
    const auto* declared = std::get_if<NpcSeat>(&seats.seats()[index]);
    if (declared == nullptr || declared->controller.has_value()) {
      return;
    }
    if (expected_npc.has_value() && declared->declaration() != *expected_npc) {
      return;
    }
    NpcSeat joined = *declared;
    joined.controller = controller;
    seats.assign_seat(index, Seat{joined});
    return;
  }
  // The lowest empty seat, else a declared bot's before the match starts, else nothing: the rule
  // is `first_joinable_seat`'s, shared with the session that decides whether to ask at all.
  const std::optional<std::size_t> index = first_joinable_seat(seats, world.match().phase);
  if (index.has_value()) {
    seats.assign_seat(*index, Seat{ControllerSeat{controller}});
  }
}

// Phase 0's arm for a departed controller. Every entity whose `Controllable` names it is destroyed,
// pending or seated, and any seat it holds is vacated: a person's seat empties, an NPC seat keeps
// its declared kind and loses its bot, so the reconciliation that created the bot can create
// another. Both walks are ascending by construction, and a controller that drives nothing and sits
// nowhere is a no-op -- which is what makes a repeated close harmless and a replayed leave for a
// never-seated controller legal (`commands/leave_command.hpp`).
void apply_leave(GameWorld& world, const ControllerId controller) {
  std::vector<EntityId> driven;
  for (const auto& entry : world.store<Controllable>().entries()) {
    if (entry.value.controller_id == controller) {
      driven.push_back(entry.entity);
    }
  }
  for (const EntityId entity : driven) {
    world.destroy_entity(entity);
  }

  SeatRoster& seats = world.mutable_match().seats;
  for (std::size_t index = 0; index < seats.seat_count(); ++index) {
    const Seat& seat = seats.seats()[index];
    if (const auto* held = std::get_if<ControllerSeat>(&seat);
        held != nullptr && held->controller == controller) {
      seats.assign_seat(index, Seat{EmptySeat{}});
      continue;
    }
    if (const auto* declared = std::get_if<NpcSeat>(&seat);
        declared != nullptr && declared->controller == controller) {
      NpcSeat vacated = *declared;
      vacated.controller.reset();
      seats.assign_seat(index, Seat{vacated});
    }
  }
}

void apply_input_batch(GameWorld& world, const InputBatch& input_batch,
                       const std::size_t seat_ceiling, const TickSequence decision_tick,
                       std::vector<MovementTuningDecision>& tuning_decisions) {
  const std::uint64_t entry_revision = world.match().movement.revision;
  const SetMovementTuningCommand* winner = nullptr;
  std::optional<std::size_t> winner_index;
  // Last tick's recorded commands are cleared in place rather than by reconstructing the
  // component, so each entity's vector keeps its capacity across ticks.
  for (Controllable& controllable : world.mutable_store<Controllable>().mutable_values()) {
    controllable.commands_this_tick.clear();
  }
  if (input_batch.commands().empty()) {
    return;
  }

  for (const Command& command : input_batch.commands()) {
    if (const auto* despawn = std::get_if<DespawnCommand>(&command); despawn != nullptr) {
      world.destroy_entity(despawn->entity);
      continue;
    }
    if (const auto* spawn = std::get_if<SpawnCommand>(&command); spawn != nullptr) {
      // A controller drives at most one body. `InputBatch` collapses repeated spawns *within* one
      // tick, but nothing stopped a source from spawning again in a later tick, so a session or a
      // bot that asked twice got two blobs -- steering one and abandoning the other in the arena.
      // Ignored rather than rejected, exactly as ADR 0005 § "Roster edge rules" says a duplicate
      // spawn is, because the source is an untrusted session and a hard failure would let one
      // client stop the match.
      if (controller_holds_a_body(world, spawn->controller)) {
        continue;
      }
      const EntityId created = world.create_entity();
      world.mutable_store<Controllable>().insert_or_assign(created,
                                                           Controllable{spawn->controller});
      continue;
    }
    if (const auto* tuning = std::get_if<SetMovementTuningCommand>(&command); tuning != nullptr) {
      auto status = MovementTuningDecisionStatus::kSuperseded;
      if (!controller_holds_a_seat(world.match().seats, tuning->controller)) {
        status = MovementTuningDecisionStatus::kNotSeated;
      } else if (tuning->expected_revision != entry_revision) {
        status = MovementTuningDecisionStatus::kStaleRevision;
      } else if (entry_revision >= kMaximumProtocolSafeInteger) {
        status = MovementTuningDecisionStatus::kRevisionExhausted;
      } else {
        winner = tuning;
        winner_index = tuning_decisions.size();
      }
      tuning_decisions.push_back(MovementTuningDecision{
          tuning->controller, tuning->tuning_request_id, status, decision_tick, entry_revision});
      continue;
    }
    if (apply_lobby_command(world, command, seat_ceiling)) {
      continue;
    }
    if (const auto* join = std::get_if<JoinCommand>(&command); join != nullptr) {
      apply_join(world, join->controller, join->seat_index, join->expected_npc);
      continue;
    }
    if (const auto* leave = std::get_if<LeaveCommand>(&command); leave != nullptr) {
      apply_leave(world, leave->controller);
      continue;
    }
    // Total over the closed variant: every kind that addresses no entity is handled above, so this
    // guard is unreachable today and is what keeps the pass correct the day a kind that addresses
    // something other than an EntityId is registered (`command_registry.hpp`, AddressedIdentity).
    const std::optional<EntityId> recorded_entity = addressed_identity_of(command).entity();
    if (!recorded_entity.has_value()) {
      continue;
    }
    if (Controllable* controllable =
            world.mutable_store<Controllable>().mutable_find(*recorded_entity);
        controllable != nullptr) {
      // Appended in batch order, and that is the whole ordering rule. The batch already arrives in
      // phase 0's application order, so **one canonical order governs one command list** and this
      // pass neither sorts nor regroups. A second convention -- re-sorting each entity's recorded
      // list by ascending CommandKind -- used to live here; it was a no-op over the rank table it
      // claimed to be independent of, and two orderings over one closed vocabulary is a
      // disagreement waiting for a fourth kind (engine review finding 6).
      controllable->commands_this_tick.push_back(command);
    }
  }
  // Every contender compared against entry_revision. Later joins cannot grant retroactive
  // authority, and a later Leave cannot undo an already admitted contender. Apply once, before
  // any pre-kernel system or the lifecycle evaluation of Start sees the working match.
  if (winner != nullptr) {
    auto& movement = world.mutable_match().movement;
    movement.current = winner->tuning;
    movement.revision = entry_revision + 1;
    movement.effective_tick = decision_tick;
    tuning_decisions[*winner_index].status = MovementTuningDecisionStatus::kApplied;
  }
  for (auto& decision : tuning_decisions) {
    decision.revision = world.match().movement.revision;
  }
}

void apply_stage(const SystemPipeline& system_pipeline, const SystemStage stage, GameWorld& world,
                 const TickContext& context) {
  for (const SystemPipeline::StagedSystem& staged : system_pipeline.systems_at(stage)) {
    staged.system->apply(world, context);
  }
}

// Phase 1. Semi-implicit Euler on the stored acceleration, then the drag factor
// `max(0, 1 - drag_per_second * drag_scale * dt)` on the accelerated velocity, both in ascending
// EntityId order because a ComponentStore's entries are ascending by construction. At
// `drag_per_second = 0` the factor is exactly 1.0 and multiplication by 1.0 is the identity on
// every finite binary64 value, so this reproduces the accepted baseline bit-for-bit.
//
// **The body's scale enters before the equation and changes nothing else about it.** The kernel
// still owns drag, still applies it here, and no system reproduces or bypasses it; it reads a
// coefficient from the body the way phase 3 already reads a mass
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick" phase 1, as
// amended 2026-09-07). The product is formed first and the rest of the operation order --
// `* dt`, then `1.0 -`, then `max(0, ...)`, then the componentwise scale -- is untouched, which is
// what makes this an addition rather than a versioned change: every body that exists carries
// `kDefaultDragScale`, multiplication by `1.0` is exact in binary64 for every finite value, so
// `drag_per_second * 1.0` **is** `drag_per_second` bit for bit and every committed velocity is the
// value it always was. A body declaring `0.0` coasts, which is what an object crossing the arena
// needs: the factor is geometric, so total travel under drag is exactly `speed / drag_per_second`
// and a hazard would otherwise stall a fraction of the way in.
//
// A static body is copied through untouched: a wall is never accelerated and never dragged, so
// neither equation is evaluated for it at all rather than being evaluated and happening to be an
// identity (`docs/architecture/0004-gameplay-architecture.md`
// § "Entities, components, and stores").
[[nodiscard]] std::vector<BodyEntry>
apply_stored_acceleration_and_drag(const GameWorld& world, const double drag_per_second,
                                   const FixedDelta fixed_delta) {
  const std::span<const BodyEntry> committed_bodies = world.store<PhysicsBody>().entries();
  std::vector<BodyEntry> accelerated_bodies;
  accelerated_bodies.reserve(committed_bodies.size());
  for (const BodyEntry& entry : committed_bodies) {
    const PhysicsBody& body = entry.value;
    if (body.is_static()) {
      accelerated_bodies.push_back(entry);
      continue;
    }
    const Vector2 accelerated_velocity =
        integrate_accelerated_velocity(body.velocity(), body.acceleration(), fixed_delta);
    const Vector2 dragged_velocity =
        apply_velocity_drag(accelerated_velocity, drag_per_second * body.drag_scale(), fixed_delta);
    accelerated_bodies.push_back(BodyEntry{entry.entity, body.with_velocity(dragged_velocity)});
  }
  return accelerated_bodies;
}

// All allocations and callbacks finish before the transaction publishes either bodies or events.
// Solver paths stay owned by this result through effect application; diagnostic events never
// enter the gameplay event list.
void apply_motion_result(GameWorld& world, const ContinuousMotionResult<WorldEvent>& result) {
  std::vector<BodyEntry> bodies;
  bodies.reserve(result.motion.bodies.size());
  for (const auto& body : result.motion.bodies) {
    bodies.push_back({body.entity, body.result.body});
  }
  auto replacement = ComponentStore<PhysicsBody>::create(std::move(bodies));
  for (const auto& effect : result.effects) {
    world.emit(effect.effect);
  }
  world.mutable_store<PhysicsBody>() = std::move(replacement);
}

// Startup and final survivors obey the same body envelope as the canonical solver. Initial
// wall-radius overlap is legal; this does not introduce depenetration or weaken spawn clearance.
void require_committed_bodies_in_bounds(const GameWorld& world, const MapDefinition& map,
                                        const SimulationConfig& configuration) {
  for (const BodyEntry& entry : world.store<PhysicsBody>().entries()) {
    const auto violation =
        motion_body_envelope_violation(entry.value, map.bounds(), configuration.player_radius());
    if (!violation) {
      continue;
    }
    throw SimulationValidationError(
        SimulationValidationCode::kGameSimulationBodyOutOfBounds,
        "game_simulation.commit.bodies[entity_id=" + std::to_string(entry.entity.value()) + "]",
        *violation == MotionBodyEnvelopeViolation::kStaticOutsideEnvelope
            ? "committed static body center must lie inside the closed arena rectangle"
            : "committed folding body must fit its motion envelope");
  }
}

// Phase 10 roster removal. Returns whether the roster changed, because the derived spatial index
// has to be rebuilt from the survivors exactly when it did.
[[nodiscard]] bool apply_despawn_events(GameWorld& world) {
  // The named ids are collected before any destruction so the walk never reads the event list
  // while the world it belongs to is being mutated.
  std::vector<EntityId> removed_entities;
  for (const WorldEvent& event : world.events()) {
    if (const auto* despawn = std::get_if<DespawnEvent>(&event);
        despawn != nullptr && world.contains(despawn->entity)) {
      removed_entities.push_back(despawn->entity);
    }
  }
  for (const EntityId entity : removed_entities) {
    world.destroy_entity(entity);
  }
  return !removed_entities.empty();
}

// canonical: indexed_body -- the part of a body the spatial index is a function of.
//
// Grid coverage depends on body position and effective radius, so both join identity in this
// snapshot. Comparing this and not the entity roster is what makes the rebuild decision
// correct: a stage may insert a body, destroy an entity directly rather than by emitting a
// DespawnEvent, or move one, and the first two change the roster while the third does not -- yet
// all three, and radius changes, invalidate the index. A stage that writes only velocity or stored
// acceleration, which is what a steering system does, leaves the index correct and pays no rebuild.
struct IndexedBody final {
  EntityId entity;
  Vector2 position;
  double radius;
};

[[nodiscard]] bool same_indexed_body(const IndexedBody& indexed, const BodyEntry& entry) noexcept {
  return indexed.entity == entry.entity && indexed.position == entry.value.position() &&
         indexed.radius == entry.value.radius();
}

[[nodiscard]] std::vector<IndexedBody> indexed_bodies_of(const GameWorld& world) {
  const std::span<const BodyEntry> bodies = world.store<PhysicsBody>().entries();
  std::vector<IndexedBody> indexed;
  indexed.reserve(bodies.size());
  for (const BodyEntry& entry : bodies) {
    indexed.push_back(IndexedBody{entry.entity, entry.value.position(), entry.value.radius()});
  }
  return indexed;
}

// Whether an index built over `indexed` is still current for this world.
[[nodiscard]] bool still_indexes(const std::vector<IndexedBody>& indexed,
                                 const GameWorld& world) noexcept {
  const std::span<const BodyEntry> bodies = world.store<PhysicsBody>().entries();
  return indexed.size() == bodies.size() &&
         std::equal(indexed.cbegin(), indexed.cend(), bodies.begin(), bodies.end(),
                    same_indexed_body);
}

// The same question between two worlds, which needs no copy because the committed world is still
// intact while the working world is built beside it.
[[nodiscard]] bool indexed_bodies_equal(const GameWorld& first, const GameWorld& second) noexcept {
  const std::span<const BodyEntry> left = first.store<PhysicsBody>().entries();
  const std::span<const BodyEntry> right = second.store<PhysicsBody>().entries();
  return std::equal(left.begin(), left.end(), right.begin(), right.end(),
                    [](const BodyEntry& first_entry, const BodyEntry& second_entry) {
                      return first_entry.entity == second_entry.entity &&
                             first_entry.value.position() == second_entry.value.position() &&
                             first_entry.value.radius() == second_entry.value.radius();
                    });
}

// A declared mode must accept the kinds the server issues for every session: `spawn` on admission,
// `leave` on close, and `despawn`, which a fixture or a test issues by entity. A mode that refused
// one would have the mailbox drop it on every disconnect, counted but otherwise silent, and the
// result is a body nobody owns left in the arena -- the failure this check turns into a startup
// rejection with a name (`commands/leave_command.hpp`).
void require_mode_accepts_server_issued_kinds(const CommandKindMask accepted,
                                              const std::string_view mode_name) {
  for (const CommandKind kind : {CommandKind::kSpawn, CommandKind::kDespawn, CommandKind::kLeave}) {
    if (accepted.contains(kind)) {
      continue;
    }
    throw SimulationValidationError(
        SimulationValidationCode::kGameSimulationModeRefusesServerIssuedKind,
        "game_simulation.setup.accepted_command_kinds",
        "mode " + std::string(mode_name) + " does not accept the server-issued command kind " +
            std::string(command_kind_name_of(kind)));
  }
  // A mode with a lobby is one whose seats people and bots must be able to take: accepting
  // `start_match` without `join` would be a lobby nobody can ever fill.
  if (accepted.contains(CommandKind::kStartMatch) && !accepted.contains(CommandKind::kJoin)) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameSimulationModeRefusesServerIssuedKind,
        "game_simulation.setup.accepted_command_kinds",
        "mode " + std::string(mode_name) +
            " accepts start_match but not the server-issued command kind join, so its lobby could "
            "never be filled");
  }
}

// Not noexcept, because `systems_at` rejects a stage outside the closed enumeration rather than
// reading past its offsets array (engine review finding 15). Every call below names a literal
// enumerator, so the rejection is unreachable from the kernel and is there for the caller that
// manufactures a stage value.
[[nodiscard]] bool stage_is_declared(const SystemPipeline& system_pipeline,
                                     const SystemStage stage) {
  return !system_pipeline.systems_at(stage).empty();
}

} // namespace

GameSimulation GameSimulation::create(SimulationConfig configuration, GameWorld initial_world,
                                      GameSimulationSetup setup) {
  if (setup.has_mode() &&
      (setup.has_systems() || setup.has_contact_rules() || setup.has_motion_triggers())) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameSimulationSetupConflict, "game_simulation.setup",
        "a setup that declares a GameMode may not also declare systems, contact rules, or motion "
        "triggers; the mode declares them");
  }
  const MotionLimits limits = setup.motion_limits_;
  detail::validate_motion_limits(limits);
  detail::require_motion_budget(initial_world.store<PhysicsBody>().size(), limits.bodies,
                                "initial motion body budget exhausted");
  static_cast<void>(project_contact_effect_policies(initial_world));

  // The bare rectangular arena the configuration still publishes. Every caller written before maps
  // existed lands here, which is why no accepted fixture had to change to gain a map.
  MapDefinition map = setup.has_map()
                          ? std::move(*setup.map_)
                          : MapDefinition::bare_arena(ArenaBounds::create(
                                configuration.world_width(), configuration.world_height()));

  // Every declaration is read exactly once, here, and the mode is destroyed with `setup` when this
  // function returns. "Nothing calls into the mode during a tick" is therefore structural: the
  // kernel holds declarations and never a mode
  // (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle").
  std::string mode_name{GameSimulation::kEngineDefaultModeName};
  SystemPipeline declared_systems = SystemPipeline::empty();
  ContactRuleTable contact_rules = ContactRuleTable::built_in();
  MotionTriggerTable motion_triggers = MotionTriggerTable::empty();
  CommandKindMask accepted_command_kinds = CommandKindMask::all();
  std::unique_ptr<const SpawnPolicy> spawn_policy = std::make_unique<const IdleSpawnPolicy>();
  std::unique_ptr<const MatchObjective> objective = std::make_unique<const IdleMatchObjective>();
  if (setup.has_mode()) {
    const GameMode& mode = *setup.mode_;
    mode.validate_map(map);
    mode_name = std::string(mode.name());
    declared_systems = mode.systems();
    contact_rules = mode.contact_rules();
    motion_triggers = mode.motion_triggers();
    accepted_command_kinds = mode.accepted_command_kinds();
    require_mode_accepts_server_issued_kinds(accepted_command_kinds, mode_name);
    spawn_policy = mode.spawn_policy();
    objective = mode.objective();
  } else {
    if (setup.has_systems()) {
      declared_systems = std::move(*setup.systems_);
    }
    if (setup.has_contact_rules()) {
      contact_rules = std::move(*setup.contact_rules_);
    }
    if (setup.has_motion_triggers()) {
      motion_triggers = std::move(*setup.motion_triggers_);
    }
  }

  // A spawn point that cannot seat a disc of the configured radius is a startup rejection rather
  // than a bounds failure on the tick that first seated an entity there. The production path has
  // already run this inside `GameWorld::create(configuration, map, seed)`; running it again here
  // is what covers every other way a map reaches a simulation.
  require_spawn_points_are_seatable(configuration, map);
  require_committed_bodies_in_bounds(initial_world, map, configuration);

  // The engine's lifecycle system is appended last at kLifecycle and is not removable, so a mode's
  // own lifecycle systems always run before this tick's phase transition is evaluated.
  SystemPipeline system_pipeline =
      std::move(declared_systems)
          .with_appended(SystemPipeline::StagedSystem{
              SystemStage::kLifecycle,
              std::make_unique<const MatchLifecycleSystem>(std::move(objective))});

  SpatialGrid initial_grid = SpatialGrid::create(configuration, map.bounds(), initial_world);
  // Allocate only after validation, outside the non-throwing constructor. Each snapshot retains
  // an aliasing handle to this immutable map's terrain instead of allocating a terrain wrapper.
  std::shared_ptr<const MapDefinition> retained_map =
      std::make_shared<const MapDefinition>(std::move(map));
  return GameSimulation(configuration, std::move(retained_map), std::move(initial_world),
                        std::move(initial_grid), std::move(system_pipeline),
                        std::move(contact_rules), std::move(motion_triggers), limits,
                        SpawnSystem(std::move(spawn_policy)), std::move(mode_name),
                        accepted_command_kinds, TickSequence::zero());
}

MovementTuningDecisions GameSimulation::step(const FixedDelta fixed_delta,
                                             const InputBatch& input_batch) {
  const TickSequence next_tick_sequence = tick_sequence_.next();
  std::vector<MovementTuningDecision> tuning_decisions;
  const auto tuning_count = std::count_if(
      input_batch.commands().begin(), input_batch.commands().end(), [](const Command& command) {
        return std::holds_alternative<SetMovementTuningCommand>(command);
      });
  tuning_decisions.reserve(static_cast<std::size_t>(tuning_count));

  // Phase 0. Every phase and stage below reads the working world, so a failure anywhere leaves the
  // committed world, grid, and sequence exactly as the previous commit left them.
  //
  // Opening the tick installs the batch's EntityIdReservation on the working world, which is what
  // makes `GameWorld::create_entity()` succeed for the duration of this tick and nowhere else
  // (`docs/architecture/0004-gameplay-architecture.md`
  // § "Determinism obligations for framework code").
  GameWorld next_world = world_;
  next_world.open_tick(input_batch.entity_id_reservation());
  // The map's spawn-marker count is the lobby's ceiling at run time, as it is at startup.
  apply_input_batch(next_world, input_batch, map().spawn_points().size(), next_tick_sequence,
                    tuning_decisions);
  detail::require_motion_budget(next_world.store<PhysicsBody>().size(), motion_limits_.bodies,
                                "intake motion body budget exhausted");

  // The batch's own index: the bodies the despawns of this batch left, at this tick's
  // start-of-tick positions. It is derived before seating because the SpawnSystem's policy socket
  // reads a TickContext, and the index that context must publish is the world as the tick found
  // it -- a seating cannot observe a seating.
  std::optional<SpatialGrid> reindexed_batch_grid;
  if (!indexed_bodies_equal(next_world, world_)) {
    reindexed_batch_grid = grid_.rebuilt(next_world);
  }
  const SpatialGrid& batch_grid = reindexed_batch_grid ? *reindexed_batch_grid : grid_;
  const TickContext seating_context =
      TickContext::create(next_tick_sequence, fixed_delta, configuration_, map(), batch_grid);

  // Still phase 0: the engine's SpawnSystem offers every entity awaiting a body to the mode's
  // SpawnPolicy and performs the seatings it chose. A seated entity is indexed at its marker, so
  // the intake index is re-derived exactly when something was seated.
  const std::size_t seated_count = spawn_system_.seat_pending_entities(next_world, seating_context);
  detail::require_motion_budget(next_world.store<PhysicsBody>().size(), motion_limits_.bodies,
                                "seated motion body budget exhausted");

  // The intake index: the index of the bodies phase 0 left, at this tick's start-of-tick
  // positions. The committed index already is that value whenever phase 0 changed no indexed body,
  // so rebuilding exactly when it did is both the contract and what keeps an empty batch
  // bit-identical to the accepted baseline
  // (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick").
  //
  // It is resolved before the pre-kernel stage rather than after it because this is the index
  // `TickContext::spatial_index()` promises a kPreKernel system: the bodies phase 0 left, at
  // positions nothing has moved yet.
  std::optional<SpatialGrid> reindexed_intake_grid;
  if (seated_count != 0) {
    reindexed_intake_grid = batch_grid.rebuilt(next_world);
  }
  const SpatialGrid& intake_grid = reindexed_intake_grid ? *reindexed_intake_grid : batch_grid;

  const TickContext intake_context =
      TickContext::create(next_tick_sequence, fixed_delta, configuration_, map(), intake_grid);

  // A kPreKernel system may create or resize a physical body. Rebuild the index exposed through
  // the kernel context when its coverage changed; the solver builds its own swept candidates.
  // The snapshot is taken
  // only when the mode declared such a system at all, so a mode with an empty stage, which is
  // every accepted fixture, allocates nothing here.
  std::vector<IndexedBody> intake_indexed_bodies;
  const bool pre_kernel_declared = stage_is_declared(system_pipeline_, SystemStage::kPreKernel);
  if (pre_kernel_declared) {
    intake_indexed_bodies = indexed_bodies_of(next_world);
  }
  apply_stage(system_pipeline_, SystemStage::kPreKernel, next_world, intake_context);
  detail::require_motion_budget(next_world.store<PhysicsBody>().size(), motion_limits_.bodies,
                                "kernel-entry motion body budget exhausted");

  std::optional<SpatialGrid> reindexed_pair_grid;
  if (pre_kernel_declared && !still_indexes(intake_indexed_bodies, next_world)) {
    reindexed_pair_grid = intake_grid.rebuilt(next_world);
  }
  const SpatialGrid& pair_grid = reindexed_pair_grid ? *reindexed_pair_grid : intake_grid;
  const TickContext kernel_context =
      TickContext::create(next_tick_sequence, fixed_delta, configuration_, map(), pair_grid);

  // Freeze the post-intake/post-pre-kernel world. Phase 1 derives motion separately, so policies
  // observe this same immutable state throughout the solve, never partially applied effects.
  const GameWorld& kernel_world = next_world;
  const auto effect_policies = project_contact_effect_policies(kernel_world);
  const auto triggers = motion_triggers_.bind(kernel_world, kernel_context, motion_limits_);
  const auto facts = LiveMotionFacts::for_contacts(contact_rules_);
  const std::vector<BodyEntry> next_bodies =
      apply_stored_acceleration_and_drag(next_world, configuration_.drag_per_second(), fixed_delta);
  std::vector<ContactRule::Subject> subjects;
  subjects.reserve(next_bodies.size());
  for (const auto& body : next_bodies) {
    subjects.push_back({body.entity, body.value});
  }
  const auto motion = solve_continuous_motion<WorldEvent, LiveMotionFacts>(
      kernel_world, subjects, kernel_context, facts, respond_live_pair, triggers.rows(),
      motion_limits_, effect_policies);
  apply_motion_result(next_world, motion);
  SpatialGrid next_grid = pair_grid.rebuilt(next_world);

  // From here the index a system reads is this tick's phase 6 rebuild, over the positions phase 5
  // integrated. A stage that writes a body invalidates the very index it was handed, which is why
  // the commit re-derives it below rather than trusting that no stage wrote one.
  std::vector<IndexedBody> committed_indexed_bodies;
  const bool late_stage_declared = stage_is_declared(system_pipeline_, SystemStage::kPostKernel) ||
                                   stage_is_declared(system_pipeline_, SystemStage::kLifecycle);
  if (late_stage_declared) {
    committed_indexed_bodies = indexed_bodies_of(next_world);
  }
  const TickContext committed_context =
      TickContext::create(next_tick_sequence, fixed_delta, configuration_, map(), next_grid);
  apply_stage(system_pipeline_, SystemStage::kPostKernel, next_world, committed_context);
  apply_stage(system_pipeline_, SystemStage::kLifecycle, next_world, committed_context);

  // Phase 10: apply this tick's DespawnEvent removals, validate what survives them, rebuild the
  // index from the survivors, clear the tick-local state, replace the committed state, and
  // increment the sequence once. The contract's signed-zero canonicalization has no step here
  // because Vector2 performs it at every construction, so no negative zero can reach a body in the
  // first place.
  //
  // **Removal precedes validation, which is a deliberate deviation from the written order of
  // `docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick" phase 10**
  // and closes engine review finding 14. Validating first makes an entity that a system both
  // pushed out of bounds and marked for despawn stop the match, and royale's elimination pairs
  // exactly those two: `zone_elimination` names an entity that left the safe zone, which is
  // frequently an entity a contact has just pushed past the arena edge. Validating what survives
  // asks the only question that matters -- is the world this tick is about to *publish* legal --
  // and an entity that is about to be removed has no committed position to be illegal. Everything
  // the original order guaranteed still holds: removal precedes the rebuild, the rebuild precedes
  // publication, no committed grid holds a non-live EntityId, and no snapshot observes a
  // half-applied removal. ADR 0003 owes this reordering an amendment.
  const bool roster_removed = apply_despawn_events(next_world);
  detail::require_motion_budget(next_world.store<PhysicsBody>().size(), motion_limits_.bodies,
                                "surviving motion body budget exhausted");
  static_cast<void>(project_contact_effect_policies(next_world));
  require_committed_bodies_in_bounds(next_world, map(), configuration_);
  if (roster_removed ||
      (late_stage_declared && !still_indexes(committed_indexed_bodies, next_world))) {
    next_grid = next_grid.rebuilt(next_world);
  }
  next_world.close_tick();

#ifndef NDEBUG
  // The invariant the predicates above exist to maintain, checked rather than asserted in prose.
  // It costs a second full rebuild, so it is debug-only -- which is every unit-test, fixture, and
  // sanitizer run, and therefore every run that could catch a stale index.
  if (!(next_grid == next_grid.rebuilt(next_world))) {
    throw SimulationValidationError(
        SimulationValidationCode::kGameSimulationSpatialIndexStale, "game_simulation.commit.grid",
        "the committed spatial index is not a rebuild of the committed world");
  }
#endif

  // All calculations and allocations are complete. These value moves are noexcept, so the three
  // assignments form one non-throwing commit from the caller's perspective.
  world_ = std::move(next_world);
  grid_ = std::move(next_grid);
  tick_sequence_ = next_tick_sequence;
  return MovementTuningDecisions(std::move(tuning_decisions));
}

WorldSnapshot GameSimulation::snapshot() const {
  return WorldSnapshot::from_world(tick_sequence_, world_, mode_name_, map_);
}

GameSimulation::GameSimulation(SimulationConfig configuration,
                               std::shared_ptr<const MapDefinition> map, GameWorld world,
                               SpatialGrid grid, SystemPipeline system_pipeline,
                               ContactRuleTable contact_rules, MotionTriggerTable motion_triggers,
                               MotionLimits motion_limits, SpawnSystem spawn_system,
                               std::string mode_name, const CommandKindMask accepted_command_kinds,
                               const TickSequence tick_sequence) noexcept
    : configuration_(configuration), map_(std::move(map)), world_(std::move(world)),
      grid_(std::move(grid)), system_pipeline_(std::move(system_pipeline)),
      contact_rules_(std::move(contact_rules)), motion_triggers_(std::move(motion_triggers)),
      motion_limits_(motion_limits), spawn_system_(std::move(spawn_system)),
      mode_name_(std::move(mode_name)), accepted_command_kinds_(accepted_command_kinds),
      tick_sequence_(tick_sequence) {}

} // namespace blob_royale::simulation
