#ifndef BLOB_ROYALE_TESTING_COMPONENT_LIFETIME_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_COMPONENT_LIFETIME_FIXTURE_HPP

#include "components/charge_component.hpp"
#include "components/contact_effect_admission_component.hpp"
#include "components/hill_component.hpp"
#include "components/hill_motion_component.hpp"
#include "components/hill_presence_component.hpp"
#include "components/race_progress_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "components/score_component.hpp"
#include "components/shield_component.hpp"
#include "components/stun_component.hpp"
#include "components/zone_exposure_component.hpp"
#include "game_world.hpp"

#include <array>
#include <cstdint>

namespace blob_royale::testing::component_lifetime_fixture {

inline constexpr std::array<std::uint64_t, 8> kMixedEntities{1, 2, 3, 4, 5, 6, 7, 8};
inline constexpr std::array<std::uint64_t, 2> kLiveEntities{3, 7};
inline constexpr std::uint64_t kNonparticipantBody = 3;
inline constexpr std::uint64_t kLivePlayer = 7;
inline constexpr std::uint64_t kBoundOnlyEntity = 8;
inline constexpr std::uint64_t kHillEntity = 90;
inline constexpr std::uint64_t kPresenceTicks = 2;
inline constexpr std::uint64_t kExposureTicks = 4;
inline constexpr std::uint64_t kReturnTicks = 3;
inline constexpr std::uint64_t kNextCheckpoint = 2;
inline constexpr std::int64_t kEarnedScore = 11;
inline constexpr std::uint64_t kStunActivation = 1;
inline constexpr std::uint64_t kStunDuration = 50;
inline constexpr std::uint64_t kShieldActivation = 1;
inline constexpr std::uint64_t kShieldDuration = 40;
inline constexpr std::uint64_t kShieldPerfectDuration = 8;
inline constexpr std::uint64_t kShieldCooldownDuration = 90;
inline constexpr std::uint64_t kShieldParryStunDuration = 60;
inline constexpr std::uint64_t kChargeActivation = 1;
inline constexpr std::uint64_t kChargeCooldownDuration = 120;

[[nodiscard]] inline simulation::EntityId entity(const std::uint64_t value) {
  return simulation::EntityId::create(value);
}

[[nodiscard]] inline simulation::Vector2 zero() { return simulation::Vector2::create(0.0, 0.0); }

[[nodiscard]] inline simulation::PhysicsBody body() {
  return simulation::PhysicsBody::create(simulation::Vector2::create(100.0, 100.0), zero(), zero());
}

inline void attach_bound_components(simulation::GameWorld& world,
                                    const simulation::EntityId target) {
  world.mutable_store<simulation::ContactEffectAdmission>().insert_or_assign(
      target, simulation::ContactEffectAdmission{});
  world.mutable_store<simulation::HillPresence>().insert_or_assign(
      target, simulation::HillPresence{kPresenceTicks});
  world.mutable_store<simulation::ZoneExposure>().insert_or_assign(
      target, simulation::ZoneExposure{kExposureTicks});
  world.mutable_store<simulation::Stun>().insert_or_assign(
      target, simulation::Stun{simulation::TickWindow::create(
                  simulation::TickSequence::create(kStunActivation), kStunDuration)});
  // A shield outlives neither its body nor a round, so the generic sweep tests must actually see
  // one: without an attached value they would pass while proving nothing about the new kind.
  world.mutable_store<simulation::Shield>().insert_or_assign(
      target, simulation::Shield::activate(simulation::TickSequence::create(kShieldActivation),
                                           kShieldDuration, kShieldPerfectDuration,
                                           kShieldCooldownDuration, kShieldParryStunDuration));
  // A charge cooldown outlives neither its body nor a round either, so the generic sweep tests must
  // see one attached: without a value the body-bound branch would visit the kind and prove nothing.
  world.mutable_store<simulation::Charge>().insert_or_assign(
      target, simulation::Charge::activate(simulation::TickSequence::create(kChargeActivation),
                                           kChargeCooldownDuration));
}

// Adjacent bodyless entries before, between, and after live entries expose index skipping. One
// live entry is not a participant; the last bodyless entity consists solely of bound state.
[[nodiscard]] inline simulation::GameWorld mixed_world() {
  auto world = simulation::GameWorld::create({});
  for (const auto id : kMixedEntities) {
    attach_bound_components(world, entity(id));
    if (id != kBoundOnlyEntity) {
      world.mutable_store<simulation::Score>().insert_or_assign(entity(id),
                                                                simulation::Score{kEarnedScore});
      world.mutable_store<simulation::RaceProgress>().insert_or_assign(
          entity(id), simulation::RaceProgress{kNextCheckpoint});
      world.mutable_store<simulation::RespawnTimer>().insert_or_assign(
          entity(id), simulation::RespawnTimer{kReturnTicks});
    }
    if (id != kBoundOnlyEntity && id != kNonparticipantBody) {
      world.mutable_store<simulation::Controllable>().insert_or_assign(
          entity(id), simulation::Controllable{simulation::ControllerId::create(id)});
    }
  }
  for (const auto id : kLiveEntities) {
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity(id), body());
  }
  world.mutable_store<simulation::Hill>().insert_or_assign(
      entity(kHillEntity), simulation::Hill{simulation::Vector2::create(200.0, 200.0), 25.0});
  world.mutable_store<simulation::HillMotion>().insert_or_assign(
      entity(kHillEntity),
      simulation::HillMotion{simulation::Vector2::create(20.0, -30.0),
                             simulation::HillMotionSchedule{simulation::TickSequence::create(140),
                                                            simulation::RandomStreamKind::kHill}});
  return world;
}

} // namespace blob_royale::testing::component_lifetime_fixture

#endif
