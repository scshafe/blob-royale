#include "royale/elimination_grace_publisher_system.hpp"

#include "game_world.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "royale/royale_mode_state.hpp"
#include "tick_context.hpp"

#include <cstdint>
#include <memory>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::SimulationSystem>
EliminationGracePublisherSystem::create(const std::uint64_t elimination_grace_ticks) {
  return std::make_unique<const EliminationGracePublisherSystem>(elimination_grace_ticks);
}

EliminationGracePublisherSystem::EliminationGracePublisherSystem(
    const std::uint64_t elimination_grace_ticks) noexcept
    : elimination_grace_ticks_(elimination_grace_ticks) {}

void EliminationGracePublisherSystem::apply(simulation::GameWorld& world,
                                            const simulation::TickContext&) const {
  // Unconditional: there is no phase in which a client stops needing the denominator of a counter
  // the snapshot still carries. A `ZoneExposure` written on the last `running` tick keeps being
  // published through `ended`, so a grace published only while `running` would leave the client
  // reading that counter against nothing for the length of the restart delay.
  royale_mode_state_in(world).elimination_grace_ticks = elimination_grace_ticks_;
}

} // namespace blob_royale::gameplay
