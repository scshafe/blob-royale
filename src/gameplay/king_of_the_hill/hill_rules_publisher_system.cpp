#include "king_of_the_hill/hill_rules_publisher_system.hpp"

#include "game_world.hpp"
#include "king_of_the_hill/king_of_the_hill_mode_state.hpp"
#include "mode_states/king_of_the_hill_mode_state.hpp"
#include "tick_context.hpp"

#include <memory>
#include <utility>

namespace blob_royale::gameplay {

std::unique_ptr<const simulation::SimulationSystem>
HillRulesPublisherSystem::create(KingOfTheHillConfiguration configuration) {
  return std::make_unique<const HillRulesPublisherSystem>(std::move(configuration));
}

HillRulesPublisherSystem::HillRulesPublisherSystem(
    KingOfTheHillConfiguration configuration) noexcept
    : configuration_(std::move(configuration)) {}

void HillRulesPublisherSystem::apply(simulation::GameWorld& world,
                                     const simulation::TickContext&) const {
  simulation::KingOfTheHillModeState& block = king_of_the_hill_mode_state_in(world);
  block.points_to_win = configuration_.points_to_win();
  block.point_interval_ticks = configuration_.point_interval_ticks();
  block.time_limit_ticks = configuration_.time_limit_ticks();
}

} // namespace blob_royale::gameplay
