#ifndef BLOB_ROYALE_TESTS_UNIT_PROTOCOL_FIXTURES_CHARGE_ENCODING_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_PROTOCOL_FIXTURES_CHARGE_ENCODING_FIXTURE_HPP

#include "../protocol_v3_test_fixture.hpp"
#include "components/charge_component.hpp"

#include <cstdint>

namespace blob_royale::protocol::charge_fixture {

// `gameplay::AbilityConfiguration::defaults()` in ticks, transcribed rather than linked, for the
// reason `shield_encoding_fixture.hpp` gives beside its own four counts: `blob_protocol_unit_tests`
// links `blob_protocol` and not `blob_gameplay`, and what this fixture needs is a published shape
// rather than a mode's authority over where its number came from. 480 is ADR 0008's authored 1.2
// second cooldown at 400 Hz, and a build whose conversion disagreed would be caught by the
// gameplay-side tests that do link it.
inline constexpr std::uint64_t kCooldownDurationTicks = 480;

inline constexpr std::uint64_t kEntity = v3_test_fixture::kPlayerEntityId;
inline constexpr std::uint64_t kActivation = 100;

// One charge exactly as `AbilitySystem` would commit it. The cooldown is overridable so a case can
// reach the top of the tick domain without a second construction path that could disagree with
// `activate`'s validation -- and, unlike the shield fixture's five knobs, that is the only value
// there is to vary: a one-shot activation owns no protection window and captures no effect
// parameter (`docs/reviews/2026-09-12-charge-contract.md` § "The component and the command").
[[nodiscard]] inline simulation::Charge
activated(const std::uint64_t activation_tick = kActivation,
          const std::uint64_t cooldown_duration_ticks = kCooldownDurationTicks) {
  return simulation::Charge::activate(simulation::TickSequence::create(activation_tick),
                                      cooldown_duration_ticks);
}

// One committed tick carrying a body and its charge, driven by the idle engine: no gameplay system
// is declared, so nothing expires or sweeps the specimen between construction and the snapshot the
// encoder reads, and the value on the wire is the value the test wrote.
//
// The body is real because `Charge` declares the body-bound lifetime trait. A charge published on
// an entity that never had a body is a state shared respawn would already have swept, so a golden
// built that way would pin bytes for a world that cannot occur. It also puts a second component in
// the entity, which is what makes the ascending component-key order observable at all -- and here
// that order is the interesting half, because `"charge"` sorts *before* `"physics_body"` where
// `"shield"` sorts after it.
[[nodiscard]] inline simulation::WorldSnapshot
snapshot(const simulation::Charge& charge, const std::uint64_t tick_count = kActivation) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      simulation::EntityId::create(kEntity),
      simulation::PhysicsBody::create(
          simulation::Vector2::create(480.0, 320.0), simulation::Vector2::create(0.0, 0.0),
          simulation::Vector2::create(0.0, 0.0), 10.0, 1.0, 1, 1, false));
  world.mutable_store<simulation::Charge>().insert_or_assign(simulation::EntityId::create(kEntity),
                                                             charge);
  simulation::GameSimulation game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world));
  for (std::uint64_t tick = 0; tick < tick_count; ++tick) {
    game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  }
  return game.snapshot();
}

} // namespace blob_royale::protocol::charge_fixture

#endif
