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
// mode declares stays in that mode's directory. Nothing here is mode-specific: every declaring
// mode reads the same match-owned movement tuning.
//
// The system is stateless: every value it reads or changes is world-owned, so a tick's result stays
// a function of the committed world and the tick's InputBatch alone.
//
// It runs at `kPreKernel` because that stage reads this tick's recorded commands and start-of-tick
// positions and writes body intent, and nothing has moved yet: an entity phase 0 seated this tick
// can be thrust in the same tick and phase 1 applies the acceleration written here
// (`docs/architecture/0005-royale-mode.md` § "Steering").
// related: steered_acceleration -- the arithmetic, named and testable on its own.
// related: sandbox/sandbox_mode.hpp -- one of the two modes that declare it.

// Standalone direction-to-acceleration arithmetic, delegated to the canonical locomotion helpers.
// It retains its original scalar domain; only live tuning goes through MovementTuning and the cap.
//
// **The magnitude clamp happens exactly once.** `ThrustCommand` carries the submitted
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

class ThrustSteeringSystem final : public simulation::SimulationSystem {
public:
  // The stable name this system is known by in the pipeline, diagnostics, and fixtures. Two modes
  // declaring it declare the same name, and a pipeline holds one mode's systems, so the names never
  // collide.
  static constexpr std::string_view kSystemName = "thrust_steering";

  // Builds the stateless system; validated tuning belongs to MatchState, not a mode or system.
  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem> create();

  // Public because the pipeline holds `std::unique_ptr<const SimulationSystem>` and
  // `std::make_unique` needs an accessible constructor, exactly as `MatchLifecycleSystem` does.
  ThrustSteeringSystem() = default;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // Writes `PhysicsBody::acceleration` for every entity that carries both a Controllable and a
  // PhysicsBody, in ascending EntityId order and without a phase gate. Active input lock writes
  // explicit zero intent and acceleration before command reading. Outside a lock, recorded thrust
  // replaces private intent only on exact optional generation equality, including zero releases.
  // A held intent recomputes acceleration from current tuning and
  // velocity every tick through locomotion's finite-step cap. Absent intent preserves authored
  // acceleration, unlike explicit zero. A bodyless entity is skipped. Seating clears old intent.
  // Canonical integration/Vector2 domain errors and LOCOMOTION_PRECISION_LOST propagate to the
  // tick's existing transactional boundary; there is no coast fallback.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;
};

} // namespace blob_royale::gameplay

#endif
