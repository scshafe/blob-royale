#ifndef BLOB_ROYALE_TESTS_UNIT_PROTOCOL_FIXTURES_CONTACT_EFFECT_ADMISSION_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_PROTOCOL_FIXTURES_CONTACT_EFFECT_ADMISSION_FIXTURE_HPP

#include "../protocol_v3_test_fixture.hpp"
#include "contact_effect_admission.hpp"
#include "fixed_delta.hpp"
#include "input_batch.hpp"

#include <array>
#include <utility>

namespace blob_royale::protocol::contact_effect_admission_fixture {

inline constexpr std::uint64_t kEntity = 1;
inline constexpr std::uint64_t kController = 1;
inline constexpr std::array kInvalidStoredPolicies{
    simulation::ContactEffectPolicy::kClosingImpact,
    static_cast<simulation::ContactEffectPolicy>(255)};

[[nodiscard]] inline simulation::WorldSnapshot snapshot(bool any_touch) {
  const auto entity = simulation::EntityId::create(kEntity);
  auto world = simulation::GameWorld::create({simulation::GameWorld::EntitySeed::create(
      entity,
      simulation::PhysicsBody::create(
          simulation::Vector2::create(480.0, 320.0), simulation::Vector2::create(0.0, 0.0),
          simulation::Vector2::create(0.0, 0.0), 10.0, 1.0, 1, 1, false),
      simulation::ControllerId::create(kController))});
  simulation::assign_contact_effect_policy(world, entity,
                                           any_touch
                                               ? simulation::ContactEffectPolicy::kAnyTouch
                                               : simulation::ContactEffectPolicy::kClosingImpact);
  auto game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world));
  game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  return game.snapshot();
}

} // namespace blob_royale::protocol::contact_effect_admission_fixture

#endif
