#ifndef BLOB_ROYALE_TESTS_UNIT_PROTOCOL_PROTOCOL_TEST_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_PROTOCOL_PROTOCOL_TEST_FIXTURE_HPP

#include "entity_id.hpp"
#include "fixed_delta.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "http_error.hpp"
#include "physics_body.hpp"
#include "protocol_encoding_error.hpp"
#include "protocol_json_encoding.hpp"
#include "public_configuration.hpp"
#include "request_id.hpp"
#include "simulation_config.hpp"
#include "vector2.hpp"
#include "world_snapshot.hpp"

#include <boost/json/parse.hpp>
#include <boost/json/value.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef BLOB_ROYALE_PROTOCOL_SCHEMA_DIRECTORY
#error "BLOB_ROYALE_PROTOCOL_SCHEMA_DIRECTORY must name docs/protocol/schema/v1"
#endif

namespace blob_royale::protocol::test_fixture {

namespace simulation = blob_royale::simulation;

inline constexpr std::string_view kConfigurationRequestId = "018f47a4-5d5b-7b86-bd4a-273c2dd6f4ee";
inline constexpr std::string_view kErrorRequestId = "018f47a4-66c3-79b4-91ac-ecb672f93fc0";
inline constexpr std::string_view kLivenessRequestId = "probe-live-0001";
inline constexpr std::string_view kReadinessRequestId = "probe-ready-0001";
inline constexpr std::string_view kSnapshotRequestId = "018f47a4-70b7-77c8-aa51-f91265a9bb2f";
inline constexpr std::string_view kSnapshotTimestamp = "2026-08-05T19:42:17.125Z";

struct PlayerValues final {
  std::uint64_t entity_id;
  double position_x;
  double position_y;
  double velocity_x;
  double velocity_y;
  double acceleration_x;
  double acceleration_y;
};

inline constexpr PlayerValues kFirstGoldenPlayer{1, 240.0, 300.0, 0.0, 0.0, 0.0, 0.0};
inline constexpr PlayerValues kSecondGoldenPlayer{2, 240.0, 320.0, 0.0, 0.0, 0.0, 0.0};

[[nodiscard]] inline RequestId request_id(const std::string_view value) {
  return decode_request_id(value);
}

[[nodiscard]] inline PublicConfiguration golden_configuration() {
  return PublicConfiguration::create(960.0, 640.0, 10.0, 30);
}

[[nodiscard]] inline simulation::GameWorld::EntitySeed player(const PlayerValues& values) {
  return simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(values.entity_id),
      simulation::PhysicsBody::create(
          simulation::Vector2::create(values.position_x, values.position_y),
          simulation::Vector2::create(values.velocity_x, values.velocity_y),
          simulation::Vector2::create(values.acceleration_x, values.acceleration_y)));
}

[[nodiscard]] inline simulation::GameWorld::EntitySeed zero_player(const std::uint64_t entity_id) {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  return simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(entity_id), simulation::PhysicsBody::create(zero, zero, zero));
}

[[nodiscard]] inline simulation::GameWorld::EntitySeed
stationary_player(const std::uint64_t entity_id, const double position_x, const double position_y) {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);
  return simulation::GameWorld::EntitySeed::create(
      simulation::EntityId::create(entity_id),
      simulation::PhysicsBody::create(simulation::Vector2::create(position_x, position_y), zero,
                                      zero));
}

[[nodiscard]] inline simulation::SimulationConfig default_simulation_config() {
  return simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16);
}

[[nodiscard]] inline simulation::WorldSnapshot
snapshot_after_steps(simulation::SimulationConfig configuration,
                     std::vector<simulation::GameWorld::EntitySeed> players,
                     const std::size_t step_count) {
  simulation::GameSimulation game_simulation = simulation::GameSimulation::create(
      std::move(configuration), simulation::GameWorld::create(std::move(players)));
  for (std::size_t step = 0; step < step_count; ++step) {
    game_simulation.step(simulation::FixedDelta::canonical());
  }
  return game_simulation.snapshot();
}

[[nodiscard]] inline simulation::WorldSnapshot golden_snapshot() {
  return snapshot_after_steps(default_simulation_config(),
                              {player(kFirstGoldenPlayer), player(kSecondGoldenPlayer)}, 1'601);
}

[[nodiscard]] inline simulation::WorldSnapshot empty_snapshot() {
  return snapshot_after_steps(default_simulation_config(), {}, 1);
}

[[nodiscard]] inline simulation::WorldSnapshot maximum_player_snapshot() {
  std::vector<simulation::GameWorld::EntitySeed> players;
  players.reserve(kSnapshotPlayerLimit);
  for (std::size_t index = 0; index < kSnapshotPlayerLimit; ++index) {
    constexpr std::size_t kPlayersPerRow = 64;
    constexpr double kCellExtent = 1'000.0 / static_cast<double>(kPlayersPerRow);
    const std::size_t column = index % kPlayersPerRow;
    const std::size_t row = index / kPlayersPerRow;
    players.push_back(stationary_player(static_cast<std::uint64_t>(index + 1),
                                        (static_cast<double>(column) + 0.5) * kCellExtent,
                                        (static_cast<double>(row) + 0.5) * kCellExtent));
  }
  return snapshot_after_steps(
      simulation::SimulationConfig::create(1'000.0, 1'000.0, 1.0, 400, 64, 64), std::move(players),
      1);
}

[[nodiscard]] inline std::string read_golden_example(const std::string_view filename) {
  const std::filesystem::path fixture_path =
      std::filesystem::path{BLOB_ROYALE_PROTOCOL_SCHEMA_DIRECTORY} / "examples" / filename;
  std::ifstream input{fixture_path, std::ios::binary};
  if (!input.is_open()) {
    throw std::runtime_error{"failed to open protocol golden example: " + fixture_path.string()};
  }
  std::string contents{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  if (input.bad()) {
    throw std::runtime_error{"failed to read protocol golden example: " + fixture_path.string()};
  }
  return contents;
}

inline void require_json_matches_golden_example(const std::string_view encoded,
                                                const std::string_view filename) {
  const boost::json::value encoded_value = boost::json::parse(encoded);
  const boost::json::value fixture_value = boost::json::parse(read_golden_example(filename));
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

} // namespace blob_royale::protocol::test_fixture

#endif
