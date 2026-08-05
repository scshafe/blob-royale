#ifndef BLOB_ROYALE_RUNTIME_SIMULATION_RUNTIME_STATE_HPP
#define BLOB_ROYALE_RUNTIME_SIMULATION_RUNTIME_STATE_HPP

#include <string_view>

namespace blob_royale::runtime {

// The complete externally observable lifecycle of one SimulationRuntime.
enum class SimulationRuntimeState {
  kReady,
  kRunning,
  kPausing,
  kPaused,
  kStopping,
  kStopped,
  kFailed,
};

[[nodiscard]] constexpr std::string_view
simulation_runtime_state_name(const SimulationRuntimeState state) noexcept {
  switch (state) {
  case SimulationRuntimeState::kReady:
    return "ready";
  case SimulationRuntimeState::kRunning:
    return "running";
  case SimulationRuntimeState::kPausing:
    return "pausing";
  case SimulationRuntimeState::kPaused:
    return "paused";
  case SimulationRuntimeState::kStopping:
    return "stopping";
  case SimulationRuntimeState::kStopped:
    return "stopped";
  case SimulationRuntimeState::kFailed:
    return "failed";
  }
  return "invalid";
}

} // namespace blob_royale::runtime

#endif
