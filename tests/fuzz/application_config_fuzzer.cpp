#include "application_config_loader.hpp"
#include "application_input_error.hpp"
#include "controllers_validation_error.hpp"
#include "fuzz_input_workspace.hpp"
#include "gameplay_validation_error.hpp"
#include "server_config.hpp"
#include "simulation_validation_error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace {

using blob_royale::application::ApplicationConfigLoader;
using blob_royale::application::ApplicationInputError;
using blob_royale::controllers::ControllersValidationError;
using blob_royale::fuzz::FuzzInputWorkspace;
using blob_royale::gameplay::GameplayValidationError;
using blob_royale::server::ServerConfigValidationError;
using blob_royale::simulation::SimulationValidationError;

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
  static FuzzInputWorkspace workspace{"configuration.cfg"};
  const std::filesystem::path& configuration_path =
      workspace.write(std::span<const std::uint8_t>{data, size});
  const std::string configuration_path_text = configuration_path.string();
  constexpr char kExecutable[] = "blob-royale";
  constexpr char kConfigOption[] = "--config";
  constexpr char kScenarioOption[] = "--scenario";
  constexpr char kScenarioPath[] = "scenario.csv";
  const char* arguments[] = {kExecutable, kConfigOption, configuration_path_text.c_str(),
                             kScenarioOption, kScenarioPath};

  // Exactly the typed errors the loader is contracted to throw, and no catch-all: a `...` here
  // would swallow the crash this harness exists to find. The two gameplay-domain rejections joined
  // the list when `[match]` and `[royale]` did, because a mode's own rules -- a lobby minimum below
  // one, an unknown bot kind -- are refused by the domain that owns them rather than by the parser.
  // `main.cpp` catches the same set plus a terminal catch-all, so a rejected configuration is a
  // one-line diagnostic and a nonzero exit rather than an abort.
  try {
    static_cast<void>(ApplicationConfigLoader::load(5, arguments));
  } catch (const ApplicationInputError&) {
  } catch (const ServerConfigValidationError&) {
  } catch (const SimulationValidationError&) {
  } catch (const GameplayValidationError&) {
  } catch (const ControllersValidationError&) {
  }
  return 0;
}
