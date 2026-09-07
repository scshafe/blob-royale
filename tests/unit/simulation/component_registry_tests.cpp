#include "component_kind_name.hpp"
#include "component_list.hpp"
#include "component_registry.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/lifetime_component.hpp"
#include "components/score_component.hpp"
#include "components/team_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "physics_body.hpp"
#include "team_id.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <vector>

namespace simulation = blob_royale::simulation;

TEST_CASE("ComponentRegistry declares the engine component kinds in a closed ordered list",
          "[unit][simulation][component_registry]") {
  STATIC_REQUIRE(simulation::ComponentRegistry::kKindCount == 5);
  STATIC_REQUIRE(std::is_same_v<simulation::ComponentStores<simulation::ComponentRegistry>,
                                std::tuple<simulation::ComponentStore<simulation::PhysicsBody>,
                                           simulation::ComponentStore<simulation::Controllable>,
                                           simulation::ComponentStore<simulation::Lifetime>,
                                           simulation::ComponentStore<simulation::Score>,
                                           simulation::ComponentStore<simulation::Team>>>);
}

TEST_CASE("Every registered component kind declares its own wire name",
          "[unit][simulation][component_registry]") {
  std::vector<std::string_view> names;
  simulation::ComponentRegistry::for_each_kind([&names]<typename Component>() {
    names.push_back(simulation::component_kind_name<Component>);
  });

  CHECK(names ==
        std::vector<std::string_view>{"physics_body", "controllable", "lifetime", "score", "team"});
}

TEST_CASE("Registry visitation reaches every kind exactly once in declared order",
          "[unit][simulation][component_registry]") {
  std::size_t visit_count = 0;
  simulation::ComponentRegistry::for_each_kind([&visit_count]<typename Component>() {
    static_cast<void>(sizeof(Component));
    ++visit_count;
  });

  CHECK(visit_count == simulation::ComponentRegistry::kKindCount);
}

TEST_CASE("Every registered component kind is a comparable value struct",
          "[unit][simulation][component_registry]") {
  const simulation::Vector2 zero = simulation::Vector2::create(0.0, 0.0);

  CHECK(simulation::PhysicsBody::create(zero, zero, zero) ==
        simulation::PhysicsBody::create(zero, zero, zero));
  CHECK(simulation::Controllable{simulation::ControllerId::create(4)} ==
        simulation::Controllable{simulation::ControllerId::create(4)});
  CHECK(simulation::Controllable{simulation::ControllerId::create(4)} !=
        simulation::Controllable{simulation::ControllerId::create(5)});
  CHECK(simulation::Lifetime{7} == simulation::Lifetime{7});
  CHECK(simulation::Lifetime{7} != simulation::Lifetime{8});
  CHECK(simulation::Score{-3} == simulation::Score{-3});
  CHECK(simulation::Score{-3} != simulation::Score{3});
  CHECK(simulation::Team{simulation::TeamId::create(2)} ==
        simulation::Team{simulation::TeamId::create(2)});
  CHECK(simulation::Team{simulation::TeamId::create(2)} !=
        simulation::Team{simulation::TeamId::create(3)});
}

TEST_CASE("A component list generates one store per declared kind",
          "[unit][simulation][component_registry]") {
  using PairList = simulation::ComponentList<simulation::Score, simulation::Team>;

  STATIC_REQUIRE(PairList::kKindCount == 2);
  STATIC_REQUIRE(std::is_same_v<simulation::ComponentStores<PairList>,
                                std::tuple<simulation::ComponentStore<simulation::Score>,
                                           simulation::ComponentStore<simulation::Team>>>);
}
