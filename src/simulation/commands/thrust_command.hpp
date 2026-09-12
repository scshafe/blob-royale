#ifndef BLOB_ROYALE_SIMULATION_COMMANDS_THRUST_COMMAND_HPP
#define BLOB_ROYALE_SIMULATION_COMMANDS_THRUST_COMMAND_HPP

#include "entity_id.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <optional>

namespace blob_royale::simulation {

// canonical: thrust_command -- one entity's steering intent for one tick.
//
// `direction` carries each component in [-1, 1]. Components outside that range are rejected in
// InputBatch::create and never reach the world
// (`docs/architecture/0005-royale-mode.md` § "Steering"); a non-finite component cannot be built
// at all, because Vector2::create rejects it first.
//
// The vector is stored exactly as submitted and is **not** clamped here. The magnitude clamp
// `s = 1` when `m <= 1`, `s = 1 / m` when `m > 1` belongs to the mode's steering system at
// `kPreKernel`, whose written operation order is the contract and which
// `docs/architecture/0003-deterministic-simulation-contract.md` § "Floating-point contract"
// forbids reassociating. Clamping at construction and clamping again in the system would scale
// twice and is not bit-identical to scaling once, so `(1, 1)` is carried verbatim and normalized
// exactly once, by the system.
//
// The command is intent, not effect: a thrust names no acceleration and no maximum. The mode
// supplies its own `thrust_max` when it turns this direction into `PhysicsBody::acceleration`,
// which then persists until that entity's next thrust.
// related: command_registry.hpp -- the closed list of command kinds.
// related: input_batch.hpp -- where the component range is validated.
struct ThrustCommand final {
  EntityId entity;
  Vector2 direction;
  // Exact activation token, including absence. Present zero is rejected by InputBatch.
  std::optional<TickSequence> input_generation{};

  friend bool operator==(const ThrustCommand&, const ThrustCommand&) = default;
};

} // namespace blob_royale::simulation

#endif
