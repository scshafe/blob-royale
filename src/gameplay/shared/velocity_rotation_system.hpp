#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_VELOCITY_ROTATION_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_VELOCITY_ROTATION_SYSTEM_HPP

#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: velocity_rotation -- exact quarter-turn pulses after charge, before integration.
class VelocityRotationSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "velocity_rotation";
  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem> create();
  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;
};

} // namespace blob_royale::gameplay

#endif
