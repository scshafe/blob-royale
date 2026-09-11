#include "contact_impulse_frozen_reference.hpp"
#include "contact_rule.hpp"
#include "contact_rule_table.hpp"
#include "entity_id.hpp"
#include "fixed_delta.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "physics.hpp"
#include "physics_body.hpp"
#include "simulation_config.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "spatial_grid.hpp"
#include "tick_context.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace simulation = blob_royale::simulation;
namespace frozen = blob_royale::testing::contact_impulse_reference;

namespace {

constexpr double kConfiguredRadius = 10.0;

[[nodiscard]] simulation::Vector2 point(const double x, const double y) {
  return simulation::Vector2::create(x, y);
}

[[nodiscard]] simulation::PhysicsBody body(const double x, const double y, const double vx,
                                           const double vy) {
  return simulation::PhysicsBody::create(point(x, y), point(vx, vy), point(0.0, 0.0));
}

struct PairCase final {
  std::string_view name;
  simulation::PhysicsBody first;
  simulation::PhysicsBody second;
};

[[nodiscard]] std::vector<PairCase> pair_cases() {
  return {
      {"axial approach", body(100.0, 100.0, 3.0, 1.0), body(120.0, 100.0, -2.0, 0.5)},
      {"three-four-five normal", body(100.0, 100.0, 7.0, -2.0), body(112.0, 116.0, -4.0, -1.0)},
      {"irrational oblique normal", body(100.0, 100.0, 0.1, 7.0), body(113.0, 111.0, -0.3, -5.0)},
      {"negative normal", body(150.0, 160.0, -3.0, -4.0), body(138.0, 144.0, 1.0, 2.0)},
      {"noncontact approach", body(100.0, 100.0, 3.0, 1.0), body(121.0, 100.0, -2.0, 0.5)},
      {"separating", body(100.0, 100.0, -3.0, 1.0), body(119.0, 100.0, 4.0, -2.0)},
      {"coincident moving", body(100.0, 100.0, 3.0, -4.0), body(100.0, 100.0, -2.0, 5.0)},
      {"coincident stationary relative motion", body(100.0, 100.0, 3.0, -4.0),
       body(100.0, 100.0, 3.0, -4.0)},
      {"signed zero and cancellation", body(100.0, 100.0, 1.0, -0.0),
       body(120.0, 100.0, -0.0, 0.0)},
  };
}

void check_bits(const double actual, const double expected) {
  CHECK(std::bit_cast<std::uint64_t>(actual) == std::bit_cast<std::uint64_t>(expected));
}

void check_vector_bits(const simulation::Vector2& actual, const simulation::Vector2& expected) {
  check_bits(actual.x(), expected.x());
  check_bits(actual.y(), expected.y());
}

template <typename Expected>
void check_collision_bits(const simulation::PlayerPairCollisionResult& actual,
                          const Expected& expected) {
  CHECK(actual.contact().is_contact() == expected.contact().is_contact());
  check_vector_bits(actual.contact().normal(), expected.contact().normal());
  check_bits(actual.contact().center_distance(), expected.contact().center_distance());
  check_bits(actual.contact().relative_normal_speed(), expected.contact().relative_normal_speed());
  CHECK(actual.impulse_applied() == expected.impulse_applied());
  check_vector_bits(actual.first_velocity(), expected.first_velocity());
  check_vector_bits(actual.second_velocity(), expected.second_velocity());
}

// Phase A calls the still-original radius implementations here. After delegation, the frozen
// 638da1b reference remains independent: wrapper-vs-helper equality is not the sole oracle.
void check_player_promotion(const simulation::PhysicsBody& first,
                            const simulation::PhysicsBody& second) {
  const auto contact = simulation::detect_player_pair_contact(first, second, kConfiguredRadius);
  const auto original = simulation::resolve_player_pair_collision(first, second, kConfiguredRadius);
  const auto promoted = simulation::resolve_player_pair_collision(first, second, contact);
  const auto reference = frozen::resolve_player_pair_collision(first, second, contact);
  check_collision_bits(promoted, original);
  check_collision_bits(original, reference);
  check_collision_bits(promoted, reference);
}

void check_general_promotion(const simulation::PhysicsBody& first,
                             const simulation::PhysicsBody& second) {
  const auto contact = simulation::detect_pair_contact(
      first, second, simulation::pair_contact_distance(first, second, kConfiguredRadius));
  const auto original =
      simulation::resolve_general_pair_collision(first, second, kConfiguredRadius);
  const auto promoted = simulation::resolve_general_pair_collision(first, second, contact);
  const auto reference = frozen::resolve_general_pair_collision(first, second, contact);
  check_collision_bits(promoted, original);
  check_collision_bits(original, reference);
  check_collision_bits(promoted, reference);
}

struct Rejection final {
  simulation::SimulationValidationCode code;
  std::string context;
  std::string detail;

  friend bool operator==(const Rejection&, const Rejection&) = default;
};

template <typename Operation> [[nodiscard]] Rejection rejection_from(Operation&& operation) {
  std::optional<Rejection> rejection;
  try {
    static_cast<void>(std::forward<Operation>(operation)());
  } catch (const simulation::SimulationValidationError& error) {
    rejection.emplace(Rejection{error.validation_code(), error.context(), error.detail()});
  }
  REQUIRE(rejection.has_value());
  return *rejection;
}

} // namespace

TEST_CASE("contact-taking baseline impulse preserves original and frozen binary64 results",
          "[unit][simulation][physics][promotion]") {
  for (const PairCase& test : pair_cases()) {
    INFO(test.name);
    check_player_promotion(test.first, test.second);
    check_player_promotion(test.second, test.first);
  }
}

TEST_CASE("contact-taking general impulse preserves original and frozen mass equations",
          "[unit][simulation][physics][promotion]") {
  for (const PairCase& test : pair_cases()) {
    INFO(test.name);
    check_general_promotion(test.first, test.second);
    const auto first = test.first.with_mass(7.0).with_restitution(0.75).with_radius(13.0);
    const auto second = test.second.with_mass(3.0).with_restitution(0.25).with_radius(7.0);
    check_general_promotion(first, second);
    check_general_promotion(second, first);
  }
  check_general_promotion(body(100.0, 100.0, 4.0, 2.0).with_mass(1e-12).with_restitution(0.0),
                          body(119.0, 100.0, -3.0, -1.0).with_mass(1e12));
  check_general_promotion(body(100.0, 100.0, 4.0, 2.0).with_mass(9.0).with_radius(26.0),
                          body(132.0, 100.0, -3.0, -1.0).with_radius(10.0));
}

TEST_CASE("contact impulse promotion retains the exact separating tolerance boundary",
          "[unit][simulation][physics][promotion]") {
  // The accepted absolute-plus-relative guard changes at epsilon/(1-relative_epsilon), not
  // epsilon alone. Adjacent doubles on both sides exercise its written rounding order.
  const double threshold = simulation::kVelocityTolerance / (1.0 - simulation::kRelativeTolerance);
  const double infinity = std::numeric_limits<double>::infinity();
  for (const double speed :
       {0.0, simulation::kVelocityTolerance,
        std::nextafter(simulation::kVelocityTolerance, infinity), std::nextafter(threshold, 0.0),
        threshold, std::nextafter(threshold, infinity), 2.0 * simulation::kVelocityTolerance}) {
    INFO("approach speed " << speed);
    const auto first = body(100.0, 100.0, speed, 0.0);
    const auto second = body(120.0, 100.0, 0.0, -0.0);
    check_player_promotion(first, second);
    check_general_promotion(first.with_mass(7.0).with_restitution(0.25), second.with_mass(3.0));
  }
}

TEST_CASE("contact-taking helpers trust the supplied contact without repeating detection",
          "[unit][simulation][physics][promotion]") {
  const auto first = body(100.0, 100.0, 3.0, 1.0).with_radius(50.0);
  const auto second = body(200.0, 100.0, -2.0, 0.5).with_radius(50.0);
  const auto certified_contact = simulation::detect_pair_contact(first, second, 100.0);
  // These unit-mass discs touch at their authored radii, far outside the legacy configured-size
  // predicate. A supplied hit is sufficient for either equation; neither takes a fallback radius.
  CHECK_FALSE(simulation::resolve_player_pair_collision(first, second, kConfiguredRadius)
                  .impulse_applied());
  const auto player = simulation::resolve_player_pair_collision(first, second, certified_contact);
  const auto general = simulation::resolve_general_pair_collision(first, second, certified_contact);
  CHECK(player.impulse_applied());
  CHECK(general.impulse_applied());
  check_collision_bits(player,
                       frozen::resolve_player_pair_collision(first, second, certified_contact));
  check_collision_bits(general,
                       frozen::resolve_general_pair_collision(first, second, certified_contact));
}

TEST_CASE("general impulse promotion preserves finite-result rejection codes and contexts",
          "[unit][simulation][physics][promotion][validation]") {
  struct MassCase final {
    double first;
    double second;
    std::string_view context;
  };
  for (const MassCase& masses : std::array{
           MassCase{1e-310, 1.0, "physics.resolve_general_pair_collision.inverse_first_mass"},
           MassCase{1.0, 1e-310, "physics.resolve_general_pair_collision.inverse_second_mass"},
           MassCase{1e-308, 1e-308, "physics.resolve_general_pair_collision.inverse_mass_sum"}}) {
    INFO(masses.context);
    const auto first = body(100.0, 100.0, 3.0, 0.0).with_mass(masses.first);
    const auto second = body(119.0, 100.0, -2.0, 0.0).with_mass(masses.second);
    const auto contact = simulation::detect_player_pair_contact(first, second, kConfiguredRadius);
    const auto original = rejection_from([&] {
      return simulation::resolve_general_pair_collision(first, second, kConfiguredRadius);
    });
    const auto promoted = rejection_from(
        [&] { return simulation::resolve_general_pair_collision(first, second, contact); });
    const auto reference = rejection_from(
        [&] { return frozen::resolve_general_pair_collision(first, second, contact); });
    CHECK(original.code == simulation::SimulationValidationCode::kPhysicalScalarNotFinite);
    CHECK(original.context == masses.context);
    CHECK(promoted == original);
    CHECK(promoted == reference);
  }
}

TEST_CASE("radius wrappers retain their legacy validation before contact impulse delegation",
          "[unit][simulation][physics][promotion][validation]") {
  const auto first = body(100.0, 100.0, 3.0, 0.0);
  const auto second = body(120.0, 100.0, -2.0, 0.0);
  for (const double radius :
       {0.0, -1.0, simulation::kMaximumPhysicalComponentMagnitude * 2.0,
        std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
    const auto player = rejection_from(
        [&] { return simulation::resolve_player_pair_collision(first, second, radius); });
    const auto general = rejection_from(
        [&] { return simulation::resolve_general_pair_collision(first, second, radius); });
    const auto expected = std::isfinite(radius)
                              ? simulation::SimulationValidationCode::kPhysicalScalarOutOfRange
                              : simulation::SimulationValidationCode::kPhysicalScalarNotFinite;
    CHECK(player.code == expected);
    CHECK(player.context == "physics.player_pair.player_radius");
    CHECK(general.code == expected);
    CHECK(general.context == "physics.pair_contact_distance.configured_radius");
  }
}

TEST_CASE("static velocity extraction matches the actual old response and frozen reflection",
          "[unit][simulation][physics][promotion]") {
  const auto configuration = simulation::SimulationConfig::create(
      500.0, 500.0, kConfiguredRadius, simulation::SimulationConfig::kRequiredTicksPerSecond, 10,
      10);
  const auto map =
      simulation::MapDefinition::bare_arena(simulation::ArenaBounds::create(500.0, 500.0));
  const auto world =
      simulation::GameWorld::create(std::vector<simulation::GameWorld::EntitySeed>{});
  const auto grid = simulation::SpatialGrid::create(configuration, map.bounds(), world);
  const auto context = simulation::TickContext::create(simulation::TickSequence::create(1),
                                                       simulation::FixedDelta::canonical(),
                                                       configuration, map, grid);
  for (const PairCase& test : pair_cases()) {
    INFO(test.name);
    const auto wall = simulation::PhysicsBody::create_static(test.second.position());
    const auto contact =
        simulation::detect_player_pair_contact(test.first, wall, kConfiguredRadius);
    const auto original = simulation::reflect_static_response(
        {simulation::EntityId::create(1), test.first}, {simulation::EntityId::create(2), wall},
        contact, context);
    const auto promoted =
        simulation::reflect_static_contact_velocity(test.first.velocity(), contact.normal());
    const auto reference =
        frozen::reflect_static_contact_velocity(test.first.velocity(), contact.normal());
    REQUIRE(original.replaces_bodies());
    check_vector_bits(promoted, original.first_body().velocity());
    check_vector_bits(promoted, reference);
    CHECK(original.second_body() == wall);
    CHECK(original.first_body().position() == test.first.position());
    CHECK(original.first_body().acceleration() == test.first.acceleration());
  }
}
