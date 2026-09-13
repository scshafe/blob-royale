#include "shared/charge_contact_system.hpp"

#include "components/charge_component.hpp"
#include "components/stun_component.hpp"
#include "events/charge_contact_event.hpp"
#include "events/stun_request_event.hpp"
#include "gameplay_test_fixture.hpp"
#include "shared/status_system.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr std::uint64_t kActivation = 3;
constexpr std::uint64_t kContactTick = 7;
constexpr std::uint64_t kCooldown = 480;
constexpr std::uint64_t kActive = 200;
constexpr std::uint64_t kHitStun = 240;
constexpr std::uint64_t kSecondHitStun = 120;
constexpr std::uint64_t kAttacker = 1;
constexpr std::uint64_t kTarget = 2;
constexpr std::uint64_t kOtherTarget = 3;
constexpr double kCarriedVelocity = 90.0;

[[nodiscard]] simulation::EntityId entity(const std::uint64_t id) {
  return simulation::EntityId::create(id);
}

[[nodiscard]] simulation::TickSequence tick(const std::uint64_t value = kContactTick) {
  return simulation::TickSequence::create(value);
}

[[nodiscard]] simulation::Vector2 vector(const double x, const double y = 0.0) {
  return simulation::Vector2::create(x, y);
}

[[nodiscard]] simulation::Charge charge(const std::uint64_t stun = kHitStun,
                                        const std::uint64_t cooldown = kCooldown) {
  return simulation::Charge::activate(tick(kActivation), cooldown, kActive, stun);
}

[[nodiscard]] simulation::GameWorld world_with_players() {
  std::vector<simulation::GameWorld::EntitySeed> seeds;
  for (const auto id : {kAttacker, kTarget, kOtherTarget}) {
    seeds.push_back(simulation::GameWorld::EntitySeed::create(
        entity(id),
        simulation::PhysicsBody::create(vector(100.0 * static_cast<double>(id), 100.0),
                                        vector(kCarriedVelocity), vector(5.0)),
        simulation::ControllerId::create(id)));
  }
  auto world = simulation::GameWorld::create(std::move(seeds));
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  world.mutable_store<simulation::Charge>().insert_or_assign(entity(kAttacker), charge());
  return world;
}

[[nodiscard]] simulation::ChargeContactCandidate
candidate(const std::uint64_t attacker = kAttacker, const std::uint64_t target = kTarget,
          const simulation::ChargeContactOutcome outcome =
              simulation::ChargeContactOutcome::kSuccessfulHit,
          const std::uint64_t activation = kActivation) {
  return {entity(attacker), entity(target), tick(activation), outcome};
}

[[nodiscard]] std::vector<simulation::StunRequest> requests(const simulation::GameWorld& world) {
  std::vector<simulation::StunRequest> result;
  for (const auto& event : world.events()) {
    if (const auto* request = std::get_if<simulation::StunRequest>(&event); request != nullptr) {
      result.push_back(*request);
    }
  }
  return result;
}

void commit_contacts_and_status(simulation::GameWorld& world) {
  const testing::TickHarness harness{tick()};
  gameplay::ChargeContactSystem::create()->apply(world, harness.context());
  gameplay::StatusSystem::create()->apply(world, harness.context());
}

} // namespace

TEST_CASE("a successful charge refunds immediately and stuns without stopping the moving target",
          "[unit][gameplay][charge_contact]") {
  auto world = world_with_players();
  auto* target = world.mutable_store<simulation::Controllable>().mutable_find(entity(kTarget));
  target->normalized_thrust_intent = vector(1.0);
  target->braking_intent = true;
  world.mutable_store<simulation::Charge>().insert_or_assign(entity(kTarget),
                                                             charge(kSecondHitStun));
  world.emit(candidate());
  commit_contacts_and_status(world);
  CHECK(world.store<simulation::Charge>().find(entity(kAttacker)) == nullptr);
  CHECK(requests(world) == std::vector<simulation::StunRequest>{{entity(kTarget), kHitStun}});
  const auto* stun = world.store<simulation::Stun>().find(entity(kTarget));
  REQUIRE(stun != nullptr);
  CHECK(stun->window.activation_tick() == tick());
  CHECK(stun->window.expiry_tick() == tick(kContactTick + kHitStun));
  CHECK(world.store<simulation::PhysicsBody>().find(entity(kTarget))->velocity() ==
        vector(kCarriedVelocity));
  CHECK(world.store<simulation::PhysicsBody>().find(entity(kTarget))->acceleration() ==
        vector(0.0));
  CHECK(target->normalized_thrust_intent == vector(0.0));
  CHECK_FALSE(target->braking_intent);
  CHECK(target->input_generation == tick());
  REQUIRE(world.store<simulation::Charge>().find(entity(kTarget)) != nullptr);
  CHECK(*world.store<simulation::Charge>().find(entity(kTarget)) ==
        charge(kSecondHitStun).canceled_at(tick()));
}

TEST_CASE("only the first contact candidate for an activation can stun one target",
          "[unit][gameplay][charge_contact]") {
  auto world = world_with_players();
  for (const auto target : {kOtherTarget, kTarget, kOtherTarget}) {
    world.emit(candidate(kAttacker, target));
  }
  const testing::TickHarness harness{tick()};
  const auto system = gameplay::ChargeContactSystem::create();
  CHECK(system->name() == "charge_contact");
  system->apply(world, harness.context());
  CHECK(requests(world) == std::vector<simulation::StunRequest>{{entity(kOtherTarget), kHitStun}});
  CHECK(world.store<simulation::Charge>().find(entity(kAttacker)) == nullptr);
  // Applying the consumer again cannot recover the already consumed attempt.
  system->apply(world, harness.context());
  CHECK(requests(world).size() == 1);
}

TEST_CASE("a first shield block consumes active charge without refund or later target stun",
          "[unit][gameplay][charge_contact][shield]") {
  auto world = world_with_players();
  world.emit(candidate(kAttacker, kTarget, simulation::ChargeContactOutcome::kBlockedByShield));
  world.emit(candidate(kAttacker, kOtherTarget));
  const auto velocity = world.store<simulation::PhysicsBody>().find(entity(kAttacker))->velocity();
  commit_contacts_and_status(world);
  CHECK(requests(world).empty());
  REQUIRE(world.store<simulation::Charge>().find(entity(kAttacker)) != nullptr);
  CHECK(*world.store<simulation::Charge>().find(entity(kAttacker)) == charge().canceled_at(tick()));
  CHECK(world.store<simulation::PhysicsBody>().find(entity(kAttacker))->velocity() == velocity);
  CHECK(world.store<simulation::Stun>().empty());
}

TEST_CASE("mutual charge hits refund both and apply their captured stuns symmetrically",
          "[unit][gameplay][charge_contact][symmetry]") {
  for (const bool reversed : {false, true}) {
    auto world = world_with_players();
    world.mutable_store<simulation::Charge>().insert_or_assign(entity(kTarget),
                                                               charge(kSecondHitStun));
    world.emit(reversed ? candidate(kTarget, kAttacker) : candidate());
    world.emit(reversed ? candidate() : candidate(kTarget, kAttacker));
    commit_contacts_and_status(world);
    CHECK(world.store<simulation::Charge>().empty());
    REQUIRE(world.store<simulation::Stun>().find(entity(kAttacker)) != nullptr);
    REQUIRE(world.store<simulation::Stun>().find(entity(kTarget)) != nullptr);
    CHECK(world.store<simulation::Stun>().find(entity(kAttacker))->window.expiry_tick() ==
          tick(kContactTick + kSecondHitStun));
    CHECK(world.store<simulation::Stun>().find(entity(kTarget))->window.expiry_tick() ==
          tick(kContactTick + kHitStun));
    CHECK(world.store<simulation::PhysicsBody>().find(entity(kAttacker))->velocity() ==
          vector(kCarriedVelocity));
    CHECK(world.store<simulation::PhysicsBody>().find(entity(kTarget))->velocity() ==
          vector(kCarriedVelocity));
  }
}

TEST_CASE("stale charge candidates cannot consume a replacement activation",
          "[unit][gameplay][charge_contact]") {
  auto world = world_with_players();
  world.emit(candidate(kAttacker, kTarget, simulation::ChargeContactOutcome::kSuccessfulHit,
                       kActivation - 1));
  const auto before = world;
  const testing::TickHarness harness{tick()};
  gameplay::ChargeContactSystem::create()->apply(world, harness.context());
  CHECK(world == before);
  world.emit(candidate(kAttacker, kOtherTarget));
  gameplay::ChargeContactSystem::create()->apply(world, harness.context());
  CHECK(requests(world) == std::vector<simulation::StunRequest>{{entity(kOtherTarget), kHitStun}});
}

TEST_CASE("charge candidates require active time rather than remaining cooldown",
          "[unit][gameplay][charge_contact]") {
  auto cooling = world_with_players();
  cooling.mutable_store<simulation::Charge>().insert_or_assign(entity(kAttacker),
                                                               charge().canceled_at(tick()));
  cooling.emit(candidate());
  const auto before = cooling;
  const testing::TickHarness harness{tick()};
  gameplay::ChargeContactSystem::create()->apply(cooling, harness.context());
  CHECK(cooling == before);

  auto naturally_ready = world_with_players();
  naturally_ready.mutable_store<simulation::Charge>().insert_or_assign(entity(kAttacker),
                                                                       charge(kHitStun, 1));
  naturally_ready.emit(candidate());
  gameplay::ChargeContactSystem::create()->apply(naturally_ready, harness.context());
  CHECK(requests(naturally_ready) ==
        std::vector<simulation::StunRequest>{{entity(kTarget), kHitStun}});
  CHECK(naturally_ready.store<simulation::Charge>().empty());
}
