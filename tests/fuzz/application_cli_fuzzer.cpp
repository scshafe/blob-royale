#include "application_config_loader.hpp"
#include "application_input_error.hpp"
#include "fuzz_input_workspace.hpp"
#include "server_config.hpp"
#include "simulation_validation_error.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

using blob_royale::application::ApplicationConfigLoader;
using blob_royale::application::ApplicationInputError;
using blob_royale::fuzz::FuzzInputWorkspace;
using blob_royale::server::ServerConfigValidationError;
using blob_royale::simulation::SimulationValidationError;

[[nodiscard]] std::vector<std::string> decode_arguments(const std::span<const std::uint8_t> bytes) {
  constexpr std::size_t kMaximumFuzzedArgumentCount = 7;
  std::vector<std::string> arguments;
  arguments.emplace_back("blob-royale");

  std::size_t segment_start = 0;
  while (segment_start < bytes.size() && arguments.size() <= kMaximumFuzzedArgumentCount) {
    const auto delimiter = std::find(bytes.begin() + static_cast<std::ptrdiff_t>(segment_start),
                                     bytes.end(), static_cast<std::uint8_t>('\n'));
    const auto segment_end = static_cast<std::size_t>(delimiter - bytes.begin());
    arguments.emplace_back(reinterpret_cast<const char*>(bytes.data() + segment_start),
                           segment_end - segment_start);
    if (delimiter == bytes.end()) {
      break;
    }
    segment_start = segment_end + 1;
  }
  return arguments;
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, const std::size_t size) {
  static FuzzInputWorkspace workspace{"configuration.cfg"};
  std::vector<std::string> arguments = decode_arguments(std::span<const std::uint8_t>{data, size});

  // Only the accepted run shape can reach the filesystem. Redirect that shape to a
  // process-local empty regular file so arbitrary fuzzer bytes never select a host path.
  if (arguments.size() == 5 && arguments[1] == "--config" && arguments[3] == "--scenario") {
    const std::span<const std::uint8_t> empty_input;
    arguments[2] = workspace.write(empty_input).string();
    arguments[4] = "scenario.csv";
  }

  std::vector<const char*> argument_pointers;
  argument_pointers.reserve(arguments.size());
  for (const std::string& argument : arguments) {
    argument_pointers.push_back(argument.c_str());
  }

  try {
    static_cast<void>(ApplicationConfigLoader::load(static_cast<int>(argument_pointers.size()),
                                                    argument_pointers.data()));
  } catch (const ApplicationInputError&) {
  } catch (const ServerConfigValidationError&) {
  } catch (const SimulationValidationError&) {
  }
  return 0;
}
