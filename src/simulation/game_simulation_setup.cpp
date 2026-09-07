#include "game_simulation_setup.hpp"

#include <memory>
#include <utility>

namespace blob_royale::simulation {

GameSimulationSetup GameSimulationSetup::engine_defaults() { return GameSimulationSetup(); }

GameSimulationSetup GameSimulationSetup::of_mode(MapDefinition map,
                                                 std::unique_ptr<const GameMode> mode) {
  return GameSimulationSetup().with_map(std::move(map)).with_mode(std::move(mode));
}

GameSimulationSetup GameSimulationSetup::with_map(MapDefinition map) && {
  GameSimulationSetup setup = std::move(*this);
  setup.map_ = std::move(map);
  return setup;
}

GameSimulationSetup GameSimulationSetup::with_mode(std::unique_ptr<const GameMode> mode) && {
  GameSimulationSetup setup = std::move(*this);
  setup.mode_ = std::move(mode);
  return setup;
}

GameSimulationSetup GameSimulationSetup::with_systems(SystemPipeline systems) && {
  GameSimulationSetup setup = std::move(*this);
  setup.systems_ = std::move(systems);
  return setup;
}

GameSimulationSetup GameSimulationSetup::with_contact_rules(ContactRuleTable contact_rules) && {
  GameSimulationSetup setup = std::move(*this);
  setup.contact_rules_ = std::move(contact_rules);
  return setup;
}

} // namespace blob_royale::simulation
