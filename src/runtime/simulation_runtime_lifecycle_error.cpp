#include "simulation_runtime_lifecycle_error.hpp"

#include <string>
#include <utility>

namespace blob_royale::runtime {
namespace {

[[nodiscard]] std::string lifecycle_error_message(const std::string& operation,
                                                  const SimulationRuntimeState state) {
  return "cannot " + operation + " SimulationRuntime while it is " +
         std::string(simulation_runtime_state_name(state));
}

} // namespace

SimulationRuntimeLifecycleError::SimulationRuntimeLifecycleError(
    std::string operation, const SimulationRuntimeState current_state)
    : std::logic_error(lifecycle_error_message(operation, current_state)),
      operation_(std::move(operation)), current_state_(current_state) {}

} // namespace blob_royale::runtime
