#ifndef BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_HILL_RULES_PUBLISHER_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_KING_OF_THE_HILL_HILL_RULES_PUBLISHER_SYSTEM_HPP

#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: hill_rules_publisher -- puts the hill's three denominators on the wire.
// @extension-point simulation_system
//
// It writes king of the hill's mode-state block -- `points_to_win`, `point_interval_ticks`,
// `time_limit_ticks` -- and does nothing else. It reads no world state, emits no event, creates
// and destroys nothing, and its output is the same three numbers on every tick of a process.
//
// **Why a system exists for three constants** is the reason `elimination_grace_publisher` exists
// for one: a snapshot encoder can see only the world, the numbers are `[king_of_the_hill]` keys the
// mode holds, and a client reading a score, a presence counter, and a clock without their bounds
// cannot say how close anyone is to winning. **Why it is declared last**: it is the sole writer of
// the block, and running last is what makes the published values independent of any other system
// (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Mode state and the wire").
//
// The consequence worth stating: **no committed hill tick ever publishes an unstamped zero.** The
// first snapshot a session can receive is the first completed tick, and this ran on it.
// related: king_of_the_hill_mode_state.hpp -- the one answer to "what if another arm is held".
// related: ../../simulation/mode_states/king_of_the_hill_mode_state.hpp -- the block this writes.
class HillRulesPublisherSystem final : public simulation::SimulationSystem {
public:
  static constexpr std::string_view kSystemName = "hill_rules_publisher";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(KingOfTheHillConfiguration configuration);

  explicit HillRulesPublisherSystem(KingOfTheHillConfiguration configuration) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // Never throws and never fails: there is no world state it could find inconsistent.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  KingOfTheHillConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
