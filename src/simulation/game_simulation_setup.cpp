#include "game_simulation_setup.hpp"

#include <memory>
#include <utility>

namespace blob_royale::simulation {

GameSimulationSetup GameSimulationSetup::engine_defaults() { return GameSimulationSetup(); }

GameSimulationSetup GameSimulationSetup::of_mode(MapDefinition map,
                                                 std::unique_ptr<const GameMode> mode) {
  // Construct the final value directly: moving a chain of empty optionals obscures their
  // initialization from the pinned optimized compiler once a map owns shared terrain storage.
  GameSimulationSetup setup;
  setup.map_.emplace(std::move(map));
  setup.mode_ = std::move(mode);
  return setup;
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
