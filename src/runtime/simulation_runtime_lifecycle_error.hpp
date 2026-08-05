#ifndef BLOB_ROYALE_RUNTIME_SIMULATION_RUNTIME_LIFECYCLE_ERROR_HPP
#define BLOB_ROYALE_RUNTIME_SIMULATION_RUNTIME_LIFECYCLE_ERROR_HPP

#include "simulation_runtime_state.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace blob_royale::runtime {

// Reports an attempted lifecycle transition from a terminal runtime state.
class SimulationRuntimeLifecycleError final : public std::logic_error {
public:
  static constexpr std::string_view kCode = "RUNTIME.INVALID_LIFECYCLE_TRANSITION";

  SimulationRuntimeLifecycleError(std::string operation, SimulationRuntimeState current_state);

  [[nodiscard]] constexpr std::string_view code() const noexcept { return kCode; }
  [[nodiscard]] const std::string& operation() const& noexcept { return operation_; }
  [[nodiscard]] const std::string& operation() const&& = delete;
  [[nodiscard]] SimulationRuntimeState current_state() const noexcept { return current_state_; }

private:
  std::string operation_;
  SimulationRuntimeState current_state_;
};

} // namespace blob_royale::runtime

#endif
