#ifndef BLOB_ROYALE_CONTROLLERS_CHASER_CONTROLLER_HPP
#define BLOB_ROYALE_CONTROLLERS_CHASER_CONTROLLER_HPP

#include "command_registry.hpp"
#include "controller.hpp"
#include "controller_id.hpp"
#include "controllers_limits.hpp"
#include "entity_id.hpp"
#include "observation.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace blob_royale::controllers {

// canonical: chaser_controller -- thrust toward the nearest other controllable entity.
// @extension-point controller
//
// The second registered bot, and the first one whose decision is a function of the *world* rather
// than of its own state. It asks for a body when it has none; otherwise it finds the nearest entity
// that is not itself and carries both a body and a controller link, and thrusts along the unit
// vector toward it, scaled by its personality's aggression weight.
//
// **"Another controllable entity" is exactly `WorldSnapshot::players()`.** That span is the
// materialized ordered merge of the `PhysicsBody` and `Controllable` stores -- the entities that
// carry both, in ascending `EntityId` -- so walls, obstacles, and royale's zone entity are excluded
// by construction rather than by a filter this file would have to keep correct. It is read here
// rather than re-deriving the merge because `component_join.hpp`'s canonical merge takes two
// `ComponentStore` values and a published snapshot hands out spans, so the canonical join is not
// callable on a snapshot; `players()` is that same merge, already performed once at publication.
//
// **The tie-break is the lowest `EntityId`, and it is stated because it is observable.** Two
// candidates exactly equidistant from the chaser is not a hypothetical -- a symmetric spawn ring
// produces it on the first tick of every match -- and an unstated tie-break would make the bot's
// behavior a property of iteration order. `players()` is ascending `EntityId` and the comparison is
// strict, so the first candidate seen wins a tie, which is the lowest `EntityId`. That is also the
// oldest body, which is the only one of the two that means anything to a player watching.
//
// **Two states decide nothing at all**, and both are ordinary rather than errors: a chaser alone in
// the world has no target, and a chaser exactly co-located with its target has no direction toward
// it. Both return an empty command list, which leaves the stored acceleration exactly as it was.
//
// **The aggression weight is a personality, not a subclass.** A timid chaser and a relentless one
// are two `Personality` values (ADR 0004 § "Controllers": personalities are configuration values,
// not new types).
// related: controller_registry.hpp -- the row that resolves "chaser".
// related: wanderer_controller.hpp -- the first registered bot.
// related: ../simulation/player_snapshot.hpp -- the published join this reads.
class ChaserController final : public Controller {
public:
  static constexpr std::string_view kControllerKind = "chaser";

  // Full commitment: the unit vector toward the target, unscaled.
  static constexpr double kDefaultAggressionWeight = kMaximumChaserAggressionWeight;

  // One chaser's tunable behavior.
  struct Personality final {
    // Scales the unit direction toward the target, in
    // `[kMinimumChaserAggressionWeight, kMaximumChaserAggressionWeight]`. `1.0` thrusts at the
    // mode's full declared maximum; `0.25` thrusts at a quarter of it and produces a bot that
    // closes slowly and overshoots less; `0.0` is a pacifist that brakes to a stop and stays put.
    double aggression_weight{kDefaultAggressionWeight};
  };

  // The registry factory shape. The seed is accepted and unused: a chaser draws no randomness, and
  // one uniform factory signature is what keeps a roster line from having to know which kinds are
  // seeded.
  [[nodiscard]] static std::unique_ptr<Controller> create(simulation::ControllerId controller,
                                                          std::uint64_t seed);

  // The configured form. Throws ControllersValidationError with
  // `CONTROLLERS.CHASER_AGGRESSION_WEIGHT_NOT_FINITE` for a non-finite weight and
  // `CONTROLLERS.CHASER_AGGRESSION_WEIGHT_OUT_OF_RANGE` outside the accepted range, because a
  // weight above one would scale a unit direction past the thrust component range the
  // `CommandSink` refuses, so every command the bot produced would be silently dropped.
  [[nodiscard]] static std::unique_ptr<Controller>
  create(simulation::ControllerId controller, std::uint64_t seed, Personality personality);

  // Public because `create` hands the controller over as a `std::unique_ptr<Controller>` and
  // `std::make_unique` needs an accessible constructor. **`create` is the validating entry point**;
  // this one takes the personality as given.
  ChaserController(simulation::ControllerId controller, Personality personality) noexcept;

  [[nodiscard]] std::string_view kind() const noexcept override { return kControllerKind; }

  [[nodiscard]] const Personality& personality() const& noexcept { return personality_; }
  [[nodiscard]] const Personality& personality() const&& = delete;

  // The entity this chaser chose at its most recent decision, or `std::nullopt` when it chose none
  // because it had no body, no candidate, or no direction. Published so a test and a diagnostic can
  // read the target selection without inferring it from a thrust vector.
  [[nodiscard]] std::optional<simulation::EntityId> target() const noexcept { return target_; }

private:
  [[nodiscard]] std::vector<simulation::Command>
  decide_from_observation(const Observation& observation) override;

  Personality personality_;
  std::optional<simulation::EntityId> target_;
};

} // namespace blob_royale::controllers

#endif
