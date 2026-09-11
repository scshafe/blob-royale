#include "application_input_error.hpp"
#include "fuzz_input_workspace.hpp"
#include "map_loader.hpp"
#include "simulation_validation_error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace {

using blob_royale::application::ApplicationInputError;
using blob_royale::application::MapLoader;
using blob_royale::fuzz::FuzzInputWorkspace;
using blob_royale::simulation::SimulationValidationError;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
  // The seed's map.name matches this stable directory name. Only map.cfg mutates; complete,
  // valid companion headers let accepted inputs reach the production terrain value factories.
  static FuzzInputWorkspace workspace{
      "fuzz-map/map.cfg",
      {{"static_bodies.csv", std::string{MapLoader::expected_static_bodies_header()} + "\n"},
       {"markers.csv", std::string{MapLoader::expected_markers_header()} + "\n"}}};
  const std::filesystem::path& configuration_path =
      workspace.write(std::span<const std::uint8_t>{data, size});
  try {
    static_cast<void>(MapLoader::load(configuration_path.parent_path()));
  } catch (const ApplicationInputError&) {
  } catch (const SimulationValidationError&) {
  }
  return 0;
}
