#ifndef BLOB_ROYALE_CONTROLLERS_RACER_CONTROLLER_HPP
#define BLOB_ROYALE_CONTROLLERS_RACER_CONTROLLER_HPP

#include "controller.hpp"
#include "controller_id.hpp"
#include "controllers_limits.hpp"
#include "observation.hpp"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace blob_royale::controllers {

// canonical: racer_controller -- seek the next published gate, recovering toward the centreline.
// @extension-point controller
//
// Reads only the race block and its own RaceProgress/body from the observation. Before progress
// exists it waits on the grid; after the last gate it submits zero thrust. A bodyless participant
// waits for the mode's return rather than requesting a second entity. Near the corridor edge the
// target becomes the nearest centreline point, with the earliest segment winning an exact tie.
// The seed is retained for the registry's reproducibility contract; this policy draws no random
// values, so identical observations and personalities produce identical decisions under any seed.
// related: controller_registry.hpp -- the one registration.
// related: ../simulation/mode_states/race_mode_state.hpp -- the published course, not mode rules.
class RacerController final : public Controller {
public:
  static constexpr std::string_view kControllerKind = "racer";

  struct Personality final {
    // Recovery starts strictly beyond this fraction of the published corridor half-width.
    // Finite and in (0, 1]; the default leaves one quarter of the width for changing direction.
    double caution_fraction{kDefaultRacerCautionFraction};
  };

  [[nodiscard]] static std::unique_ptr<Controller> create(simulation::ControllerId controller,
                                                          std::uint64_t seed);

  // Rejects non-finite or out-of-range caution with CONTROLLERS.RACER_CAUTION_FRACTION_*.
  [[nodiscard]] static std::unique_ptr<Controller>
  create(simulation::ControllerId controller, std::uint64_t seed, Personality personality);

  // `create` is the validating entry point; public for std::make_unique, like the other bots.
  RacerController(simulation::ControllerId controller, std::uint64_t seed,
                  Personality personality) noexcept;

  [[nodiscard]] std::string_view kind() const noexcept override { return kControllerKind; }
  [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }
  [[nodiscard]] const Personality& personality() const& noexcept { return personality_; }
  [[nodiscard]] const Personality& personality() const&& = delete;

private:
  [[nodiscard]] std::vector<simulation::Command>
  decide_from_observation(const Observation& observation) override;

  std::uint64_t seed_;
  Personality personality_;
};

} // namespace blob_royale::controllers

#endif
