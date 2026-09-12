#ifndef BLOB_ROYALE_TESTS_APPLICATION_PROFILED_BOT_RECONCILIATION_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_APPLICATION_PROFILED_BOT_RECONCILIATION_FIXTURE_HPP

#include "bot_reconciliation.hpp"
#include "command_mailbox.hpp"
#include "entity_id_allocator.hpp"
#include "fixed_delta.hpp"
#include "game_simulation.hpp"
#include "game_simulation_setup.hpp"
#include "input_batch.hpp"
#include "npc_catalogue.hpp"
#include "structured_log_capture.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace blob_royale::application::profiled_bot_fixture {

inline constexpr std::uint64_t kRawMatchSeed = 23;
inline constexpr std::uint64_t kLobbyId = 4;
inline constexpr std::uint64_t kLegacyRoomSeed = kRawMatchSeed + kLobbyId - 1;
inline constexpr std::uint64_t kFirstControllerId = 101;
inline constexpr std::string_view kSteady = "steady";
inline constexpr std::string_view kQuick = "quick";
inline constexpr std::string_view kUnknown = "unconfigured";

[[nodiscard]] inline simulation::NpcDeclaration
declaration(const std::string_view profile = kSteady, const std::string_view kind = "tactical") {
  return {simulation::SeatKindName::create(kind), simulation::BotProfileName::create(profile)};
}

// Two whole sections, written positionally so that a new `TacticalProfile::Section` member is
// visible here as a missing argument rather than as a value. The five weights cannot go short --
// `AuthoredObjectiveWeight` has no default constructor -- but the two trailing combat scalars can,
// and a value-initialized zero is legal for both, so they are authored rather than left off.
// Nothing in reconciliation reads them; they are here to keep this fixture a complete section.
[[nodiscard]] inline controllers::TacticalProfileCatalogue profiles() {
  return controllers::TacticalProfileCatalogue::create(
      {controllers::TacticalProfile::create(
           {std::string{kSteady}, 1, 80, 0.05, 400, {1.0, 1.0, 1.0, 1.0, 1.0}, 0.5, 80, 0.25, 8}),
       controllers::TacticalProfile::create(
           {std::string{kQuick}, 1, 0, 0, 0, {1.0, 1.0, 1.0, 1.0, 1.0}, 0.5, 0, 0.25, 8})});
}

[[nodiscard]] inline simulation::NpcCatalogue catalogue() {
  return simulation::NpcCatalogue::create({"wanderer", "chaser"},
                                          {declaration(kSteady), declaration(kQuick)});
}

[[nodiscard]] inline simulation::SeatRoster
roster(const simulation::NpcDeclaration& declared,
       const std::optional<simulation::ControllerId> controller = std::nullopt) {
  auto seats = simulation::SeatRoster::of_size(2);
  seats.assign_seat(
      1, simulation::Seat{simulation::NpcSeat{declared.kind, controller, declared.profile_name}});
  return seats;
}

[[nodiscard]] inline simulation::MapDefinition map() {
  return simulation::MapDefinition::create(
      "profiled_reconciliation", simulation::ArenaBounds::create(500, 500), {},
      {simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(100, 250)),
       simulation::MapDefinition::Marker::spawn(simulation::Vector2::create(400, 250))},
      simulation::MapMetadata::none());
}

// No worker thread or wall-clock waits: reconciliation sees authored committed snapshots and the
// test is the mailbox's only drainer, so a replacement can deterministically precede its old join.
[[nodiscard]] inline simulation::GameSimulation game(simulation::SeatRoster seats) {
  auto world = simulation::GameWorld::create({});
  world.mutable_match().seats = std::move(seats);
  return simulation::GameSimulation::create(
      simulation::SimulationConfig::create(500, 500, 10, 400, 8, 8), std::move(world),
      simulation::GameSimulationSetup::engine_defaults().with_map(map()));
}

inline void step(simulation::GameSimulation& game, std::vector<simulation::Command> commands) {
  game.step(simulation::FixedDelta::canonical(),
            simulation::InputBatch::create(std::move(commands), game.accepted_command_kinds(),
                                           simulation::EntityIdReservation::none()));
}

struct Harness final {
  Harness()
      : mailbox(simulation::CommandKindMask::all()),
        allocator(runtime::EntityIdAllocator::create(simulation::EntityId::create(100))),
        publication(game(simulation::SeatRoster::of_size(2)).snapshot()),
        sink(mailbox, directory, allocator, kFirstControllerId, catalogue()),
        host(publication, sink),
        reconciler(sink, host, kLegacyRoomSeed, kRawMatchSeed, kLobbyId, profiles(), logs.logger) {}

  void observe(const simulation::SeatRoster& seats, const bool abandoned = false) {
    reconciler.reconcile(game(seats).snapshot(), abandoned);
  }

  void observe_at(const simulation::SeatRoster& seats, const std::uint64_t tick) {
    auto simulation = game(seats);
    for (std::uint64_t index = 0; index < tick; ++index) {
      simulation.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
    }
    reconciler.reconcile(simulation.snapshot());
  }

  [[nodiscard]] std::size_t event_count(const std::string_view event) const {
    std::size_t count = 0;
    for (const auto& record : logs.events()) {
      count += record.event == event ? 1 : 0;
    }
    return count;
  }

  runtime::CommandMailbox mailbox;
  runtime::ControllerDirectory directory;
  runtime::EntityIdAllocator allocator;
  runtime::SnapshotPublication publication;
  runtime::CommandSink sink;
  controllers::ControllerHost host;
  test_support::StructuredLogCapture logs;
  SeatBotReconciler reconciler;
};

} // namespace blob_royale::application::profiled_bot_fixture

#endif
