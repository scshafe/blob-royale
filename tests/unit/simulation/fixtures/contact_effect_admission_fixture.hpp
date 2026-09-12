#ifndef BLOB_ROYALE_TESTING_CONTACT_EFFECT_ADMISSION_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_CONTACT_EFFECT_ADMISSION_FIXTURE_HPP

#include "game_world.hpp"

#include <array>
#include <cstdint>

namespace blob_royale::testing::contact_effect_admission_fixture {

inline constexpr std::uint64_t kFirstEntity = 10;
inline constexpr std::uint64_t kSecondEntity = 20;
inline constexpr std::uint64_t kMissingEntity = 30;
inline constexpr auto kInvalidPolicy = static_cast<simulation::ContactEffectPolicy>(73);
inline constexpr std::array kPolicies{simulation::ContactEffectPolicy::kClosingImpact,
                                      simulation::ContactEffectPolicy::kAnyTouch};

[[nodiscard]] inline simulation::EntityId entity(const std::uint64_t value) {
  return simulation::EntityId::create(value);
}

[[nodiscard]] inline simulation::PhysicsBody body() {
  const auto zero = simulation::Vector2::create(0.0, 0.0);
  return simulation::PhysicsBody::create(simulation::Vector2::create(40.0, 40.0), zero, zero)
      .with_radius(2.0);
}

[[nodiscard]] inline simulation::GameWorld world() {
  return simulation::GameWorld::create(
      {simulation::GameWorld::EntitySeed::create(entity(kSecondEntity), body()),
       simulation::GameWorld::EntitySeed::create(entity(kFirstEntity), body())});
}

[[nodiscard]] inline simulation::PhysicsBody static_body() {
  return simulation::PhysicsBody::create_static(simulation::Vector2::create(40.0, 40.0))
      .with_radius(7.0);
}

[[nodiscard]] inline simulation::SimulationConfig configuration() {
  return simulation::SimulationConfig::create(100.0, 100.0, 3.0, 400, 2, 2);
}

} // namespace blob_royale::testing::contact_effect_admission_fixture

#endif
