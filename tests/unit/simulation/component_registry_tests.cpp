#include "command_registry.hpp"
#include "commands/thrust_command.hpp"
#include "component_kind_name.hpp"
#include "component_list.hpp"
#include "component_registry.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/hill_component.hpp"
#include "components/hill_motion_component.hpp"
#include "components/hill_presence_component.hpp"
#include "components/lethal_on_contact_component.hpp"
#include "components/lifetime_component.hpp"
#include "components/race_progress_component.hpp"
#include "components/respawn_timer_component.hpp"
#include "components/score_component.hpp"
#include "components/team_component.hpp"
#include "components/zone_component.hpp"
#include "components/zone_exposure_component.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "game_world.hpp"
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

TEST_CASE("ComponentRegistry declares every component kind in one closed ordered list",
          "[unit][simulation][component_registry]") {
  // Fifteen, including the five the engine itself needs. `Zone` and `ZoneExposure` are royale's,
  // added by plan Step 21 as two headers under `components/` and one edited line in the registry,
  // with no other kernel file touched. `LethalOnContact` is the third addition and the one that
  // measures the seam hardest: it belongs to **no mode at all** but to `src/gameplay/shared/`, and
  // it cost the same one header plus one line here. That is the measurement the `entity_component`
  // seam's claim is answerable to (`docs/architecture/0005-royale-mode.md` § "Where zone and
  // elimination state live"). `RespawnTimer` is the fourth, also `shared/`'s, and cost the same;
  // `Hill` and `HillPresence` are king of the hill's, the way the zone pair is royale's
  // (`docs/architecture/0007-king-of-the-hill-and-race-modes.md`). `RaceProgress` is race's.
  STATIC_REQUIRE(simulation::ComponentRegistry::kKindCount == 15);
  STATIC_REQUIRE(
      std::is_same_v<simulation::ComponentStores<simulation::ComponentRegistry>,
                     std::tuple<simulation::ComponentStore<simulation::PhysicsBody>,
                                simulation::ComponentStore<simulation::Controllable>,
                                simulation::ComponentStore<simulation::Lifetime>,
                                simulation::ComponentStore<simulation::Score>,
                                simulation::ComponentStore<simulation::Team>,
                                simulation::ComponentStore<simulation::Zone>,
                                simulation::ComponentStore<simulation::ZoneExposure>,
                                simulation::ComponentStore<simulation::LethalOnContact>,
                                simulation::ComponentStore<simulation::RespawnTimer>,
                                simulation::ComponentStore<simulation::Hill>,
                                simulation::ComponentStore<simulation::HillPresence>,
                                simulation::ComponentStore<simulation::RaceProgress>,
                                simulation::ComponentStore<simulation::HillMotion>,
                                simulation::ComponentStore<simulation::Stun>,
                                simulation::ComponentStore<simulation::ContactEffectAdmission>>>);
}

TEST_CASE("Every registered component kind declares its own wire name",
          "[unit][simulation][component_registry]") {
  std::vector<std::string_view> names;
  simulation::ComponentRegistry::for_each_kind([&names]<typename Component>() {
    names.push_back(simulation::component_kind_name<Component>);
  });

  CHECK(names == std::vector<std::string_view>{
                     "physics_body", "controllable", "lifetime", "score", "team", "zone",
                     "zone_exposure", "lethal_on_contact", "respawn_timer", "hill", "hill_presence",
                     "race_progress", "hill_motion", "stun", "contact_effect_admission"});
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
  CHECK(simulation::Zone{zero, 10.0} == simulation::Zone{zero, 10.0});
  CHECK(simulation::Zone{zero, 10.0} != simulation::Zone{zero, 11.0});
  CHECK(simulation::Zone{zero, 10.0} !=
        simulation::Zone{simulation::Vector2::create(1.0, 0.0), 10.0});
  CHECK(simulation::ZoneExposure{4} == simulation::ZoneExposure{4});
  CHECK(simulation::ZoneExposure{4} != simulation::ZoneExposure{5});
  CHECK(simulation::RaceProgress{} == simulation::RaceProgress{0});
  CHECK(simulation::RaceProgress{1} != simulation::RaceProgress{2});
}

TEST_CASE("Controllable carries this tick's recorded commands and compares on them",
          "[unit][simulation][component_registry]") {
  const simulation::Command thrust{simulation::ThrustCommand{
      simulation::EntityId::create(5), simulation::Vector2::create(1.0, 0.0)}};
  const simulation::Controllable idle{simulation::ControllerId::create(4), {}};
  const simulation::Controllable thrusting{simulation::ControllerId::create(4), {thrust}};

  CHECK(idle.commands_this_tick.empty());
  REQUIRE(thrusting.commands_this_tick.size() == 1);
  CHECK(thrusting.commands_this_tick[0] == thrust);
  CHECK(idle != thrusting);
  CHECK(idle == simulation::Controllable{simulation::ControllerId::create(4)});
}

TEST_CASE("A component list generates one store per declared kind",
          "[unit][simulation][component_registry]") {
  using PairList = simulation::ComponentList<simulation::Score, simulation::Team>;

  STATIC_REQUIRE(PairList::kKindCount == 2);
  STATIC_REQUIRE(std::is_same_v<simulation::ComponentStores<PairList>,
                                std::tuple<simulation::ComponentStore<simulation::Score>,
                                           simulation::ComponentStore<simulation::Team>>>);
}

TEST_CASE("Race progress participates in world equality and entity destruction",
          "[unit][simulation][component_registry][race_progress]") {
  const simulation::EntityId racer = simulation::EntityId::create(7);
  simulation::GameWorld world = simulation::GameWorld::create({});
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(racer,
                                                                   simulation::RaceProgress{1});
  const simulation::GameWorld first_gate = world;
  CHECK(world == first_gate);
  world.mutable_store<simulation::RaceProgress>().insert_or_assign(racer,
                                                                   simulation::RaceProgress{2});
  CHECK(world != first_gate);
  world.destroy_entity(racer);
  CHECK(world.store<simulation::RaceProgress>().empty());
  CHECK_FALSE(world.contains(racer));
}
