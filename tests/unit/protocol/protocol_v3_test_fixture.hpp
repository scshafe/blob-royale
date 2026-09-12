#ifndef BLOB_ROYALE_TESTS_UNIT_PROTOCOL_PROTOCOL_V3_TEST_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_PROTOCOL_PROTOCOL_V3_TEST_FIXTURE_HPP

#include "controller_directory_view.hpp"
#include "lobby_listing.hpp"
#include "protocol_encoding_error.hpp"
#include "protocol_v3_json_encoding.hpp"
#include "request_id.hpp"
#include "session_welcome.hpp"

#include "command_kind_mask.hpp"
#include "components/controllable_component.hpp"
#include "components/hill_component.hpp"
#include "components/hill_motion_component.hpp"
#include "components/race_progress_component.hpp"
#include "components/zone_component.hpp"
#include "components/zone_exposure_component.hpp"
#include "contact_rule_table.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "fixed_delta.hpp"
#include "game_mode.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "game_world.hpp"
#include "input_batch.hpp"
#include "map_definition.hpp"
#include "match_lifecycle_durations.hpp"
#include "match_objective.hpp"
#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "match_state.hpp"
#include "mode_states/king_of_the_hill_mode_state.hpp"
#include "mode_states/race_mode_state.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "physics_body.hpp"
#include "random_stream_registry.hpp"
#include "seat_roster.hpp"
#include "simulation_config.hpp"
#include "simulation_system.hpp"
#include "spawn_policy.hpp"
#include "system_pipeline.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef BLOB_ROYALE_PROTOCOL_V3_SCHEMA_DIRECTORY
#error "BLOB_ROYALE_PROTOCOL_V3_SCHEMA_DIRECTORY must name docs/protocol/schema/v3"
#endif

namespace blob_royale::protocol::v3_test_fixture {

namespace simulation = blob_royale::simulation;

// The exact values of `docs/protocol/schema/v3/examples/*.json`, named once so a golden-byte test
// and the world that produces it cannot disagree about what the accepted document says.
inline constexpr std::string_view kSessionRequestId = "018f47a4-9c21-7f10-8a55-4b7d1e0c33a2";
inline constexpr std::string_view kWelcomeTimestamp = "2026-09-06T18:04:11.500Z";
// The golden session's room and its map's seat ceiling: room 1, the 32-marker shipped arena.
inline constexpr std::uint64_t kGoldenLobbyId = 1;
inline constexpr std::uint64_t kGoldenSeatCountMaximum = 32;
inline constexpr std::string_view kSnapshotTimestamp = "2026-09-06T18:04:17.750Z";
inline constexpr std::uint64_t kSnapshotMessageSequence = 129;
inline constexpr std::uint64_t kGoldenTickSequence = 12'904;
inline constexpr std::uint64_t kGoldenPhaseStartedTick = 10'904;
inline constexpr std::uint64_t kGoldenEliminatedTick = 12'400;
inline constexpr std::uint64_t kGoldenEliminationGraceTicks = 1'200;
// Two gates taken, matching examples/race-progress-component.json.
inline constexpr std::uint64_t kGoldenNextCheckpoint = 2;

inline constexpr std::uint64_t kWallEntityId = 1;
inline constexpr std::uint64_t kPlayerEntityId = 7;
inline constexpr std::uint64_t kBotEntityId = 8;
inline constexpr std::uint64_t kZoneEntityId = 9;
inline constexpr std::uint64_t kPlacedEntityId = 5;

inline constexpr std::uint64_t kPlayerControllerId = 3;
inline constexpr std::uint64_t kBotControllerId = 4;
inline constexpr std::uint64_t kPlacedControllerId = 6;
// The bot whose seat the runtime has already built, which is what makes seat 1 an NPC seat carrying
// a controller. It drives no entity here on purpose: a seat is a lobby fact and a body is an arena
// fact, and the golden should not imply the two are the same value.
inline constexpr std::uint64_t kSeatedBotId = 12;

inline constexpr std::string_view kPlayerDisplayName = "Cole Shaffer";
inline constexpr std::string_view kBotDisplayName = "wanderer-1";
inline constexpr std::string_view kSessionControllerKind = "session";
inline constexpr std::string_view kBotControllerKind = "wanderer";
inline constexpr std::string_view kGoldenModeName = "royale";
inline constexpr std::string_view kGoldenMapName = "arena-960x640";

[[nodiscard]] inline RequestId session_request_id() { return decode_request_id(kSessionRequestId); }

// canonical: stub_controller_directory -- the test's implementation of the encoder's directory
// port.
//
// It is the second implementation of `ControllerDirectoryView` alongside the adapter the server
// gains in Step 26, and it is deliberately a plain map: the port exists so `blob_protocol` never
// names `blob_runtime`, and a test that reached for the runtime's own directory would defeat the
// separation it is meant to prove.
class StubControllerDirectory final : public ControllerDirectoryView {
public:
  void add_controller(const std::uint64_t controller, const std::string_view controller_kind,
                      const std::string_view display_name) {
    controllers_.insert_or_assign(
        simulation::ControllerId::create(controller),
        PublishedController{std::string{controller_kind}, std::string{display_name}});
  }

  void forget_controller(const std::uint64_t controller) {
    controllers_.erase(simulation::ControllerId::create(controller));
  }

  [[nodiscard]] std::optional<PublishedController>
  find_controller(const simulation::ControllerId controller) const override {
    const auto match = controllers_.find(controller);
    if (match == controllers_.cend()) {
      return std::nullopt;
    }
    return match->second;
  }

private:
  std::map<simulation::ControllerId, PublishedController> controllers_;
};

// The directory the golden snapshot is encoded against.
[[nodiscard]] inline StubControllerDirectory golden_directory() {
  StubControllerDirectory directory;
  directory.add_controller(kPlayerControllerId, kSessionControllerKind, kPlayerDisplayName);
  directory.add_controller(kBotControllerId, kBotControllerKind, kBotDisplayName);
  // `kPlacedControllerId` is deliberately absent: the placed entity's session has closed, and the
  // encoder reads its controller from the recorded `RoyalePlacement` rather than from here.
  return directory;
}

// canonical: golden_state_pin_system -- the in-test system that writes the accepted example's
// world.
//
// The golden document is one committed tick of a whole world -- a static wall, a session player, a
// bot with identical component structure, a zone entity, and a placement for a body that is already
// destroyed -- at tick 12,904. A test cannot construct a `WorldSnapshot` directly, because only
// `GameSimulation` may publish one, so the world is *driven* to the accepted state instead: this
// system runs at `kLifecycle`, after every kernel phase and before the commit, and rewrites exactly
// the components and match state the example declares. Every tick therefore commits the same state
// and the snapshot at tick 12,904 is the example.
//
// It obeys the system interface exactly: `apply` is const, it holds only the values it was
// constructed with, and everything it writes is world-owned.
class GoldenStatePinSystem final : public simulation::SimulationSystem {
public:
  [[nodiscard]] std::string_view name() const noexcept override { return "golden_state_pin"; }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    const simulation::Vector2 at_rest = simulation::Vector2::create(0.0, 0.0);

    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
        simulation::EntityId::create(kWallEntityId),
        simulation::PhysicsBody::create(simulation::Vector2::create(480.0, 160.0), at_rest, at_rest,
                                        40.0, 0.0, 2, 1, true));

    world.mutable_store<simulation::Controllable>().insert_or_assign(
        simulation::EntityId::create(kPlayerEntityId),
        simulation::Controllable{simulation::ControllerId::create(kPlayerControllerId)});
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
        simulation::EntityId::create(kPlayerEntityId),
        simulation::PhysicsBody::create(
            simulation::Vector2::create(412.5, 288.25), simulation::Vector2::create(18.75, -42.5),
            simulation::Vector2::create(400.0, 0.0), 10.0, 1.0, 1, 3, false));
    world.mutable_store<simulation::ZoneExposure>().insert_or_assign(
        simulation::EntityId::create(kPlayerEntityId), simulation::ZoneExposure{0});

    world.mutable_store<simulation::Controllable>().insert_or_assign(
        simulation::EntityId::create(kBotEntityId),
        simulation::Controllable{simulation::ControllerId::create(kBotControllerId)});
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
        simulation::EntityId::create(kBotEntityId),
        simulation::PhysicsBody::create(
            simulation::Vector2::create(760.5, 512.25), simulation::Vector2::create(-6.25, 31.5),
            simulation::Vector2::create(0.0, -400.0), 10.0, 1.0, 1, 3, false));
    world.mutable_store<simulation::ZoneExposure>().insert_or_assign(
        simulation::EntityId::create(kBotEntityId), simulation::ZoneExposure{214});

    world.mutable_store<simulation::Zone>().insert_or_assign(
        simulation::EntityId::create(kZoneEntityId),
        simulation::Zone{simulation::Vector2::create(480.0, 320.0), 210.5});

    simulation::MatchState& match = world.mutable_match();
    match.phase = simulation::MatchPhase::kRunning;
    match.phase_started_tick = simulation::TickSequence::create(kGoldenPhaseStartedTick);
    // A four-seat lobby holding one of every published seat shape: a person, an NPC whose bot the
    // runtime has built, an NPC still waiting for one, and an empty seat. A running match carrying
    // a full roster is the ordinary case -- the roster is what started it and nothing clears it --
    // and publishing all four shapes in one golden is what makes the example a decoder can be
    // written against.
    match.seats = simulation::SeatRoster::of_size(4);
    match.seats.assign_seat(0, simulation::Seat{simulation::ControllerSeat{
                                   simulation::ControllerId::create(kPlayerControllerId)}});
    match.seats.assign_seat(
        1, simulation::Seat{simulation::NpcSeat{simulation::SeatKindName::create("wanderer"),
                                                simulation::ControllerId::create(kSeatedBotId)}});
    match.seats.assign_seat(2, simulation::Seat{simulation::NpcSeat{
                                   simulation::SeatKindName::create("chaser"), std::nullopt}});
    match.seats.request_start();
    match.running_started_tick = simulation::TickSequence::create(kGoldenPhaseStartedTick);
    match.outcome = simulation::MatchOutcome::undecided();
    // The grace is royale's proposed `elimination_grace_seconds = 3.0` at 400 ticks/s, so the
    // golden publishes a number an operator would recognize rather than a round test value.
    match.mode_state = simulation::RoyalePlacementsModeState{
        {simulation::RoyalePlacement{simulation::EntityId::create(kPlacedEntityId),
                                     simulation::ControllerId::create(kPlacedControllerId), 3,
                                     simulation::TickSequence::create(kGoldenEliminatedTick)}},
        simulation::MatchPhase::kRunning,
        kGoldenEliminationGraceTicks};
  }
};

// An objective that never starts and never decides, so the engine's lifecycle system commits no
// transition and leaves the pinned match state exactly as this fixture wrote it.
class UndecidedObjective final : public simulation::MatchObjective {
public:
  [[nodiscard]] bool can_start(const simulation::GameWorld&) const override { return false; }

  [[nodiscard]] simulation::MatchOutcome outcome(const simulation::GameWorld&,
                                                 const simulation::TickContext&) const override {
    return simulation::MatchOutcome::undecided();
  }

  [[nodiscard]] simulation::MatchLifecycleDurations durations() const noexcept override {
    return simulation::MatchLifecycleDurations{};
  }
};

// A policy that never seats, because the fixture's entities are written by the pin system at the
// ids the accepted example names rather than drawn from a spawn point.
class NeverSeatingSpawnPolicy final : public simulation::SpawnPolicy {
public:
  [[nodiscard]] std::optional<std::size_t>
  choose_spawn_point(const simulation::GameWorld&, const simulation::TickContext&,
                     simulation::EntityId, std::size_t, std::span<const bool>) const override {
    return std::nullopt;
  }
};

// The mode the fixture's simulation runs. Its declared name is what `match.mode` publishes, which
// is why it is `royale`: the fixture reproduces the accepted royale document, not `RoyaleMode`
// itself, which `blob_protocol` neither links nor may link.
class GoldenStateMode final : public simulation::GameMode {
public:
  [[nodiscard]] std::string_view name() const noexcept override { return kGoldenModeName; }

  [[nodiscard]] simulation::SystemPipeline systems() const override {
    std::vector<simulation::SystemPipeline::StagedSystem> staged;
    staged.push_back(simulation::SystemPipeline::StagedSystem{
        simulation::SystemStage::kLifecycle, std::make_unique<const GoldenStatePinSystem>()});
    return simulation::SystemPipeline::create(std::move(staged));
  }

  [[nodiscard]] simulation::ContactRuleTable contact_rules() const override {
    return simulation::ContactRuleTable::built_in();
  }

  // The three server-issued kinds every declared mode must accept, plus the one client kind. The
  // welcome still publishes only `set_thrust`: it is the intersection with the client-sendable
  // vocabulary, and `spawn`, `despawn`, and `leave` have no wire name.
  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept override {
    return simulation::CommandKindMask::create(
        {simulation::CommandKind::kSpawn, simulation::CommandKind::kDespawn,
         simulation::CommandKind::kLeave, simulation::CommandKind::kThrust});
  }

  [[nodiscard]] std::unique_ptr<const simulation::SpawnPolicy> spawn_policy() const override {
    return std::make_unique<const NeverSeatingSpawnPolicy>();
  }

  [[nodiscard]] std::unique_ptr<const simulation::MatchObjective> objective() const override {
    return std::make_unique<const UndecidedObjective>();
  }

  void validate_map(const simulation::MapDefinition&) const override {}
};

[[nodiscard]] inline simulation::MapDefinition golden_map() {
  return simulation::MapDefinition::create("golden-arena",
                                           simulation::ArenaBounds::create(960.0, 640.0), {}, {},
                                           simulation::MapMetadata::none());
}

// The accepted example's committed tick, produced by the real kernel rather than assembled by hand:
// `WorldSnapshot` is constructible only by `GameSimulation`, which is the property that makes a
// golden-byte test a test of the production path.
[[nodiscard]] inline const simulation::WorldSnapshot& golden_snapshot() {
  static const simulation::WorldSnapshot snapshot = [] {
    simulation::GameSimulation game_simulation = simulation::GameSimulation::create(
        simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16),
        simulation::GameWorld::create({}),
        simulation::GameSimulationSetup::of_mode(golden_map(),
                                                 std::make_unique<const GoldenStateMode>()));
    for (std::uint64_t tick = 0; tick < kGoldenTickSequence; ++tick) {
      game_simulation.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
    }
    return game_simulation.snapshot();
  }();
  return snapshot;
}

// Uses ordinary world-owned draws so the full encoder test proves publication of real counts,
// not a manually injected snapshot or generator state. Two ticks consume hazards=4 and hill=6.
class CountedRandomDrawSystem final : public simulation::SimulationSystem {
public:
  [[nodiscard]] std::string_view name() const noexcept override { return "counted_random_draws"; }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    for (std::size_t draw = 0; draw < 2; ++draw) {
      static_cast<void>(world.random(simulation::RandomStreamKind::kHazards).next_bits());
    }
    for (std::size_t draw = 0; draw < 3; ++draw) {
      static_cast<void>(world.random(simulation::RandomStreamKind::kHill).next_bits());
    }
  }
};

[[nodiscard]] inline simulation::WorldSnapshot counted_random_snapshot() {
  std::vector<simulation::SystemPipeline::StagedSystem> systems;
  systems.push_back(simulation::SystemPipeline::StagedSystem{
      simulation::SystemStage::kLifecycle, std::make_unique<const CountedRandomDrawSystem>()});
  simulation::GameSimulation game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16),
      simulation::GameWorld::create({}),
      simulation::GameSimulationSetup::engine_defaults().with_systems(
          simulation::SystemPipeline::create(std::move(systems))));
  for (std::size_t tick = 0; tick < 2; ++tick) {
    game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  }
  return game.snapshot();
}

// A committed tick of a match that has never transitioned, which is what every lobby looks like
// before its first phase change: `MatchState::phase_started_tick` is still `TickSequence::zero()`.
// It runs on the engine's own declarations, so nothing writes a lifecycle field.
[[nodiscard]] inline const simulation::WorldSnapshot& untransitioned_lobby_snapshot() {
  static const simulation::WorldSnapshot snapshot = [] {
    simulation::GameSimulation game_simulation = simulation::GameSimulation::create(
        simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16),
        simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
            simulation::EntityId::create(kPlayerEntityId),
            simulation::PhysicsBody::create(
                simulation::Vector2::create(480.0, 320.0), simulation::Vector2::create(0.0, 0.0),
                simulation::Vector2::create(0.0, 0.0), 10.0, 1.0, 1, 1, false),
            simulation::ControllerId::create(kPlayerControllerId))}));
    game_simulation.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
    return game_simulation.snapshot();
  }();
  return snapshot;
}

// Explicit shared movement state at a committed tick, independent of gameplay mutation policy.
[[nodiscard]] inline simulation::WorldSnapshot
tuning_snapshot(simulation::MovementTuningState movement, const std::uint64_t tick_count = 1) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_match().movement = std::move(movement);
  simulation::GameSimulation game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world));
  for (std::uint64_t tick = 0; tick < tick_count; ++tick) {
    game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  }
  return game.snapshot();
}

// An entity carrying only progress proves registry-driven publication does not require a body.
// The engine owns no race rule, so the supplied gate count survives the committed tick unchanged.
[[nodiscard]] inline simulation::WorldSnapshot
race_progress_snapshot(const std::uint64_t next_checkpoint) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(
      simulation::EntityId::create(kPlayerEntityId), simulation::RaceProgress{next_checkpoint});
  simulation::GameSimulation game_simulation = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world));
  game_simulation.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  return game_simulation.snapshot();
}

// Published hill values committed by the idle engine, with no gameplay dependency in this fixture.
[[nodiscard]] inline simulation::WorldSnapshot
hill_mode_snapshot(const double hill_radius, const std::uint64_t points_to_win,
                   const std::optional<simulation::HillMotion> motion = std::nullopt) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_store<simulation::Hill>().insert_or_assign(
      simulation::EntityId::create(kPlayerEntityId),
      simulation::Hill{simulation::Vector2::create(480.0, 320.0), hill_radius});
  if (motion.has_value()) {
    world.mutable_store<simulation::HillMotion>().insert_or_assign(
        simulation::EntityId::create(kPlayerEntityId), *motion);
  }
  world.mutable_match().mode_state = simulation::KingOfTheHillModeState{points_to_win, 400, 96'000};
  simulation::GameSimulation game_simulation = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world));
  game_simulation.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  return game_simulation.snapshot();
}

// The accepted race block: one bend, three gates, and two simultaneous first-place finishes.
[[nodiscard]] inline simulation::RaceModeState golden_race_mode_state() {
  return simulation::RaceModeState{
      simulation::RaceRoadName::create("road"),
      20.0,
      {simulation::Vector2::create(300.0, 100.0), simulation::Vector2::create(700.0, 200.0),
       simulation::Vector2::create(700.0, 500.0)},
      96'000,
      2'000,
      {simulation::RaceStanding{simulation::EntityId::create(kPlayerEntityId),
                                simulation::ControllerId::create(kPlayerControllerId), 1,
                                simulation::TickSequence::create(1),
                                simulation::MotionTime::create(0.25)},
       simulation::RaceStanding{simulation::EntityId::create(kBotEntityId),
                                simulation::ControllerId::create(kBotControllerId), 1,
                                simulation::TickSequence::create(1),
                                simulation::MotionTime::create(0.25)}}};
}

// Actual authored terrain for the golden race block, not a geometry mirror in match state.
[[nodiscard]] inline simulation::TerrainDefinition
race_terrain(const std::string& road_name = "road", const double half_width = 60.0,
             const bool include_unselected = false, const bool selected_first = true) {
  const std::vector<simulation::Vector2> points{simulation::Vector2::create(100.0, 100.0),
                                                simulation::Vector2::create(700.0, 100.0),
                                                simulation::Vector2::create(700.0, 500.0)};
  std::vector<simulation::TerrainCorridor> corridors;
  if (include_unselected && !selected_first) {
    corridors.push_back(simulation::TerrainCorridor::create("unselected", 10.0, points));
  }
  corridors.push_back(simulation::TerrainCorridor::create(road_name, half_width, points));
  if (include_unselected && selected_first) {
    corridors.push_back(simulation::TerrainCorridor::create("unselected", 10.0, points));
  }
  return simulation::TerrainDefinition::create(simulation::ArenaBounds::create(960.0, 640.0),
                                               simulation::TerrainGround::kCorridors,
                                               std::move(corridors), {});
}

// Runs on the engine's idle declarations so no gameplay system can repair a malformed block
// before an encoder rejection test observes it. The wire arm itself is mode-registry-driven.
[[nodiscard]] inline simulation::WorldSnapshot
race_mode_snapshot(simulation::RaceModeState state, simulation::TerrainDefinition terrain) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(
      simulation::EntityId::create(kPlayerEntityId), simulation::RaceProgress{3});
  world.mutable_match().mode_state = std::move(state);
  simulation::GameSimulation game_simulation = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(simulation::MapDefinition::create(
          "protocol_race", std::move(terrain), {}, {}, simulation::MapMetadata::none())));
  game_simulation.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  return game_simulation.snapshot();
}

// The NPC kinds the golden welcome publishes, in the order `ControllerRegistry` declares them. It
// is a *fixture* copy rather than a read of the registry: `blob_protocol` does not link
// `blob_controllers`, and the whole point of the member is that the list travels as data.
inline constexpr std::array<std::string_view, 2> kGoldenNpcControllerKinds{"wanderer", "chaser"};

[[nodiscard]] inline std::vector<std::string> golden_npc_controller_kinds() {
  return {std::string{kGoldenNpcControllerKinds[0]}, std::string{kGoldenNpcControllerKinds[1]}};
}

[[nodiscard]] inline simulation::NpcCatalogue golden_npc_catalogue() {
  return simulation::NpcCatalogue::create(golden_npc_controller_kinds());
}

[[nodiscard]] inline simulation::TerrainDefinition golden_terrain() {
  return simulation::TerrainDefinition::solid(simulation::ArenaBounds::create(960.0, 640.0));
}

// The golden welcome names mode `royale`, so its advertised set is royale's real one and moves
// when royale's does. Plan Step 18 added `shield` to all four gameplay modes on 2026-09-12, so
// this mask, the pinned frame bytes in protocol_v3_json_encoding_tests.cpp, and both
// `welcome*-message.json` golden examples gained it together; a golden depicting a welcome no
// royale session can produce would be a fixture that documents the wrong contract.
[[nodiscard]] inline SessionWelcome golden_welcome() {
  return SessionWelcome::create(
      simulation::EntityId::create(kPlayerEntityId),
      simulation::ControllerId::create(kPlayerControllerId), std::string{kPlayerDisplayName},
      std::string{kGoldenModeName}, std::string{kGoldenMapName},
      simulation::CommandKindMask::create(
          {simulation::CommandKind::kThrust, simulation::CommandKind::kSetSeatCount,
           simulation::CommandKind::kClearSeat, simulation::CommandKind::kSeatNpc,
           simulation::CommandKind::kStartMatch, simulation::CommandKind::kSetMovementTuning,
           simulation::CommandKind::kShield}),
      golden_npc_catalogue(), kGoldenLobbyId, kGoldenSeatCountMaximum, golden_terrain());
}

// Maximum authored shape/point/segment counts and maximum bounded string lengths, all valid
// simultaneously. Repeated geometry has distinct authored names and keeps compilation bounded;
// the wire still carries every authored row. This is a complete welcome budget workload, not a
// claim that one selected floating-point spelling maximizes all possible JSON byte sequences.
[[nodiscard]] inline SessionWelcome maximum_cardinality_welcome() {
  const auto maximum_name = [](const std::size_t index) {
    const std::string suffix = std::to_string(index);
    return std::string(kKindNameMaximumCharacterCount - suffix.size(), 'a') + suffix;
  };
  std::vector<simulation::TerrainCorridor> corridors;
  corridors.reserve(simulation::kMaximumTerrainCorridorCount);
  for (std::size_t index = 0; index < simulation::kMaximumTerrainCorridorCount; ++index) {
    corridors.push_back(simulation::TerrainCorridor::create(
        maximum_name(index), 70.0,
        {simulation::Vector2::create(100.0, 320.0), simulation::Vector2::create(280.0, 320.0),
         simulation::Vector2::create(460.0, 320.0), simulation::Vector2::create(640.0, 320.0),
         simulation::Vector2::create(820.0, 320.0)}));
  }
  std::vector<simulation::TerrainHole> holes;
  holes.reserve(simulation::kMaximumTerrainHoleCount);
  for (std::size_t index = 0; index < simulation::kMaximumTerrainHoleCount; ++index) {
    holes.push_back(simulation::TerrainHole::create(
        maximum_name(index), simulation::Vector2::create(460.0, 320.0), 20.0));
  }
  std::vector<std::string> npc_kinds;
  npc_kinds.reserve(kNpcControllerKindLimit);
  for (std::size_t index = 0; index < kNpcControllerKindLimit; ++index) {
    npc_kinds.push_back(maximum_name(index));
  }
  return SessionWelcome::create(
      simulation::EntityId::create(kMaximumSafeInteger),
      simulation::ControllerId::create(kMaximumSafeInteger),
      std::string(kDisplayNameMaximumCharacterCount, 'A'),
      std::string(kKindNameMaximumCharacterCount, 'a'),
      std::string(kMapNameMaximumCharacterCount, 'a'), simulation::CommandKindMask::all(),
      simulation::NpcCatalogue::create(std::move(npc_kinds)), kLobbyDirectoryLimit,
      kLobbySeatCountMaximum,
      simulation::TerrainDefinition::create(simulation::ArenaBounds::create(960.0, 640.0),
                                            simulation::TerrainGround::kCorridors,
                                            std::move(corridors), std::move(holes)));
}

// The two rooms of the golden directory: room 1 is the golden snapshot's match as the directory
// would list it -- running since tick 10904, read at tick 12904, a person and a created bot in two
// of four seats with a third declared and waiting -- and room 2 is a fresh lobby with one bot
// declared and nobody in it.
[[nodiscard]] inline std::vector<LobbyListing> golden_lobby_listings() {
  return {LobbyListing{.lobby_id = 1,
                       .mode_name = std::string{kGoldenModeName},
                       .map_name = std::string{kGoldenMapName},
                       .phase = simulation::MatchPhase::kRunning,
                       .phase_started_tick = 10'904,
                       .tick_sequence = kGoldenTickSequence,
                       .seat_count = 4,
                       .seat_count_maximum = kGoldenSeatCountMaximum,
                       .filled_seat_count = 2,
                       .npc_seat_count = 2,
                       .session_count = 1,
                       .healthy = true},
          LobbyListing{.lobby_id = 2,
                       .mode_name = std::string{kGoldenModeName},
                       .map_name = std::string{kGoldenMapName},
                       .phase = simulation::MatchPhase::kLobby,
                       .phase_started_tick = 0,
                       .tick_sequence = kGoldenTickSequence,
                       .seat_count = 4,
                       .seat_count_maximum = kGoldenSeatCountMaximum,
                       .filled_seat_count = 1,
                       .npc_seat_count = 1,
                       .session_count = 0,
                       .healthy = true}};
}

[[nodiscard]] inline std::string read_v3_golden_example(const std::string_view filename) {
  const std::filesystem::path fixture_path =
      std::filesystem::path{BLOB_ROYALE_PROTOCOL_V3_SCHEMA_DIRECTORY} / "examples" / filename;
  std::ifstream input{fixture_path, std::ios::binary};
  if (!input.is_open()) {
    throw std::runtime_error{"failed to open protocol v3 golden example: " + fixture_path.string()};
  }
  std::string contents{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  if (input.bad()) {
    throw std::runtime_error{"failed to read protocol v3 golden example: " + fixture_path.string()};
  }
  return contents;
}

inline void require_json_matches_v3_golden_example(const std::string_view encoded,
                                                   const std::string_view filename) {
  const boost::json::value encoded_value = boost::json::parse(encoded);
  const boost::json::value fixture_value = boost::json::parse(read_v3_golden_example(filename));
  REQUIRE(encoded_value == fixture_value);
}

template <typename Action>
void require_protocol_error_code(Action&& action,
                                 const ProtocolEncodingErrorCode expected_error_code) {
  try {
    std::forward<Action>(action)();
  } catch (const ProtocolEncodingError& error) {
    REQUIRE(error.error_code() == expected_error_code);
    REQUIRE_FALSE(error.context().empty());
    REQUIRE_FALSE(error.detail().empty());
    return;
  }
  FAIL("expected ProtocolEncodingError");
}

} // namespace blob_royale::protocol::v3_test_fixture

#endif
