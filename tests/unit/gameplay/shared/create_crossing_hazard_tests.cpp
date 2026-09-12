#include "shared/create_crossing_hazard.hpp"

#include "fixtures/contact_effect_hazard_fixture.hpp"
#include "fixtures/crossing_hazard_creation_frozen_reference.hpp"
#include "fixtures/hazard_stream_frozen_reference.hpp"
#include "gameplay_test_fixture.hpp"

#include "components/lethal_on_contact_component.hpp"
#include "components/lifetime_component.hpp"
#include "contact_effect_admission.hpp"
#include "shared/hazard_spawn_system.hpp"
#include "simulation_system.hpp"
#include "simulation_validation_error.hpp"
#include "system_pipeline.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
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
      const auto expected_entity = frozen::create_hazard(
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
            frozen::kPriorHazardDrawCount + births * frozen::kCreatedHazardDrawCount);
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

// The test-side proposed schedule remains independent of the now-delegating production spawner;
// the frozen old schedule never calls the promoted constructor.
void candidate_spawn_due(simulation::GameWorld& world, const simulation::TickContext& context,
                         const std::span<const gameplay::HazardArchetype> archetypes) {
  if (world.match().phase != simulation::MatchPhase::kRunning) {
    return;
  }
  for (const auto& archetype : archetypes) {
    if (context.tick_sequence().value() % archetype.spawn_interval_ticks() != 0) {
      continue;
    }
    if (world.entity_id_reservation().empty() ||
        world.store<simulation::PhysicsBody>().size() >= simulation::kMaximumEntityCount) {
      return;
    }
    static_cast<void>(gameplay::create_crossing_hazard(world, context.map().bounds(), archetype,
                                                       context.fixed_delta().seconds()));
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
        auto before = initial;
        before.mutable_match().phase = phase;
        while (before.entity_id_reservation().count() > remaining) {
          static_cast<void>(before.create_entity());
        }
        auto expected = before;
        auto actual = before;
        auto old_reader = before;
        const testing::TickHarness harness(simulation::TickSequence::create(tick));
        frozen::spawn_due(expected, harness.context(), archetypes);
        candidate_spawn_due(actual, harness.context(), archetypes);
        production.apply(old_reader, harness.context());
        CAPTURE(phase, remaining, tick);
        CHECK(actual == expected);
        CHECK(old_reader == expected);
        const auto births =
            phase == simulation::MatchPhase::kRunning && tick == frozen::kCommonDueTick
                ? (remaining < archetypes.size() ? remaining : archetypes.size())
                : 0;
        CHECK(actual.store<simulation::PhysicsBody>().size() == births);
        CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
              births * frozen::kCreatedHazardDrawCount);
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
  REQUIRE_THROWS_AS(frozen::create_hazard(expected, context.map().bounds(), archetype,
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
  // The old inner operation draws first. Its caller owns the pre-draw check, and changing this
  // failure boundary during promotion would be a behavior change rather than a pure extraction.
  CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
        frozen::kCreatedHazardDrawCount);
}

void prove_body_capacity(const simulation::GameWorld& initial, const simulation::TickContext&) {
  const auto archetypes = frozen::archetypes();
  const gameplay::HazardSpawnSystem production(archetypes);
  const testing::TickHarness harness(simulation::TickSequence::create(frozen::kCommonDueTick));
  constexpr std::array available_slots{std::size_t{0}, std::size_t{1}};
  for (const auto available : available_slots) {
    auto before = initial;
    for (std::size_t index = 0; index < simulation::kMaximumEntityCount - available; ++index) {
      before.mutable_store<simulation::PhysicsBody>().insert_or_assign(
          simulation::EntityId::create(frozen::kFirstCapacityFixtureEntity + index),
          frozen::capacity_fixture_body());
    }
    auto expected = before;
    auto actual = before;
    auto old_reader = before;
    frozen::spawn_due(expected, harness.context(), archetypes);
    candidate_spawn_due(actual, harness.context(), archetypes);
    production.apply(old_reader, harness.context());
    CAPTURE(available);
    CHECK(actual == expected);
    CHECK(old_reader == expected);
    CHECK(actual.store<simulation::PhysicsBody>().size() == simulation::kMaximumEntityCount);
    CHECK(actual.random(simulation::RandomStreamKind::kHazards).draw_count() ==
          available * frozen::kCreatedHazardDrawCount);
    CHECK(actual.entity_id_reservation().count() == frozen::kReservationSize - available);
    CHECK(actual.store<simulation::LethalOnContact>().size() == available);
  }
}

void prove_instance_policy(const simulation::GameWorld& initial,
                           const simulation::TickContext& context) {
  auto actual = initial;
  auto expected = initial;
  for (const auto archetype_policy : policy_fixture::kPolicies) {
    const auto archetype = policy_fixture::archetype(archetype_policy);
    for (const auto instance : policy_fixture::kOverrides) {
      const auto expected_entity = frozen::create_hazard(
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
    auto expected = initial;
    const auto archetype = policy_fixture::archetype(policy);
    const testing::TickHarness harness(simulation::TickSequence::create(frozen::kCommonDueTick));
    const gameplay::HazardSpawnSystem production({archetype});
    production.apply(actual, harness.context());
    const auto entity = frozen::create_hazard(expected, harness.map().bounds(), archetype,
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

TEST_CASE("crossing hazard creation preserves full old worlds across mixed archetypes and seeds",
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

TEST_CASE("crossing hazard creation preserves declaration order and pre-draw reservation skips",
          "[unit][gameplay][shared][create_crossing_hazard][promotion]") {
  run_proof(prove_schedule, frozen_crossing::kSeed);
}

TEST_CASE("crossing hazard creation preserves the old exhausted-reservation failure boundary",
          "[unit][gameplay][shared][create_crossing_hazard][promotion]") {
  run_proof(prove_exhausted_creation, frozen_crossing::kSeed);
}

TEST_CASE("crossing hazard creation preserves pre-draw full-body skips and the final body slot",
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
