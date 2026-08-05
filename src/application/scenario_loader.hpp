#ifndef BLOB_ROYALE_APPLICATION_SCENARIO_LOADER_HPP
#define BLOB_ROYALE_APPLICATION_SCENARIO_LOADER_HPP

#include "game_world.hpp"
#include "simulation_config.hpp"

#include <cstddef>
#include <filesystem>
#include <string_view>

namespace blob_royale::application {

// canonical: scenario_loader -- the only external scenario CSV boundary.
class ScenarioLoader final {
public:
  static constexpr std::size_t kMaximumScenarioFileBytes = 1'048'576;
  static constexpr std::size_t kMaximumScenarioRowBytes = 4'096;
  static constexpr std::size_t kScenarioColumnCount = 7;

  // Loads a bounded exact-schema CSV into a validated, ID-ordered GameWorld.
  // Throws ApplicationInputError for every file, grammar, or cross-field failure.
  [[nodiscard]] static simulation::GameWorld
  load(const std::filesystem::path& scenario_path,
       const simulation::SimulationConfig& simulation_config);

  // Returns the one accepted seven-column header; legacy type-based rows are not aliases.
  [[nodiscard]] static std::string_view expected_header() noexcept;
};

} // namespace blob_royale::application

#endif
