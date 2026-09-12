#ifndef BLOB_ROYALE_TESTS_UNIT_PROTOCOL_FIXTURES_SHIELD_ENCODING_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_PROTOCOL_FIXTURES_SHIELD_ENCODING_FIXTURE_HPP

#include "../protocol_v3_test_fixture.hpp"
#include "components/shield_component.hpp"

#include <cstdint>

namespace blob_royale::protocol::shield_fixture {

// `gameplay::AbilityConfiguration::defaults()` in ticks, transcribed rather than linked:
// `blob_protocol_unit_tests` links `blob_protocol` and not `blob_gameplay`, and what this fixture
// needs is a published shape, not a mode's authority over where its numbers came from. The four
// counts are the ones ADR 0008 states for the authored 0.4 / 0.08 / 0.9 / 0.6 second defaults, and
// a build whose conversion disagreed would be caught by the gameplay-side tests that do link it.
inline constexpr std::uint64_t kShieldDurationTicks = 160;
inline constexpr std::uint64_t kPerfectDurationTicks = 32;
inline constexpr std::uint64_t kCooldownDurationTicks = 360;
inline constexpr std::uint64_t kParryStunDurationTicks = 240;

inline constexpr std::uint64_t kEntity = v3_test_fixture::kPlayerEntityId;
inline constexpr std::uint64_t kActivation = 100;

// One shield exactly as `AbilitySystem` would commit it, with every duration overridable so a test
// can reach the legal degenerate tunings -- a zero cooldown, a perfect opening as long as the
// shield -- without a second construction path that could disagree with `activate`'s validation.
[[nodiscard]] inline simulation::Shield
activated(const std::uint64_t activation_tick = kActivation,
          const std::uint64_t shield_duration_ticks = kShieldDurationTicks,
          const std::uint64_t perfect_duration_ticks = kPerfectDurationTicks,
          const std::uint64_t cooldown_duration_ticks = kCooldownDurationTicks,
          const std::uint64_t parry_stun_duration_ticks = kParryStunDurationTicks) {
  return simulation::Shield::activate(simulation::TickSequence::create(activation_tick),
                                      shield_duration_ticks, perfect_duration_ticks,
                                      cooldown_duration_ticks, parry_stun_duration_ticks);
}

// One committed tick carrying a body and its shield, driven by the idle engine: no gameplay system
// is declared, so nothing expires, cancels, or repairs the specimen between construction and the
// snapshot the encoder reads, and the value on the wire is the value the test wrote.
//
// The body is real because `Shield` declares the body-bound lifetime trait. A shield published on
// an entity that never had a body is a state shared respawn would already have swept, so a golden
// built that way would pin bytes for a world that cannot occur. It also puts a second component in
// the entity, which is what makes the ascending component-key order observable at all.
[[nodiscard]] inline simulation::WorldSnapshot
snapshot(const simulation::Shield& shield, const std::uint64_t tick_count = kActivation) {
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(
      simulation::EntityId::create(kEntity),
      simulation::PhysicsBody::create(
          simulation::Vector2::create(480.0, 320.0), simulation::Vector2::create(0.0, 0.0),
          simulation::Vector2::create(0.0, 0.0), 10.0, 1.0, 1, 1, false));
  world.mutable_store<simulation::Shield>().insert_or_assign(simulation::EntityId::create(kEntity),
                                                             shield);
  simulation::GameSimulation game = simulation::GameSimulation::create(
      simulation::SimulationConfig::create(960.0, 640.0, 10.0, 400, 16, 16), std::move(world));
  for (std::uint64_t tick = 0; tick < tick_count; ++tick) {
    game.step(simulation::FixedDelta::canonical(), simulation::InputBatch::empty());
  }
  return game.snapshot();
}

} // namespace blob_royale::protocol::shield_fixture

#endif
