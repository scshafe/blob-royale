#include "shared/status_system.hpp"

#include "components/controllable_component.hpp"
#include "components/shield_component.hpp"
#include "components/stun_component.hpp"
#include "events/stun_request_event.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "physics_body.hpp"
#include "tick_context.hpp"
#include "tick_window.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <variant>
#include <vector>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::SimulationSystem> StatusSystem::create() {
  return std::make_unique<const StatusSystem>();
}

void StatusSystem::apply(simulation::GameWorld& world,
                         const simulation::TickContext& context) const {
  const auto tick = context.tick_sequence();
  std::map<simulation::EntityId, simulation::TickWindow> requested;
  // Aggregation is read-only, including existing-status expiry. A later invalid request cannot
  // leave earlier targets, generations, intent, acceleration, or expired-status cleanup changed.
  for (const auto& event : world.events()) {
    const auto* request = std::get_if<simulation::StunRequest>(&event);
    if (request == nullptr || request->duration_ticks == 0) {
      continue;
    }
    const auto* body = world.store<simulation::PhysicsBody>().find(request->entity);
    if (body == nullptr || body->is_static()) {
      continue;
    }
    if (tick == simulation::TickSequence::zero()) {
      throw GameplayValidationError(GameplayValidationCode::kStatusActivationTickZero,
                                    "status.activation_tick",
                                    "positive stun requests require a positive committing tick");
    }
    auto window = simulation::TickWindow::create(tick, request->duration_ticks);
    const auto* existing = world.store<simulation::Stun>().find(request->entity);
    const auto pending = requested.find(request->entity);
    const simulation::TickWindow* previous =
        pending != requested.end() ? &pending->second
                                   : (existing != nullptr ? &existing->window : nullptr);
    if (previous != nullptr && previous->contains(tick)) {
      const auto activation = previous->activation_tick();
      const auto expiry = std::max(previous->expiry_tick(), window.expiry_tick());
      window = simulation::TickWindow::create(activation, expiry.value() - activation.value());
    }
    requested.insert_or_assign(request->entity, window);
  }

  std::vector<simulation::EntityId> expired;
  for (const auto& entry : world.store<simulation::Stun>().entries()) {
    if (entry.value.window.expired(tick)) {
      expired.push_back(entry.entity);
    }
  }
  for (const auto entity : expired) {
    world.mutable_store<simulation::Stun>().erase(entity);
  }
  for (const auto& [entity, window] : requested) {
    world.mutable_store<simulation::Stun>().insert_or_assign(entity, simulation::Stun{window});
    if (auto* controllable = world.mutable_store<simulation::Controllable>().mutable_find(entity);
        controllable != nullptr) {
      // Every applicable request invalidates input, including one an older longer window covers.
      controllable->input_generation = tick;
    }
  }

  const auto zero = simulation::Vector2::create(0.0, 0.0);
  for (const auto& entry : world.store<simulation::Stun>().entries()) {
    if (!entry.value.window.contains(tick)) {
      continue;
    }
    if (auto* controllable =
            world.mutable_store<simulation::Controllable>().mutable_find(entry.entity);
        controllable != nullptr) {
      controllable->normalized_thrust_intent = zero;
    }
    if (auto* body = world.mutable_store<simulation::PhysicsBody>().mutable_find(entry.entity);
        body != nullptr && !body->is_static()) {
      *body = body->with_acceleration(zero);
    }
    // A stun ends a guard. It shortens **only** still-active protection to this tick and leaves the
    // original activation, any already-elapsed perfect history, the cooldown window and the
    // captured parry-stun duration exactly as they were, so a player stunned mid-guard pays the
    // full cooldown rather than being handed a free re-tap
    // (`../../simulation/components/shield_component.hpp` states the same of `canceled_at`).
    // Cancelling protection that has already ended is an exact no-op, so a stun after expiry
    // changes the component not at all.
    //
    // **This never erases a Shield.** A cancelled protection with a live cooldown is a component
    // that still has work to do, and only `shared/ability_system.hpp` removes one, once both
    // windows have expired. It also belongs here and not in the aggregation loop above, whose
    // stated invariant is that a later invalid request leaves nothing earlier changed.
    if (const auto* shield = world.store<simulation::Shield>().find(entry.entity);
        shield != nullptr) {
      world.mutable_store<simulation::Shield>().insert_or_assign(entry.entity,
                                                                 shield->canceled_at(tick));
    }
  }
}

} // namespace blob_royale::gameplay
