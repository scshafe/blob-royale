#include "shared/create_crossing_hazard.hpp"

#include "fixtures/contact_effect_hazard_fixture.hpp"
#include "fixtures/crossing_hazard_creation_frozen_reference.hpp"
#include "fixtures/hazard_stream_frozen_reference.hpp"
#include "gameplay_test_fixture.hpp"

#include "components/crossing_hazard_component.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "components/lifetime_component.hpp"
#include "components/score_component.hpp"
#include "contact_effect_admission.hpp"
#include "shared/hazard_crossing.hpp"
#include "shared/hazard_spawn_system.hpp"
#include "simulation_system.hpp"
#include "simulation_validation_error.hpp"
#include "system_pipeline.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;
namespace frozen = blob_royale::testing::crossing_hazard_creation_reference;
namespace frozen_crossing = blob_royale::testing::hazard_stream_reference;
namespace frozen_random = blob_royale::testing::deterministic_random_reference;
namespace policy_fixture = blob_royale::testing::contact_effect_hazard_fixture;

namespace {

using Proof = void (*)(const simulation::GameWorld&, const simulation::TickContext&);

// The real kernel alone opens reservations. Running the proof within its lifecycle stage lets it
// compare the full uncommitted world, not just a snapshot that erased reservation/event state.
// Proofs mutate local copies only, so even capacity/failure fixtures never reach the live kernel.
class CrossingCreationProofSystem final : public simulation::SimulationSystem {
public:
  CrossingCreationProofSystem(const Proof proof, bool& observed)
      : proof_(proof), observed_(observed) {}

  [[nodiscard]] std::string_view name() const noexcept override {
    return "crossing_hazard_creation_proof";
  }

  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override {
    observed_ = true;
    proof_(world, context);
  }

private:
  const Proof proof_;
  bool& observed_;
};

void run_proof(const Proof proof, const std::uint64_t seed) {
  const auto configuration = testing::gameplay_configuration();
  auto map = testing::gameplay_map(4);
  auto world = simulation::GameWorld::create(configuration, map, seed);
  world.mutable_match().phase = simulation::MatchPhase::kRunning;
  bool observed = false;
  std::vector<simulation::SystemPipeline::StagedSystem> systems;
  systems.push_back({simulation::SystemStage::kLifecycle,
                     std::make_unique<const CrossingCreationProofSystem>(proof, observed)});
  auto game = simulation::GameSimulation::create(
      configuration, std::move(world),
      simulation::GameSimulationSetup::engine_defaults()
          .with_map(std::move(map))
          .with_systems(simulation::SystemPipeline::create(std::move(systems))));
  game.step(testing::kGameplayFixedDelta,
            simulation::InputBatch::create(
                {}, game.accepted_command_kinds(),
                simulation::EntityIdReservation::create(
                    simulation::EntityId::create(frozen::kFirstEntity), frozen::kReservationSize)));
  REQUIRE(observed);
}

void check_bits(const double actual, const double expected) {
  CHECK(std::bit_cast<std::uint64_t>(actual) == std::bit_cast<std::uint64_t>(expected));
}

void check_body_bits(const simulation::PhysicsBody& actual,
                     const simulation::PhysicsBody& expected) {
  check_bits(actual.position().x(), expected.position().x());
  check_bits(actual.position().y(), expected.position().y());
  check_bits(actual.velocity().x(), expected.velocity().x());
  check_bits(actual.velocity().y(), expected.velocity().y());
  check_bits(actual.acceleration().x(), expected.acceleration().x());
  check_bits(actual.acceleration().y(), expected.acceleration().y());
  check_bits(actual.radius(), expected.radius());
  check_bits(actual.mass(), expected.mass());
  check_bits(actual.restitution(), expected.restitution());
  check_bits(actual.drag_scale(), expected.drag_scale());
  CHECK(actual == expected);
}

// The old constructor remains frozen. The new contract adds one speed draw even at zero
// variation and one body-bound membership marker; all old geometry/body/lifetime fields stay exact.
constexpr std::uint64_t kCreatedHazardDrawCount = 4;
constexpr std::uint64_t kPriorDrawsBeforeCertainBirth = 129;

[[nodiscard]] simulation::EntityId
adapted_frozen_creation(simulation::GameWorld& world, const simulation::ArenaBounds& bounds,
                        const gameplay::HazardArchetype& archetype, const double seconds_per_tick) {
  static_cast<void>(world.random(simulation::RandomStreamKind::kHazards).next_unit_interval());
  const auto entity = frozen::create_hazard(world, bounds, archetype, seconds_per_tick);
  world.mutable_store<simulation::CrossingHazard>().insert_or_assign(entity,
                                                                     simulation::CrossingHazard{});
  return entity;
}

void enable_birth(simulation::GameWorld& world) {
  const auto previous = world.match().movement.current;
  world.mutable_match().movement.current =
      simulation::MovementTuning::create(previous.acceleration(), previous.normal_top_speed(),
                                         previous.charge_speed_fraction(), 5.0, 5.0);
  for (std::uint64_t draw = 0; draw < kPriorDrawsBeforeCertainBirth; ++draw) {
    static_cast<void>(world.random(simulation::RandomStreamKind::kHazards).next_bits());
  }
}

void prove_creation_sequence(const simulation::GameWorld& initial,
                             const simulation::TickContext& context) {
  auto expected = initial;
  for (std::uint64_t draw = 0; draw < frozen::kPriorHazardDrawCount; ++draw) {
    static_cast<void>(expected.random(simulation::RandomStreamKind::kHazards).next_bits());
  }
  for (std::uint64_t draw = 0; draw < frozen::kPriorHillDrawCount; ++draw) {
    static_cast<void>(expected.random(simulation::RandomStreamKind::kHill).next_bits());
  }
  auto actual = expected;
  const auto unchanged_hill = actual.random(simulation::RandomStreamKind::kHill);
  std::uint64_t births = 0;
  for (std::size_t repetition = 0; repetition < frozen::kSequenceRepetitions; ++repetition) {
    for (const auto& archetype : frozen::archetypes()) {
      CAPTURE(repetition, archetype.kind_name());
      const auto expected_entity = adapted_frozen_creation(
          expected, context.map().bounds(), archetype, context.fixed_delta().seconds());
      const auto actual_entity = gameplay::create_crossing_hazard(
          actual, context.map().bounds(), archetype, context.fixed_delta().seconds());
      CHECK(actual_entity == expected_entity);
      CHECK(actual_entity.value() == frozen::kFirstEntity + births);
      ++births;
      // Structural equality includes every store, match, event, stream's hidden state and count,
      // and the remaining reservation. Check binary64 fields explicitly as well, including zeros.
      REQUIRE(actual == expected);
      const auto* actual_body = actual.store<simulation::PhysicsBody>().find(actual_entity);
      const auto* expected_body = expected.store<simulation::PhysicsBody>().find(expected_entity);
      REQUIRE(actual_body != nullptr);
      REQUIRE(expected_body != nullptr);
      check_body_bits(*actual_body, *expected_body);
      REQUIRE(actual.store<simulation::Lifetime>().find(actual_entity) != nullptr);
      CHECK((actual.store<simulation::LethalOnContact>().find(actual_entity) != nullptr) ==
            archetype.lethal_on_contact());
      CHECK(actual.entity_id_reservation().count() == frozen::kReservationSize - births);
      CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
            frozen::kPriorHazardDrawCount + births * kCreatedHazardDrawCount);
      CHECK(actual.random(simulation::RandomStreamKind::kHill) == unchanged_hill);
    }
  }
  auto actual_random = actual.random(simulation::RandomStreamKind::kHazards);
  auto expected_random = expected.random(simulation::RandomStreamKind::kHazards);
  CHECK(actual_random.next_bits() == expected_random.next_bits());
}

void prove_independent_frozen_crossing(const simulation::GameWorld& initial,
                                       const simulation::TickContext& context) {
  auto actual = initial;
  auto random = frozen_random::DeterministicRandom::create(frozen_crossing::kSeed);
  const auto archetype = frozen::archetypes().front();
  for (std::uint64_t birth = 0; birth < frozen::kReservationSize; ++birth) {
    static_cast<void>(random.next_unit_interval());
    const auto expected = frozen_crossing::draw_crossing(random);
    const auto entity = gameplay::create_crossing_hazard(actual, context.map().bounds(), archetype,
                                                         context.fixed_delta().seconds());
    const auto* body = actual.store<simulation::PhysicsBody>().find(entity);
    const auto* lifetime = actual.store<simulation::Lifetime>().find(entity);
    REQUIRE(body != nullptr);
    REQUIRE(lifetime != nullptr);
    CHECK(entity.value() == frozen::kFirstEntity + birth);
    check_bits(body->position().x(), expected.position.x);
    check_bits(body->position().y(), expected.position.y);
    check_bits(body->velocity().x(), expected.velocity.x);
    check_bits(body->velocity().y(), expected.velocity.y);
    CHECK(lifetime->ticks_remaining == expected.lifetime_ticks);
    CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
          random.draw_count());
  }
  CHECK(actual.entity_id_reservation().empty());
  auto actual_random = actual.random(simulation::RandomStreamKind::kHazards);
  CHECK(actual_random.next_bits() == random.next_bits());
}

void prove_sampled_speed_lifetime(const simulation::GameWorld& initial,
                                  const simulation::TickContext& context) {
  auto actual = initial;
  auto random = frozen_random::DeterministicRandom::create(frozen_crossing::kSeed);
  const auto archetype = gameplay::HazardArchetype::create(
      {"varied_meteorite", frozen_crossing::kRadius, frozen_crossing::kMass,
       frozen_crossing::kRestitution, frozen_crossing::kSpeed,
       frozen_crossing::kFirstIntervalSeconds, true,
       simulation::ContactEffectPolicy::kClosingImpact, 0.5});
  for (std::uint64_t birth = 0; birth < frozen::kReservationSize; ++birth) {
    const double sampled_speed = frozen_crossing::kSpeed * (0.5 + random.next_unit_interval());
    const auto expected = frozen_crossing::draw_crossing(random);
    const auto entity = gameplay::create_crossing_hazard(actual, context.map().bounds(), archetype,
                                                         context.fixed_delta().seconds());
    const auto* body = actual.store<simulation::PhysicsBody>().find(entity);
    const auto* lifetime = actual.store<simulation::Lifetime>().find(entity);
    REQUIRE(body != nullptr);
    REQUIRE(lifetime != nullptr);
    check_bits(body->position().x(), expected.position.x);
    check_bits(body->position().y(), expected.position.y);
    CHECK(std::hypot(body->velocity().x(), body->velocity().y()) == Catch::Approx(sampled_speed));
    CHECK(lifetime->ticks_remaining ==
          static_cast<std::uint64_t>(std::ceil(expected.travel_distance / sampled_speed /
                                               frozen_crossing::kSecondsPerTick)));
    CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
          random.draw_count());
    REQUIRE(actual.store<simulation::CrossingHazard>().find(entity) != nullptr);
  }
}

void prove_schedule(const simulation::GameWorld& initial, const simulation::TickContext&) {
  const auto archetypes = frozen::archetypes();
  const gameplay::HazardSpawnSystem production(archetypes);
  constexpr std::array phases{simulation::MatchPhase::kLobby, simulation::MatchPhase::kCountdown,
                              simulation::MatchPhase::kRunning, simulation::MatchPhase::kEnded};
  constexpr std::array remaining_counts{std::uint64_t{0}, std::uint64_t{1},
                                        frozen::kReservationSize};
  constexpr std::array ticks{frozen::kNotDueTick, frozen::kCommonDueTick};
  for (const auto phase : phases) {
    for (const auto remaining : remaining_counts) {
      for (const auto tick : ticks) {
        auto actual = initial;
        enable_birth(actual);
        actual.mutable_match().phase = phase;
        while (actual.entity_id_reservation().count() > remaining) {
          static_cast<void>(actual.create_entity());
        }
        const auto before = actual;
        const testing::TickHarness harness(simulation::TickSequence::create(tick));
        production.apply(actual, harness.context());
        CAPTURE(phase, remaining, tick);
        const std::uint64_t births =
            phase == simulation::MatchPhase::kRunning && remaining != 0 ? 1 : 0;
        CHECK(actual.store<simulation::PhysicsBody>().size() == births);
        CHECK(actual.store<simulation::CrossingHazard>().size() == births);
        CHECK(actual.entity_id_reservation().count() == remaining - births);
        CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
              kPriorDrawsBeforeCertainBirth + births * 6);
        if (births == 0)
          CHECK(actual == before);
      }
    }
  }
}

void prove_exhausted_creation(const simulation::GameWorld& initial,
                              const simulation::TickContext& context) {
  auto before = initial;
  while (!before.entity_id_reservation().empty()) {
    static_cast<void>(before.create_entity());
  }
  auto expected = before;
  auto actual = before;
  const auto archetype = frozen::archetypes().front();
  REQUIRE_THROWS_AS(adapted_frozen_creation(expected, context.map().bounds(), archetype,
                                            context.fixed_delta().seconds()),
                    simulation::SimulationValidationError);
  try {
    static_cast<void>(gameplay::create_crossing_hazard(actual, context.map().bounds(), archetype,
                                                       context.fixed_delta().seconds()));
    FAIL("the caller's missing reservation must fail, not silently skip the hazard");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() ==
          simulation::SimulationValidationCode::kEntityIdReservationExhausted);
  }
  CHECK(actual == expected);
  CHECK(actual.entities().empty());
  // The caller still owns the pre-draw reservation check; direct creation samples speed and
  // geometry before the missing reservation fails.
  CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
        kCreatedHazardDrawCount);
}

void prove_body_capacity(const simulation::GameWorld& initial, const simulation::TickContext&) {
  const gameplay::HazardSpawnSystem production(frozen::archetypes());
  const testing::TickHarness harness(simulation::TickSequence::create(frozen::kCommonDueTick));
  constexpr std::array available_slots{std::size_t{0}, std::size_t{1}};
  for (const auto available : available_slots) {
    auto actual = initial;
    enable_birth(actual);
    for (std::size_t index = 0; index < simulation::kMaximumMotionBodyCount - available; ++index) {
      actual.mutable_store<simulation::PhysicsBody>().insert_or_assign(
          simulation::EntityId::create(frozen::kFirstCapacityFixtureEntity + index),
          frozen::capacity_fixture_body());
    }
    const auto before = actual;
    production.apply(actual, harness.context());
    CAPTURE(available);
    CHECK(actual.store<simulation::PhysicsBody>().size() == simulation::kMaximumMotionBodyCount);
    CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
          kPriorDrawsBeforeCertainBirth + available * 6);
    CHECK(actual.entity_id_reservation().count() == frozen::kReservationSize - available);
    CHECK(actual.store<simulation::CrossingHazard>().size() == available);
    if (available == 0)
      CHECK(actual == before);
  }
}

void prove_crossing_capacity(const simulation::GameWorld& initial,
                             const simulation::TickContext& context) {
  const gameplay::HazardSpawnSystem production(frozen::archetypes());
  for (const std::size_t available : {std::size_t{0}, std::size_t{1}}) {
    auto actual = initial;
    enable_birth(actual);
    for (std::size_t index = 0; index < gameplay::kMaximumActiveCrossingHazardCount - available;
         ++index) {
      const auto entity = simulation::EntityId::create(frozen::kFirstCapacityFixtureEntity + index);
      actual.mutable_store<simulation::PhysicsBody>().insert_or_assign(
          entity, frozen::capacity_fixture_body());
      actual.mutable_store<simulation::CrossingHazard>().insert_or_assign(
          entity, simulation::CrossingHazard{});
    }
    const auto before = actual;
    production.apply(actual, context);
    CHECK(actual.store<simulation::CrossingHazard>().size() ==
          gameplay::kMaximumActiveCrossingHazardCount);
    CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
          kPriorDrawsBeforeCertainBirth + available * 6);
    if (available == 0) {
      CHECK(actual == before);
      const auto removed = simulation::EntityId::create(frozen::kFirstCapacityFixtureEntity);
      actual.mutable_store<simulation::PhysicsBody>().erase(removed);
      actual.erase_body_bound_components_without_body();
      REQUIRE(actual.store<simulation::CrossingHazard>().size() ==
              gameplay::kMaximumActiveCrossingHazardCount - 1);
      production.apply(actual, context);
      CHECK(actual.store<simulation::CrossingHazard>().size() ==
            gameplay::kMaximumActiveCrossingHazardCount);
      CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
            kPriorDrawsBeforeCertainBirth + 6);
    }
  }
}

void prove_entity_capacity(const simulation::GameWorld& initial,
                           const simulation::TickContext& context) {
  const gameplay::HazardSpawnSystem production(frozen::archetypes());
  for (const std::size_t available : {std::size_t{0}, std::size_t{1}}) {
    auto actual = initial;
    enable_birth(actual);
    for (std::size_t index = 0; index < simulation::kMaximumEntityCount - available; ++index) {
      actual.mutable_store<simulation::Score>().insert_or_assign(
          simulation::EntityId::create(frozen::kFirstCapacityFixtureEntity + index),
          simulation::Score{});
    }
    const auto before = actual;
    production.apply(actual, context);
    CHECK(actual.entities().size() == simulation::kMaximumEntityCount);
    CHECK(actual.store<simulation::PhysicsBody>().size() == available);
    CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
          kPriorDrawsBeforeCertainBirth + available * 6);
    if (available == 0)
      CHECK(actual == before);
  }
}

void prove_disabled_classes(const simulation::GameWorld& initial,
                            const simulation::TickContext& context) {
  const auto table = frozen::archetypes();
  const auto defaults = simulation::MovementTuning::defaults();
  for (const auto& declared :
       std::vector<std::vector<gameplay::HazardArchetype>>{{}, {table[0]}, {table[1]}, table}) {
    for (const auto rates :
         std::array{std::array{0.0, 0.0}, std::array{5.0, 0.0}, std::array{0.0, 5.0}}) {
      bool eligible = false;
      for (const auto& archetype : declared) {
        eligible = eligible || rates[archetype.lethal_on_contact() ? 0 : 1] > 0.0;
      }
      if (eligible)
        continue;
      auto actual = initial;
      actual.mutable_match().movement.current =
          simulation::MovementTuning::create(defaults.acceleration(), defaults.normal_top_speed(),
                                             defaults.charge_speed_fraction(), rates[0], rates[1]);
      const auto before = actual;
      gameplay::HazardSpawnSystem(declared).apply(actual, context);
      CHECK(actual == before);
    }
  }
  auto actual = initial;
  static_cast<void>(gameplay::create_crossing_hazard(actual, context.map().bounds(), table.front(),
                                                     context.fixed_delta().seconds()));
  const auto before = actual;
  gameplay::HazardSpawnSystem(table).apply(actual, context);
  CHECK(actual == before); // Zero rates preserve the existing body's sampled motion and lifetime.
}

void prove_class_and_kind_selection(const simulation::GameWorld& initial,
                                    const simulation::TickContext& context) {
  const auto defaults = simulation::MovementTuning::defaults();
  const auto table = frozen::archetypes();
  for (const bool lethal : {false, true}) {
    auto actual = initial;
    enable_birth(actual);
    actual.mutable_match().movement.current = simulation::MovementTuning::create(
        defaults.acceleration(), defaults.normal_top_speed(), defaults.charge_speed_fraction(),
        lethal ? 5.0 : 0.0, lethal ? 0.0 : 5.0);
    gameplay::HazardSpawnSystem(table).apply(actual, context);
    REQUIRE(actual.store<simulation::CrossingHazard>().size() == 1);
    CHECK(actual.store<simulation::LethalOnContact>().size() == (lethal ? 1 : 0));
    CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
          kPriorDrawsBeforeCertainBirth + 6);
  }
  // The independently pinned kind draw is 0.9152873011755797. Equal weights select the second;
  // a 20:380 tick interval mixture makes the first kind's share 95%, so the very same draw selects
  // it.
  for (const double second_interval : {0.05, 0.95}) {
    auto actual = initial;
    enable_birth(actual);
    const auto second = gameplay::HazardArchetype::create(
        {"rare_meteorite", 7.0, 40.0, 0.2, 200.0, second_interval, true});
    gameplay::HazardSpawnSystem({table.front(), second}).apply(actual, context);
    REQUIRE(actual.store<simulation::PhysicsBody>().size() == 1);
    CHECK(actual.store<simulation::PhysicsBody>().entries().front().value.radius() ==
          (second_interval == 0.05 ? 7.0 : table.front().radius()));
    CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
          kPriorDrawsBeforeCertainBirth + 6);
  }
}

void prove_instance_policy(const simulation::GameWorld& initial,
                           const simulation::TickContext& context) {
  auto actual = initial;
  auto expected = initial;
  for (const auto archetype_policy : policy_fixture::kPolicies) {
    const auto archetype = policy_fixture::archetype(archetype_policy);
    for (const auto instance : policy_fixture::kOverrides) {
      const auto expected_entity = adapted_frozen_creation(
          expected, context.map().bounds(), archetype, context.fixed_delta().seconds());
      const auto entity = gameplay::create_crossing_hazard(
          actual, context.map().bounds(), archetype, context.fixed_delta().seconds(), instance);
      const auto policy = instance.value_or(archetype_policy);
      if (policy == simulation::ContactEffectPolicy::kAnyTouch) {
        expected.mutable_store<simulation::ContactEffectAdmission>().insert_or_assign(
            expected_entity, simulation::ContactEffectAdmission{});
      }
      REQUIRE(actual == expected);
      CHECK(simulation::effective_contact_effect_policy(actual, entity) == policy);
    }
  }
  const auto before = actual;
  CHECK_THROWS_AS(gameplay::create_crossing_hazard(
                      actual, context.map().bounds(),
                      policy_fixture::archetype(simulation::ContactEffectPolicy::kAnyTouch),
                      context.fixed_delta().seconds(), policy_fixture::kInvalidPolicy),
                  simulation::SimulationValidationError);
  CHECK(actual == before);
}

void prove_scheduled_policy(const simulation::GameWorld& initial, const simulation::TickContext&) {
  for (const auto policy : policy_fixture::kPolicies) {
    auto actual = initial;
    enable_birth(actual);
    auto expected = actual;
    const auto archetype = policy_fixture::archetype(policy);
    const testing::TickHarness harness(simulation::TickSequence::create(frozen::kCommonDueTick));
    const gameplay::HazardSpawnSystem production({archetype});
    production.apply(actual, harness.context());
    static_cast<void>(expected.random(simulation::RandomStreamKind::kHazards).next_unit_interval());
    static_cast<void>(expected.random(simulation::RandomStreamKind::kHazards).next_unit_interval());
    const auto entity = adapted_frozen_creation(expected, harness.map().bounds(), archetype,
                                                harness.context().fixed_delta().seconds());
    if (policy == simulation::ContactEffectPolicy::kAnyTouch) {
      expected.mutable_store<simulation::ContactEffectAdmission>().insert_or_assign(
          entity, simulation::ContactEffectAdmission{});
    }
    CHECK(actual == expected);
    CHECK(simulation::effective_contact_effect_policy(actual, entity) == policy);
  }
}

} // namespace

TEST_CASE("crossing hazard creation preserves frozen physics with explicit speed draw and "
          "population marker",
          "[unit][gameplay][shared][create_crossing_hazard][promotion]") {
  for (const auto seed : frozen::kSeeds) {
    CAPTURE(seed);
    run_proof(prove_creation_sequence, seed);
  }
}

TEST_CASE("crossing hazard creation matches the independent frozen seed2026 crossing sequence",
          "[unit][gameplay][shared][create_crossing_hazard][promotion]") {
  run_proof(prove_independent_frozen_crossing, frozen_crossing::kSeed);
}

TEST_CASE(
    "random crossing births ignore old cadence and preserve pre-draw phase and reservation skips",
    "[unit][gameplay][shared][create_crossing_hazard][promotion]") {
  run_proof(prove_schedule, frozen_crossing::kSeed);
}

TEST_CASE("crossing hazard creation preserves the old exhausted-reservation failure boundary",
          "[unit][gameplay][shared][create_crossing_hazard][promotion]") {
  run_proof(prove_exhausted_creation, frozen_crossing::kSeed);
}

TEST_CASE("crossing hazard creation respects pre-draw motion capacity and the final body slot",
          "[unit][gameplay][shared][create_crossing_hazard][promotion]") {
  run_proof(prove_body_capacity, frozen_crossing::kSeed);
}

TEST_CASE(
    "crossing hazard instance policies override archetypes without changing RNG body or lifetime",
    "[unit][gameplay][shared][create_crossing_hazard][contact_effect_admission]") {
  run_proof(prove_instance_policy, frozen_crossing::kSeed);
}

TEST_CASE("scheduled hazards inherit their archetype contact effect admission",
          "[unit][gameplay][shared][create_crossing_hazard][contact_effect_admission]") {
  run_proof(prove_scheduled_policy, frozen_crossing::kSeed);
}

TEST_CASE("crossing hazard lifetime uses its independently sampled speed without resampling",
          "[unit][gameplay][shared][create_crossing_hazard][hazard_crossing]") {
  run_proof(prove_sampled_speed_lifetime, frozen_crossing::kSeed);
}

TEST_CASE("hazard speed sampling is bounded varied and consumes one draw at zero variation",
          "[unit][gameplay][shared][hazard_crossing][determinism]") {
  for (const double variation : {0.0, 0.5, 0.9}) {
    const auto archetype = gameplay::HazardArchetype::create(
        {"varied_meteorite", frozen_crossing::kRadius, frozen_crossing::kMass,
         frozen_crossing::kRestitution, frozen_crossing::kSpeed,
         frozen_crossing::kFirstIntervalSeconds, true,
         simulation::ContactEffectPolicy::kClosingImpact, variation});
    auto random = simulation::DeterministicRandom::create(frozen_crossing::kSeed);
    double minimum = archetype.speed();
    double maximum = archetype.speed();
    constexpr std::uint64_t kSamples = 1000;
    for (std::uint64_t sample = 0; sample < kSamples; ++sample) {
      const double speed = gameplay::draw_hazard_speed(random, archetype);
      CHECK(speed >= archetype.speed() * (1.0 - variation));
      CHECK(speed <= archetype.speed() * (1.0 + variation));
      minimum = std::min(minimum, speed);
      maximum = std::max(maximum, speed);
    }
    CHECK(random.draw_count() == kSamples);
    if (variation == 0.0) {
      CHECK(minimum == archetype.speed());
      CHECK(maximum == archetype.speed());
    } else {
      CHECK(minimum < archetype.speed() * (1.0 - variation * 0.9));
      CHECK(maximum > archetype.speed() * (1.0 + variation * 0.9));
    }
  }
}

TEST_CASE("random hazard bursts stop at the explicit crossing cap and body cleanup frees capacity",
          "[unit][gameplay][shared][hazard_spawn][capacity]") {
  run_proof(prove_crossing_capacity, frozen_crossing::kSeed);
}

TEST_CASE("random hazard births respect the total entity cap even when bodies have headroom",
          "[unit][gameplay][shared][hazard_spawn][capacity]") {
  run_proof(prove_entity_capacity, frozen_crossing::kSeed);
}

TEST_CASE("zero rates and absent classes consume no random draws and preserve existing hazards",
          "[unit][gameplay][shared][hazard_spawn]") {
  run_proof(prove_disabled_classes, frozen_crossing::kSeed);
}

TEST_CASE("random hazard births select enabled classes and authored reciprocal interval weights",
          "[unit][gameplay][shared][hazard_spawn][determinism]") {
  run_proof(prove_class_and_kind_selection, frozen_crossing::kSeed);
}
