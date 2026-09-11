#ifndef BLOB_ROYALE_SIMULATION_MOVEMENT_TUNING_DECISIONS_HPP
#define BLOB_ROYALE_SIMULATION_MOVEMENT_TUNING_DECISIONS_HPP

#include "movement_tuning_decision.hpp"

#include <span>
#include <utility>
#include <vector>

namespace blob_royale::simulation {

// canonical: movement_tuning_decisions -- bounded, owned decisions released only after commit.
// GameSimulation reserves at most the canonical tuning-command count before any commit. This is
// a fixed result path, not an event bus; callers cannot obtain a view of a temporary result.
class MovementTuningDecisions final {
public:
  MovementTuningDecisions(const MovementTuningDecisions&) = delete;
  MovementTuningDecisions& operator=(const MovementTuningDecisions&) = delete;
  MovementTuningDecisions(MovementTuningDecisions&&) noexcept = default;
  MovementTuningDecisions& operator=(MovementTuningDecisions&&) noexcept = default;
  ~MovementTuningDecisions() = default;

  [[nodiscard]] std::span<const MovementTuningDecision> entries() const& noexcept {
    return entries_;
  }
  [[nodiscard]] std::span<const MovementTuningDecision> entries() const&& = delete;

private:
  friend class GameSimulation;
  explicit MovementTuningDecisions(std::vector<MovementTuningDecision> entries) noexcept
      : entries_(std::move(entries)) {}

  std::vector<MovementTuningDecision> entries_;
};

} // namespace blob_royale::simulation

#endif
