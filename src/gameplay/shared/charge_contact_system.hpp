#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_CHARGE_CONTACT_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_CHARGE_CONTACT_SYSTEM_HPP

#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: charge_contact_system -- commits the first frozen impact per active charge attempt.
// Runs immediately before StatusSystem. Successful impacts remove the attacker's charge for instant
// readiness and request the captured target stun; shield blocks cancel only the active window.
// Candidates retain solver production order. Status writes occur after all candidates are judged,
// preserving mutual hits and every post-collision velocity.
class ChargeContactSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "charge_contact";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem> create();
  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // Ignores stale/duplicate activations. Appends StunRequests only after the event walk completes,
  // so growing the world-owned event vector cannot invalidate the candidates being read.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;
};

} // namespace blob_royale::gameplay

#endif
