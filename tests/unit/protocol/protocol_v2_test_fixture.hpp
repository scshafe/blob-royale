#ifndef BLOB_ROYALE_TESTS_UNIT_PROTOCOL_PROTOCOL_V2_TEST_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_PROTOCOL_PROTOCOL_V2_TEST_FIXTURE_HPP

#include "controller_directory_view.hpp"
#include "protocol_encoding_error.hpp"
#include "protocol_v2_json_encoding.hpp"
#include "request_id.hpp"
#include "session_welcome.hpp"

#include "command_kind_mask.hpp"
#include "components/controllable_component.hpp"
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
#include "mode_states/royale_placements_mode_state.hpp"
#include "physics_body.hpp"
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

#ifndef BLOB_ROYALE_PROTOCOL_V2_SCHEMA_DIRECTORY
#error "BLOB_ROYALE_PROTOCOL_V2_SCHEMA_DIRECTORY must name docs/protocol/schema/v2"
#endif

namespace blob_royale::protocol::v2_test_fixture {

namespace simulation = blob_royale::simulation;

// The exact values of `docs/protocol/schema/v2/examples/*.json`, named once so a golden-byte test
// and the world that produces it cannot disagree about what the accepted document says.
inline constexpr std::string_view kSessionRequestId = "018f47a4-9c21-7f10-8a55-4b7d1e0c33a2";
inline constexpr std::string_view kWelcomeTimestamp = "2026-09-06T18:04:11.500Z";
inline constexpr std::string_view kSnapshotTimestamp = "2026-09-06T18:04:17.750Z";
inline constexpr std::uint64_t kSnapshotMessageSequence = 129;
inline constexpr std::uint64_t kGoldenTickSequence = 12'904;
inline constexpr std::uint64_t kGoldenPhaseStartedTick = 10'904;
inline constexpr std::uint64_t kGoldenEliminatedTick = 12'400;
inline constexpr std::uint64_t kGoldenEliminationGraceTicks = 1'200;

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

  [[nodiscard]] simulation::MatchOutcome outcome(const simulation::GameWorld&) const override {
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

  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept override {
    return simulation::CommandKindMask::create({simulation::CommandKind::kThrust});
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

// The NPC kinds the golden welcome publishes, in the order `ControllerRegistry` declares them. It
// is a *fixture* copy rather than a read of the registry: `blob_protocol` does not link
// `blob_controllers`, and the whole point of the member is that the list travels as data.
inline constexpr std::array<std::string_view, 2> kGoldenNpcControllerKinds{"wanderer", "chaser"};

[[nodiscard]] inline std::vector<std::string> golden_npc_controller_kinds() {
  return {std::string{kGoldenNpcControllerKinds[0]}, std::string{kGoldenNpcControllerKinds[1]}};
}

[[nodiscard]] inline SessionWelcome golden_welcome() {
  return SessionWelcome::create(
      simulation::EntityId::create(kPlayerEntityId),
      simulation::ControllerId::create(kPlayerControllerId), std::string{kPlayerDisplayName},
      std::string{kGoldenModeName}, std::string{kGoldenMapName},
      simulation::CommandKindMask::create(
          {simulation::CommandKind::kThrust, simulation::CommandKind::kSetSeatCount,
           simulation::CommandKind::kClearSeat, simulation::CommandKind::kSeatNpc,
           simulation::CommandKind::kStartMatch}),
      golden_npc_controller_kinds());
}

[[nodiscard]] inline std::string read_v2_golden_example(const std::string_view filename) {
  const std::filesystem::path fixture_path =
      std::filesystem::path{BLOB_ROYALE_PROTOCOL_V2_SCHEMA_DIRECTORY} / "examples" / filename;
  std::ifstream input{fixture_path, std::ios::binary};
  if (!input.is_open()) {
    throw std::runtime_error{"failed to open protocol v2 golden example: " + fixture_path.string()};
  }
  std::string contents{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  if (input.bad()) {
    throw std::runtime_error{"failed to read protocol v2 golden example: " + fixture_path.string()};
  }
  return contents;
}

inline void require_json_matches_v2_golden_example(const std::string_view encoded,
                                                   const std::string_view filename) {
  const boost::json::value encoded_value = boost::json::parse(encoded);
  const boost::json::value fixture_value = boost::json::parse(read_v2_golden_example(filename));
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

} // namespace blob_royale::protocol::v2_test_fixture

#endif
