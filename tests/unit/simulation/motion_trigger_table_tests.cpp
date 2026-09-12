#include "motion_trigger_table.hpp"

#include "contact_rule_table.hpp"
#include "events/despawn_event.hpp"
#include "game_world.hpp"
#include "motion_triggers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <type_traits>
#include <utility>

namespace simulation = blob_royale::simulation;

namespace {

using Subject = simulation::ContactRule::Subject;

template <class T>
concept CanBorrowContactFacts =
    requires(T&& value) { simulation::LiveMotionFacts::for_contacts(std::forward<T>(value)); };
template <class T>
concept CanBorrowTriggerFacts =
    requires(T&& value) { simulation::LiveMotionFacts::for_trigger(std::forward<T>(value)); };
template <class T>
concept CanBorrowBindings = requires(T&& value, const simulation::GameWorld& world,
                                     const simulation::TickContext& context) {
  std::forward<T>(value).bind(world, context);
};
template <class T>
concept CanBorrowRows = requires(T&& value) { std::forward<T>(value).rows(); };

simulation::Vector2 point(double x, double y) { return simulation::Vector2::create(x, y); }

struct Fixture final {
  simulation::SimulationConfig config =
      simulation::SimulationConfig::create(100.0, 100.0, 1.0, 400, 1, 1, 0.0);
  simulation::GameWorld world = simulation::GameWorld::create({
      simulation::GameWorld::EntitySeed::create(
          simulation::EntityId::create(20),
          simulation::PhysicsBody::create(point(60.0, 60.0), point(0.0, 0.0), point(0.0, 0.0))
              .with_radius(1.0)),
      simulation::GameWorld::EntitySeed::create(
          simulation::EntityId::create(10),
          simulation::PhysicsBody::create(point(20.0, 20.0), point(0.0, 0.0), point(0.0, 0.0))
              .with_radius(1.0)),
  });
  simulation::MapDefinition map = simulation::MapDefinition::create(
      "trigger_bindings",
      simulation::TerrainDefinition::solid(simulation::ArenaBounds::create(100.0, 100.0)), {}, {},
      simulation::MapMetadata::none());
  simulation::SpatialGrid grid = simulation::SpatialGrid::create(config, map.bounds(), world);
  simulation::TickContext context = simulation::TickContext::create(
      simulation::TickSequence::create(1), simulation::FixedDelta::canonical(), config, map, grid);
  Fixture() = default;
  Fixture(const Fixture&) = delete;
  Fixture(Fixture&&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  Fixture& operator=(Fixture&&) = delete;

  std::vector<Subject> subjects() const {
    std::vector<Subject> result;
    for (const auto& entry : world.store<simulation::PhysicsBody>().entries()) {
      result.push_back({entry.entity, entry.value});
    }
    return result;
  }
};

// Immutable authored policy; the probe is test-only evidence of binding traversal, never a
// production policy state channel. Geometry callbacks do not write it.
class ScriptPolicy final : public simulation::MotionTriggerPolicy {
public:
  ScriptPolicy(std::uint64_t identity, double time, std::optional<std::uint64_t> cursor = 0,
               bool terminates = false,
               std::shared_ptr<std::vector<std::uint64_t>> binding_trace = {})
      : identity_(identity), time_(simulation::MotionTime::create(time)), cursor_(cursor),
        terminates_(terminates), binding_trace_(std::move(binding_trace)) {}
  std::optional<std::uint64_t> bind(const simulation::GameWorld&, simulation::EntityId entity,
                                    const simulation::TickContext&) const override {
    if (binding_trace_) {
      binding_trace_->push_back(entity.value() * 100 + identity_);
    }
    return cursor_;
  }
  std::optional<simulation::MotionTriggerProposal>
  query(const simulation::GameWorld&, const Subject&, const simulation::MotionTriggerWindow&,
        const simulation::TickContext&, std::uint64_t cursor,
        simulation::MotionQueryBudget&) const override {
    if (cursor != 0) {
      return std::nullopt;
    }
    return simulation::MotionTriggerProposal{time_, simulation::MotionEventPriority::kCheckpoint};
  }
  simulation::MotionTriggerResponse<simulation::WorldEvent>
  respond(const simulation::GameWorld&, const Subject& subject,
          const simulation::MotionTriggerEvent& event,
          const simulation::TickContext&) const override {
    return {{subject.body, terminates_ ? simulation::MotionDisposition::kTerminate
                                       : simulation::MotionDisposition::kContinue},
            event.cursor + 1,
            {simulation::DespawnEvent{simulation::EntityId::create(identity_)}}};
  }

private:
  std::uint64_t identity_;
  simulation::MotionTime time_;
  std::optional<std::uint64_t> cursor_;
  bool terminates_;
  std::shared_ptr<std::vector<std::uint64_t>> binding_trace_;
};

simulation::MotionTriggerTable
table_of(std::vector<simulation::MotionTriggerTable::Declaration> declarations) {
  return simulation::MotionTriggerTable::create(std::move(declarations));
}

simulation::PairMotionResponse<simulation::WorldEvent>
unchanged_pair(const simulation::GameWorld&, const Subject& first, const Subject& second,
               const simulation::PairContactObservation&, const simulation::TickContext&,
               const simulation::LiveMotionFacts& facts) {
  // Trigger facts must never escape into pair dispatch.
  static_cast<void>(facts.require_contacts());
  return {{first.body}, {second.body}, {}};
}

simulation::ContinuousMotionResult<simulation::WorldEvent>
solve(const Fixture& fixture, const simulation::BoundMotionTriggers& bindings) {
  const auto contacts = simulation::ContactRuleTable::empty();
  const auto facts = simulation::LiveMotionFacts::for_contacts(contacts);
  return simulation::solve_continuous_motion<simulation::WorldEvent, simulation::LiveMotionFacts>(
      fixture.world, fixture.subjects(), fixture.context, facts, unchanged_pair, bindings.rows());
}

} // namespace

TEST_CASE("trigger table binds ascending body ids then authored policies without geometry") {
  Fixture fixture;
  auto trace = std::make_shared<std::vector<std::uint64_t>>();
  std::vector<simulation::MotionTriggerTable::Declaration> declarations;
  declarations.push_back({20, 1, std::make_unique<ScriptPolicy>(2, 0.2, 0, false, trace)});
  declarations.push_back(
      {10, 1, std::make_unique<ScriptPolicy>(1, 0.1, std::nullopt, false, trace)});
  auto table = table_of(std::move(declarations));
  const auto bindings = table.bind(fixture.world, fixture.context);
  REQUIRE(*trace == std::vector<std::uint64_t>{1002, 1001, 2002, 2001});
  REQUIRE(bindings.rows().size() == 2);
  REQUIRE(bindings.rows()[0].entity == simulation::EntityId::create(10));
  REQUIRE(bindings.rows()[1].entity == simulation::EntityId::create(20));
}

TEST_CASE("trigger bindings preserve independent query and response facts after owner moves") {
  STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<simulation::BoundMotionTriggers>);
  STATIC_REQUIRE_FALSE(std::is_copy_assignable_v<simulation::BoundMotionTriggers>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<simulation::BoundMotionTriggers>);
  STATIC_REQUIRE(std::is_nothrow_move_assignable_v<simulation::BoundMotionTriggers>);
  Fixture fixture;
  std::vector<simulation::MotionTriggerTable::Declaration> declarations;
  declarations.push_back({10, 1, std::make_unique<ScriptPolicy>(1, 0.6)});
  declarations.push_back({20, 1, std::make_unique<ScriptPolicy>(2, 0.2)});
  auto table = table_of(std::move(declarations));
  auto original = table.bind(fixture.world, fixture.context);
  const auto* original_facts = &original.rows()[0].facts_override->get();
  auto moved = std::move(original);
  const auto empty_table = simulation::MotionTriggerTable::empty();
  auto assigned = empty_table.bind(fixture.world, fixture.context);
  assigned = std::move(moved);
  REQUIRE(&assigned.rows()[0].facts_override->get() == original_facts);
  const auto result = solve(fixture, assigned);
  REQUIRE(result.effects.size() == 4);
  REQUIRE(result.effects[0].event.time() == simulation::MotionTime::create(0.2));
  REQUIRE(result.effects[1].event.time() == simulation::MotionTime::create(0.2));
  REQUIRE(result.effects[2].event.time() == simulation::MotionTime::create(0.6));
  REQUIRE(std::get<simulation::DespawnEvent>(result.effects[0].effect).entity.value() == 2);
  REQUIRE(std::get<simulation::DespawnEvent>(result.effects[2].effect).entity.value() == 1);
  REQUIRE(result.trigger_cursors == std::vector<std::uint64_t>{1, 1, 1, 1});
}

TEST_CASE("live motion facts reject wrong capabilities without substituting an empty policy") {
  STATIC_REQUIRE(CanBorrowContactFacts<const simulation::ContactRuleTable&>);
  STATIC_REQUIRE_FALSE(CanBorrowContactFacts<simulation::ContactRuleTable>);
  STATIC_REQUIRE(CanBorrowTriggerFacts<const ScriptPolicy&>);
  STATIC_REQUIRE_FALSE(CanBorrowTriggerFacts<ScriptPolicy>);
  STATIC_REQUIRE(CanBorrowBindings<const simulation::MotionTriggerTable&>);
  STATIC_REQUIRE_FALSE(CanBorrowBindings<simulation::MotionTriggerTable>);
  STATIC_REQUIRE(CanBorrowRows<const simulation::BoundMotionTriggers&>);
  STATIC_REQUIRE_FALSE(CanBorrowRows<simulation::BoundMotionTriggers>);
  const auto contacts = simulation::ContactRuleTable::empty();
  const ScriptPolicy policy{1, 0.2};
  const auto pair_facts = simulation::LiveMotionFacts::for_contacts(contacts);
  const auto trigger_facts = simulation::LiveMotionFacts::for_trigger(policy);
  REQUIRE(&pair_facts.require_contacts() == &contacts);
  REQUIRE(&trigger_facts.require_trigger() == &policy);
  REQUIRE_THROWS_AS(pair_facts.require_trigger(), simulation::SimulationValidationError);
  REQUIRE_THROWS_AS(trigger_facts.require_contacts(), simulation::SimulationValidationError);
}

TEST_CASE("trigger table owns policies after its declaring owner is destroyed") {
  struct DeclarationOwner final {
    bool& destroyed;
    const std::uint64_t identity = 7;
    ~DeclarationOwner() { destroyed = true; }
    simulation::MotionTriggerTable declarations() const {
      std::vector<simulation::MotionTriggerTable::Declaration> rows;
      rows.push_back({0, 1, std::make_unique<ScriptPolicy>(identity, 0.25)});
      return simulation::MotionTriggerTable::create(std::move(rows));
    }
  };
  bool declaring_owner_destroyed = false;
  auto table = [&] {
    const DeclarationOwner owner{declaring_owner_destroyed};
    return owner.declarations();
  }();
  REQUIRE(declaring_owner_destroyed);
  Fixture fixture;
  const auto bindings = table.bind(fixture.world, fixture.context);
  const auto result = solve(fixture, bindings);
  REQUIRE(result.effects.size() == 2);
  for (const auto& effect : result.effects) {
    REQUIRE(effect.event.time() == simulation::MotionTime::create(0.25));
    REQUIRE(std::get<simulation::DespawnEvent>(effect.effect).entity.value() == 7);
  }
}

TEST_CASE("trigger declarations reject null policies unsafe ranges and excessive count") {
  SECTION("null") {
    std::vector<simulation::MotionTriggerTable::Declaration> declarations;
    declarations.push_back({0, 0, nullptr});
    REQUIRE_THROWS_AS(table_of(std::move(declarations)), simulation::SimulationValidationError);
  }
  SECTION("overflow") {
    std::vector<simulation::MotionTriggerTable::Declaration> declarations;
    declarations.push_back(
        {simulation::kMaximumProtocolSafeInteger, 1, std::make_unique<ScriptPolicy>(1, 0.2)});
    REQUIRE_THROWS_AS(table_of(std::move(declarations)), simulation::SimulationValidationError);
  }
  SECTION("cursor") {
    std::vector<simulation::MotionTriggerTable::Declaration> declarations;
    declarations.push_back({0, simulation::kMaximumMotionTriggerCursorValue + 1,
                            std::make_unique<ScriptPolicy>(1, 0.2)});
    REQUIRE_THROWS_AS(table_of(std::move(declarations)), simulation::SimulationValidationError);
  }
  SECTION("count") {
    std::vector<simulation::MotionTriggerTable::Declaration> declarations;
    for (std::size_t index = 0; index <= simulation::kMaximumMotionTriggerDeclarationCount;
         ++index) {
      declarations.push_back({0, 1, std::make_unique<ScriptPolicy>(1, 0.2)});
    }
    REQUIRE_THROWS_AS(table_of(std::move(declarations)), simulation::SimulationValidationError);
  }
}

TEST_CASE("trigger binding rejects body limits before policy callbacks") {
  Fixture fixture;
  auto trace = std::make_shared<std::vector<std::uint64_t>>();
  std::vector<simulation::MotionTriggerTable::Declaration> declarations;
  declarations.push_back({0, 1, std::make_unique<ScriptPolicy>(1, 0.2, 0, false, trace)});
  const auto table = table_of(std::move(declarations));
  simulation::MotionLimits limits;
  limits.bodies = 1;
  REQUIRE_THROWS_AS(table.bind(fixture.world, fixture.context, limits),
                    simulation::SimulationValidationError);
  REQUIRE(trace->empty());
}

TEST_CASE("trigger binding rejects invalid limits row exhaustion and cursors") {
  Fixture fixture;
  std::vector<simulation::MotionTriggerTable::Declaration> declarations;
  declarations.push_back({0, 1, std::make_unique<ScriptPolicy>(1, 0.2)});
  const auto table = table_of(std::move(declarations));
  simulation::MotionLimits limits;
  SECTION("upper limit") { limits.effects = simulation::kMaximumMotionEffectCount + 1; }
  SECTION("rows") { limits.trigger_declarations = 1; }
  SECTION("lowered cursor") { limits.trigger_cursor = 0; }
  REQUIRE_THROWS_AS(table.bind(fixture.world, fixture.context, limits),
                    simulation::SimulationValidationError);
}

TEST_CASE("trigger binding rejects policy initial cursor beyond declaration") {
  Fixture fixture;
  std::vector<simulation::MotionTriggerTable::Declaration> declarations;
  declarations.push_back({0, 1, std::make_unique<ScriptPolicy>(1, 0.2, 2)});
  const auto table = table_of(std::move(declarations));
  REQUIRE_THROWS_AS(table.bind(fixture.world, fixture.context),
                    simulation::SimulationValidationError);
}

TEST_CASE("trigger feature overlap remains a canonical solver rejection") {
  Fixture fixture;
  std::vector<simulation::MotionTriggerTable::Declaration> declarations;
  declarations.push_back({0, 1, std::make_unique<ScriptPolicy>(1, 0.2)});
  declarations.push_back({1, 1, std::make_unique<ScriptPolicy>(2, 0.4)});
  const auto table = table_of(std::move(declarations));
  const auto bindings = table.bind(fixture.world, fixture.context);
  REQUIRE_THROWS_AS(solve(fixture, bindings), simulation::SimulationValidationError);
}

TEST_CASE("trigger termination suppresses later declared trigger effects") {
  Fixture fixture;
  std::vector<simulation::MotionTriggerTable::Declaration> declarations;
  declarations.push_back({0, 1, std::make_unique<ScriptPolicy>(1, 0.2, 0, true)});
  declarations.push_back({10, 1, std::make_unique<ScriptPolicy>(2, 0.6)});
  const auto table = table_of(std::move(declarations));
  const auto bindings = table.bind(fixture.world, fixture.context);
  const auto result = solve(fixture, bindings);
  REQUIRE(result.effects.size() == 2);
  REQUIRE(result.trigger_cursors == std::vector<std::uint64_t>{1, 0, 1, 0});
  for (const auto& body : result.motion.bodies) {
    REQUIRE(body.result.disposition == simulation::MotionDisposition::kTerminate);
  }
}
