#include "shared/charge_contact_system.hpp"

#include "components/charge_component.hpp"
#include "events/charge_contact_event.hpp"
#include "events/stun_request_event.hpp"
#include "game_world.hpp"
#include "tick_context.hpp"

#include <memory>
#include <variant>
#include <vector>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::SimulationSystem> ChargeContactSystem::create() {
  return std::make_unique<const ChargeContactSystem>();
}

void ChargeContactSystem::apply(simulation::GameWorld& world,
                                const simulation::TickContext& context) const {
  const auto tick = context.tick_sequence();
  std::vector<simulation::StunRequest> requested;
  for (const auto& event : world.events()) {
    const auto* candidate = std::get_if<simulation::ChargeContactCandidate>(&event);
    if (candidate == nullptr) {
      continue;
    }
    const auto* charge = world.store<simulation::Charge>().find(candidate->attacker);
    if (charge == nullptr || charge->activation_tick() != candidate->activation_tick ||
        !charge->active_window().contains(tick)) {
      continue;
    }
    // Consuming the component or ending its active window makes every later candidate for this
    // activation ineligible, including a repeated observation or another target in the same tick.
    if (candidate->outcome == simulation::ChargeContactOutcome::kSuccessfulHit) {
      requested.push_back({candidate->target, charge->hit_stun_duration_ticks()});
      world.mutable_store<simulation::Charge>().erase(candidate->attacker);
    } else {
      world.mutable_store<simulation::Charge>().insert_or_assign(candidate->attacker,
                                                                 charge->canceled_at(tick));
    }
  }
  for (const auto& request : requested) {
    world.emit(request);
  }
}

} // namespace blob_royale::gameplay
