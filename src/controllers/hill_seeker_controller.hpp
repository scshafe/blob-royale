#ifndef BLOB_ROYALE_CONTROLLERS_HILL_SEEKER_CONTROLLER_HPP
#define BLOB_ROYALE_CONTROLLERS_HILL_SEEKER_CONTROLLER_HPP

#include "command_registry.hpp"
#include "controller.hpp"
#include "controller_id.hpp"
#include "controllers_limits.hpp"
#include "deterministic_random.hpp"
#include "entity_id.hpp"
#include "observation.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace blob_royale::controllers {

// canonical: hill_seeker_controller -- thrust toward the hill's centre and hold there.
// @extension-point controller
//
// The third registered bot, and the first whose decision reads a *mode's* entity: it asks for a
// body when it has none; otherwise it finds the snapshot's `hill` entity and thrusts toward its
// centre, scaled by its personality's approach weight and nudged by a seeded jitter.
//
// **"The hill" is the first entity carrying `Hill`, in ascending `EntityId`.** `king_of_the_hill`
// creates exactly one (`src/gameplay/king_of_the_hill/hill_movement_system.hpp`), so "first" is
// "the"; it is stated because it is the tie-break a second hill would observe. A world with no hill
// -- `sandbox`, `royale` -- gives this bot nothing to seek, and it decides nothing rather than
// wandering, because a seeker that wanders is a wanderer with a misleading name.
//
// **Inside the hill the heading shrinks, which is what "hold" means here.** The direction is the
// offset to the centre divided by the larger of the distance and the hill's radius: a unit vector
// while the centre is farther than one radius away, and a proportional pull that reaches zero at
// the centre once inside. The mode's drag then brings the body to rest near the centre instead of
// the body thrusting through it and back, which is what a unit heading held everywhere would do.
//
// **The jitter is why two seekers do not stack.** Every decision adds a seeded offset of at most
// `jitter_weight` to each heading component, so two seekers under different seeds head for the
// same hill along different lines and settle on different spots of it. Under the same seed and the
// same observations they are the same bot (ADR 0004 § "Controllers": a deterministic bot owns a
// `DeterministicRandom` seeded from match configuration). Every decision takes exactly two draws,
// in a written order, so the draw count is a function of the decisions taken and not of the
// personality's jitter being zero.
//
// **Both weights are a personality, not a subclass.** A shy seeker that circles and a committed
// one that dives are two `Personality` values (ADR 0004 § "Controllers").
// related: controller_registry.hpp -- the row that resolves "hill_seeker".
// related: chaser_controller.hpp -- the second registered bot, whose target is a player instead.
// related: ../simulation/components/hill_component.hpp -- the entity this reads.
class HillSeekerController final : public Controller {
public:
  static constexpr std::string_view kControllerKind = "hill_seeker";

  // Full commitment toward the centre, and a nudge of at most fifteen hundredths on each component,
  // which is enough to separate two seekers on one hill and not enough to send one off it.
  static constexpr double kDefaultApproachWeight = kMaximumHillSeekerWeight;
  static constexpr double kDefaultJitterWeight = 0.15;

  // One seeker's tunable behavior.
  struct Personality final {
    // Scales the heading toward the centre, in `[kMinimumHillSeekerWeight,
    // kMaximumHillSeekerWeight]`. `1.0` thrusts at the mode's full declared maximum when far from
    // the hill; `0.0` never approaches and only jitters.
    double approach_weight{kDefaultApproachWeight};
    // The largest offset the seeded jitter adds to one heading component per decision, in the same
    // range. `0.0` is a seeker with no jitter at all, which two seeds cannot tell apart.
    double jitter_weight{kDefaultJitterWeight};
  };

  // The registry factory shape: a durable identity and a seed, with the declared default
  // personality.
  [[nodiscard]] static std::unique_ptr<Controller> create(simulation::ControllerId controller,
                                                          std::uint64_t seed);

  // The configured form. Throws ControllersValidationError with
  // `CONTROLLERS.HILL_SEEKER_WEIGHT_NOT_FINITE` for a non-finite weight and
  // `CONTROLLERS.HILL_SEEKER_WEIGHT_OUT_OF_RANGE` outside the accepted range, naming the member in
  // the context; a weight above one would push a heading component past the range the
  // `CommandSink` refuses on every decision rather than at the clamp's edge.
  [[nodiscard]] static std::unique_ptr<Controller>
  create(simulation::ControllerId controller, std::uint64_t seed, Personality personality);

  // Public because `create` hands the controller over as a `std::unique_ptr<Controller>` and
  // `std::make_unique` needs an accessible constructor. **`create` is the validating entry point**;
  // this one takes the personality as given.
  HillSeekerController(simulation::ControllerId controller, std::uint64_t seed,
                       Personality personality) noexcept;

  [[nodiscard]] std::string_view kind() const noexcept override { return kControllerKind; }

  [[nodiscard]] std::uint64_t seed() const noexcept { return random_.seed(); }

  // Draws taken so far: exactly two per decision that produced a thrust.
  [[nodiscard]] std::uint64_t draw_count() const noexcept { return random_.draw_count(); }

  [[nodiscard]] const Personality& personality() const& noexcept { return personality_; }
  [[nodiscard]] const Personality& personality() const&& = delete;

  // The hill entity this seeker read at its most recent decision, or `std::nullopt` when it read
  // none because it had no body or the world published no hill.
  [[nodiscard]] std::optional<simulation::EntityId> hill() const noexcept { return hill_; }

private:
  [[nodiscard]] std::vector<simulation::Command>
  decide_from_observation(const Observation& observation) override;

  simulation::DeterministicRandom random_;
  Personality personality_;
  std::optional<simulation::EntityId> hill_;
};

} // namespace blob_royale::controllers

#endif
