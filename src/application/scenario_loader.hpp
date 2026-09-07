#ifndef BLOB_ROYALE_APPLICATION_SCENARIO_LOADER_HPP
#define BLOB_ROYALE_APPLICATION_SCENARIO_LOADER_HPP

#include "game_world.hpp"
#include "map_definition.hpp"
#include "simulation_config.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace blob_royale::application {

// canonical: scenario_loader -- the only external scenario CSV boundary.
class ScenarioLoader final {
public:
  static constexpr std::size_t kMaximumScenarioFileBytes = 1'048'576;
  static constexpr std::size_t kMaximumScenarioRowBytes = 4'096;
  static constexpr std::size_t kScenarioColumnCount = 7;

  // Loads a bounded exact-schema CSV into a validated, ID-ordered GameWorld seated on one map.
  //
  // **A scenario is extra content, not the whole world.** The map's static bodies are seated first,
  // by the world's own id policy, and the scenario's rows are placed on top of them; a row whose
  // `entity_id` lands inside the map's block is a rejection rather than a wall silently replaced by
  // a player. `match_seed` is `[match] seed`, so a seeded fixture and a live match draw from the
  // same generator for the same configuration.
  //
  // Throws ApplicationInputError for every file, grammar, or cross-field failure, and
  // SimulationValidationError for a world rule the simulation owns.
  [[nodiscard]] static simulation::GameWorld
  load(const std::filesystem::path& scenario_path,
       const simulation::SimulationConfig& simulation_config, const simulation::MapDefinition& map,
       std::uint64_t match_seed);

  // The same load onto the bare arena the configuration's world scalars describe, for a caller that
  // has no map. It is the pre-map shape every accepted fixture and the scenario fuzzer already use.
  [[nodiscard]] static simulation::GameWorld
  load(const std::filesystem::path& scenario_path,
       const simulation::SimulationConfig& simulation_config);

  // Returns the one accepted seven-column header; legacy type-based rows are not aliases.
  [[nodiscard]] static std::string_view expected_header() noexcept;
};

} // namespace blob_royale::application

#endif
