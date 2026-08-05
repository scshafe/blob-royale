#include "application_input_error.hpp"
#include "fuzz_input_workspace.hpp"
#include "scenario_loader.hpp"
#include "simulation_config.hpp"
#include "simulation_validation_error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace {

using blob_royale::application::ApplicationInputError;
using blob_royale::application::ScenarioLoader;
using blob_royale::fuzz::FuzzInputWorkspace;
using blob_royale::simulation::SimulationConfig;
using blob_royale::simulation::SimulationValidationError;

[[nodiscard]] const SimulationConfig& fuzz_simulation_config() {
  static const SimulationConfig configuration =
      SimulationConfig::create(1'000.0, 1'000.0, 10.0, 400, 10, 10);
  return configuration;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
  static FuzzInputWorkspace workspace{"scenario.csv"};
  const std::filesystem::path& scenario_path =
      workspace.write(std::span<const std::uint8_t>{data, size});

  try {
    static_cast<void>(ScenarioLoader::load(scenario_path, fuzz_simulation_config()));
  } catch (const ApplicationInputError&) {
  } catch (const SimulationValidationError&) {
  }
  return 0;
}
