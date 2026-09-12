#ifndef BLOB_ROYALE_TESTS_UNIT_GAMEPLAY_FIXTURES_FALLING_MODE_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_GAMEPLAY_FIXTURES_FALLING_MODE_FIXTURE_HPP

#include "components/contact_effect_admission_component.hpp"
#include "components/controllable_component.hpp"
#include "contact_effect_admission.hpp"
#include "gameplay_test_fixture.hpp"
#include "simulation_tolerance.hpp"

#include <memory>
#include <utility>

namespace blob_royale::testing::falling_mode_fixture {

// canonical: falling_mode_fixture -- a player crosses darkness before reaching its nearby peer.
// Safe markers at x=100/200 support ordinary returns; the pit's center crossing is x=120, before
// contact with the second player at x=140. Bodyless hill/zone entities receive distinct reserved
// IDs.
inline simulation::Vector2 point(double x, double y) { return simulation::Vector2::create(x, y); }
inline simulation::EntityId entity(std::uint64_t id) { return simulation::EntityId::create(id); }

inline simulation::MapDefinition map() {
  return simulation::MapDefinition::create(
      "falling_mode",
      simulation::TerrainDefinition::create(
          simulation::ArenaBounds::create(960, 640), simulation::TerrainGround::kSolid, {},
          {simulation::TerrainHole::create("pit", point(130, 320),
                                           10 + simulation::kPositionTolerance)}),
      {},
      {simulation::MapDefinition::Marker::spawn(point(100, 320)),
       simulation::MapDefinition::Marker::spawn(point(200, 320)),
       simulation::MapDefinition::Marker::create("hill", point(500, 320), std::nullopt,
                                                 simulation::MapMetadata::none())},
      simulation::MapMetadata::none());
}

inline simulation::GameWorld
world(simulation::MatchPhase phase = simulation::MatchPhase::kRunning) {
  const auto body = [](double x, double speed) {
    return simulation::PhysicsBody::create(point(x, 320), point(speed, 0), point(0, 0))
        .with_radius(10)
        .with_ground_attachment(simulation::GroundAttachment::kGroundBound);
  };
  auto value = simulation::GameWorld::create(
      {simulation::GameWorld::EntitySeed::create(entity(1), body(100, 20'000),
                                                 simulation::ControllerId::create(1)),
       simulation::GameWorld::EntitySeed::create(entity(2), body(160, 0),
                                                 simulation::ControllerId::create(2))});
  value.mutable_match().phase = phase;
  value.mutable_match().previous_phase = phase;
  value.mutable_store<simulation::Controllable>()
      .mutable_find(entity(1))
      ->normalized_thrust_intent = point(1, 0);
  simulation::assign_contact_effect_policy(value, entity(1),
                                           simulation::ContactEffectPolicy::kAnyTouch);
  return value;
}

inline simulation::GameSimulation
game(std::unique_ptr<const simulation::GameMode> mode,
     simulation::MatchPhase phase = simulation::MatchPhase::kRunning) {
  return simulation::GameSimulation::create(
      gameplay_configuration(), world(phase),
      simulation::GameSimulationSetup::of_mode(map(), std::move(mode)));
}

inline simulation::WorldSnapshot step(simulation::GameSimulation& game,
                                      std::vector<simulation::Command> commands = {}) {
  game.step(
      simulation::FixedDelta::canonical(),
      gameplay_batch(game, std::move(commands), 1'000 + game.tick_sequence().value() * 32, 32));
  return game.snapshot();
}

template <class Component>
const Component* component(const simulation::WorldSnapshot& snapshot, std::uint64_t id) {
  for (const auto& entry : snapshot.components<Component>()) {
    if (entry.entity == entity(id)) {
      return &entry.value;
    }
  }
  return nullptr;
}

} // namespace blob_royale::testing::falling_mode_fixture

#endif
