#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_THRUST_STEERING_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_THRUST_STEERING_SYSTEM_HPP

#include "simulation_system.hpp"
#include "vector2.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: thrust_steering -- the one system that turns a thrust command into stored
// acceleration.
// @extension-point simulation_system
//
// **Where a system shared by more than one mode lives.** ADR 0004 § "Libraries, and where a new
// thing goes" puts a mechanic at `src/gameplay/<mode>/<name>_system.{hpp,cpp}`, which is right for
// a mechanic only one mode declares. `thrust_steering` is declared by `sandbox` and, from plan
// Step 21, by `royale`, and both of ADR 0004's other stress-test modes declare it too, so filing it
// under one mode's directory would make the second mode reach into the first.
// `src/gameplay/shared/` is the directory for a system more than one mode declares; a system one
// mode declares stays in that mode's directory. Nothing here is mode-specific: the scale is a
// constructor argument, so two modes that disagree about thrust strength declare the same system
// twice with different numbers.
//
// The system holds one immutable scalar and nothing else, which is the whole of what
// `simulation_system.hpp` permits: every value it changes is world-owned, so a tick's result stays
// a function of the committed world and the tick's InputBatch alone.
//
// It runs at `kPreKernel` because that stage reads this tick's recorded commands and start-of-tick
// positions and writes body intent, and nothing has moved yet: an entity phase 0 seated this tick
// can be thrust in the same tick and phase 1 applies the acceleration written here
// (`docs/architecture/0005-royale-mode.md` § "Steering").
// related: steered_acceleration -- the arithmetic, named and testable on its own.
// related: sandbox/sandbox_mode.hpp -- one of the two modes that declare it.

// canonical: thrust_steering_acceleration -- the one place a thrust direction becomes acceleration.
//
// **The magnitude clamp happens exactly once, here.** `ThrustCommand` carries the submitted
// direction verbatim and `InputBatch::create` only range-checks each component against `[-1, 1]`,
// because clamping at construction and again in this system would scale twice and is not
// bit-identical to scaling once (`commands/thrust_command.hpp`).
//
// The written operation order is the contract of `docs/architecture/0005-royale-mode.md`
// § "Steering", which `docs/architecture/0003-deterministic-simulation-contract.md`
// § "Floating-point contract" forbids reassociating:
//
//     m = sqrt(x * x + y * y)
//     s = 1        when m <= 1
//     s = 1 / m    when m > 1
//     acceleration = ((x * s) * thrust_max, (y * s) * thrust_max)
//
// `sqrt(x * x + y * y)` is written out rather than delegated to `std::hypot`, which computes a
// different binary64 value for the same inputs and would silently move every accepted horizon.
//
// A direction inside the unit disc keeps its magnitude, so an analog stick produces proportional
// thrust; `(1, 1)` normalizes to `(0.7071..., 0.7071...)` and yields an acceleration of magnitude
// exactly `thrust_max`, so diagonal movement carries no advantage; `(0, 0)` stores zero
// acceleration, which is the coast command.
[[nodiscard]] simulation::Vector2 steered_acceleration(const simulation::Vector2& direction,
                                                       double thrust_max);

// canonical: thrust_maximum_validation -- the one rule a declared thrust maximum must satisfy.
//
// Finite and greater than or equal to zero (`docs/architecture/0005-royale-mode.md`
// § "Mode configuration"). It is a free function rather than a step inside the system's factory so
// a mode can reject a mis-typed balance number where it reads it, without constructing a system it
// would immediately discard. Throws GameplayValidationError naming which of the two rules failed.
void require_valid_thrust_maximum(double thrust_max_world_units_per_second_squared);

class ThrustSteeringSystem final : public simulation::SimulationSystem {
public:
  // The stable name this system is known by in the pipeline, diagnostics, and fixtures. Two modes
  // declaring it declare the same name, and a pipeline holds one mode's systems, so the names never
  // collide.
  static constexpr std::string_view kSystemName = "thrust_steering";

  // Builds the system a mode declares, through `require_valid_thrust_maximum`, so a mis-typed
  // balance number is a startup rejection rather than a world full of non-finite accelerations on
  // the first thrust.
  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(double thrust_max_world_units_per_second_squared);

  // Public because the pipeline holds `std::unique_ptr<const SimulationSystem>` and
  // `std::make_unique` needs an accessible constructor, exactly as `MatchLifecycleSystem` does.
  // **`create` is the validating entry point**; this one takes the number as given.
  explicit ThrustSteeringSystem(double thrust_max_world_units_per_second_squared) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // Writes `PhysicsBody::acceleration` for every entity that carries both a Controllable and a
  // PhysicsBody and whose recorded commands include a thrust, in ascending EntityId order. An
  // entity with no recorded thrust keeps the acceleration its last thrust stored, which is the
  // persistence ADR 0003 § "State, units, and fixed time" already specifies; a thrust naming an
  // entity that owns no body has nothing to write and is skipped.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  double thrust_max_;
};

} // namespace blob_royale::gameplay

#endif
