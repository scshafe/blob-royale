#ifndef BLOB_ROYALE_TESTS_UNIT_PROTOCOL_FIXTURES_STUN_INPUT_GENERATION_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_PROTOCOL_FIXTURES_STUN_INPUT_GENERATION_FIXTURE_HPP

#include "../protocol_v3_test_fixture.hpp"
#include "components/stun_component.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace blob_royale::protocol::stun_input_fixture {

inline constexpr std::uint64_t kEntity = 1;
inline constexpr std::uint64_t kController = 1;
inline constexpr std::uint64_t kActivation = 100;
inline constexpr std::uint64_t kDuration = 40;
inline constexpr std::string_view kDisplayName = "Player 1";

struct RejectedGeneration final {
  std::string_view name;
  std::string_view encoded;
};

inline constexpr std::array kRejectedGenerations{
    RejectedGeneration{"zero", "0"},
    RejectedGeneration{"negative", "-1"},
    RejectedGeneration{"fraction", "1.5"},
    RejectedGeneration{"decimal integer", "1.0"},
    RejectedGeneration{"exponent integer", "1e2"},
    RejectedGeneration{"null", "null"},
    RejectedGeneration{"string", "\"100\""},
    RejectedGeneration{"boolean", "true"},
    RejectedGeneration{"array", "[]"},
    RejectedGeneration{"object", "{}"},
    RejectedGeneration{"unsafe integer", "9007199254740992"},
    RejectedGeneration{"unsigned overflow", "18446744073709551616"}};
inline constexpr std::array<std::uint64_t, 3> kAcceptedGenerations{
    1, kActivation, simulation::TickSequence::kMaximumValue};
inline constexpr std::array<std::string_view, 2> kUnknownMemberPayloads{
    R"({"x":1,"y":0,"generation":100})", R"({"x":1,"y":0,"input_generation":100,"entity":2})"};

[[nodiscard]] inline simulation::Controllable
private_controllable(const std::optional<simulation::TickSequence> generation) {
  const auto direction = simulation::Vector2::create(1.0, 0.0);
  return simulation::Controllable{
      simulation::ControllerId::create(kController),
      {simulation::Command{
          simulation::ThrustCommand{simulation::EntityId::create(kEntity), direction, generation}}},
      direction,
      generation};
}

[[nodiscard]] inline v3_test_fixture::StubControllerDirectory directory() {
  v3_test_fixture::StubControllerDirectory result;
  result.add_controller(kController, "session", kDisplayName);
  return result;
}

// Idle simulation publishes the literal status through the real registry/projection, without a
// gameplay system that could repair the deliberately invalid encoding specimens.
[[nodiscard]] inline simulation::WorldSnapshot
snapshot(const std::optional<simulation::TickSequence> generation,
         const std::optional<simulation::Stun> stun = std::nullopt) {
  auto world = simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(kEntity),
      simulation::PhysicsBody::create(
          simulation::Vector2::create(480.0, 320.0), simulation::Vector2::create(0.0, 0.0),
          simulation::Vector2::create(0.0, 0.0), 10.0, 1.0, 1, 1, false),
      simulation::ControllerId::create(kController))});
  world.mutable_store<simulation::Controllable>().insert_or_assign(
      simulation::EntityId::create(kEntity), private_controllable(generation));
  if (stun.has_value()) {
    world.mutable_store<simulation::Stun>().insert_or_assign(simulation::EntityId::create(kEntity),
                                                             *stun);
  }
  auto game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world));
  for (std::uint64_t tick = 0; tick < kActivation; ++tick) {
    game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  }
  return game.snapshot();
}

} // namespace blob_royale::protocol::stun_input_fixture

#endif
