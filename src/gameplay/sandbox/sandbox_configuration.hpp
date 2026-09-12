#ifndef BLOB_ROYALE_GAMEPLAY_SANDBOX_SANDBOX_CONFIGURATION_HPP
#define BLOB_ROYALE_GAMEPLAY_SANDBOX_SANDBOX_CONFIGURATION_HPP

#include "shared/duration_ticks.hpp"

#include <cstdint>

namespace blob_royale::gameplay {

// canonical: sandbox_configuration -- explicit free-play return timing, authored in seconds.
// Shared duration validation rejects nonfinite/negative/overflowing values with
// GAMEPLAY.DURATION_*.
class SandboxConfiguration final {
public:
  static constexpr double kDefaultRespawnDelaySeconds = 2.0;
  [[nodiscard]] static SandboxConfiguration create(const double respawn_delay_seconds) {
    return SandboxConfiguration{
        duration_ticks(respawn_delay_seconds, "sandbox.respawn_delay_seconds")};
  }
  [[nodiscard]] static SandboxConfiguration defaults() {
    return create(kDefaultRespawnDelaySeconds);
  }
  [[nodiscard]] std::uint64_t respawn_delay_ticks() const noexcept { return respawn_delay_ticks_; }
  friend bool operator==(const SandboxConfiguration&, const SandboxConfiguration&) = default;

private:
  explicit SandboxConfiguration(const std::uint64_t ticks) : respawn_delay_ticks_(ticks) {}
  std::uint64_t respawn_delay_ticks_;
};

} // namespace blob_royale::gameplay

#endif
