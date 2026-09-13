#ifndef BLOB_ROYALE_SIMULATION_MOVEMENT_TUNING_STATE_HPP
#define BLOB_ROYALE_SIMULATION_MOVEMENT_TUNING_STATE_HPP

#include "movement_tuning.hpp"
#include "tick_sequence.hpp"

#include <cstdint>

namespace blob_royale::simulation {

// canonical: movement_tuning_state -- current room tuning and its authored reset target.
// MatchState owns this value; round reset preserves it. Only a winning phase-0 tuning command
// advances revision/effective_tick. Defaults are seeded once by the composition root.
struct MovementTuningState final {
  MovementTuning current{MovementTuning::defaults()};
  MovementTuning defaults{MovementTuning::defaults()};
  std::uint64_t revision{0};
  TickSequence effective_tick{TickSequence::zero()};
  // Seeded once by the composition root. Zero rate maximum means no authored archetype of
  // that class. Charge's maximum is constrained by the room's resultant-speed envelope.
  double charge_speed_fraction_maximum{kMaximumChargeSpeedFraction};
  double lethal_spawn_rate_per_second_maximum{kMaximumCrossingSpawnRatePerSecond};
  double nonlethal_spawn_rate_per_second_maximum{kMaximumCrossingSpawnRatePerSecond};

  [[nodiscard]] bool admits(const MovementTuning& tuning) const noexcept {
    return tuning.charge_speed_fraction() <= charge_speed_fraction_maximum &&
           tuning.lethal_spawn_rate_per_second() <= lethal_spawn_rate_per_second_maximum &&
           tuning.nonlethal_spawn_rate_per_second() <= nonlethal_spawn_rate_per_second_maximum;
  }

  friend bool operator==(const MovementTuningState&, const MovementTuningState&) = default;
};

} // namespace blob_royale::simulation

#endif
