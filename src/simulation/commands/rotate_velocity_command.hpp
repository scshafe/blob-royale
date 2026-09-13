#ifndef BLOB_ROYALE_SIMULATION_COMMANDS_ROTATE_VELOCITY_COMMAND_HPP
#define BLOB_ROYALE_SIMULATION_COMMANDS_ROTATE_VELOCITY_COMMAND_HPP

#include "entity_id.hpp"
#include "tick_sequence.hpp"

#include <optional>

namespace blob_royale::simulation {

// canonical: rotate_velocity_command -- one exact quarter-turn of authoritative velocity.
// Screen/world y increases downward: clockwise is the right turn. The client supplies no speed.
struct RotateVelocityCommand final {
  EntityId entity;
  bool clockwise;
  std::optional<TickSequence> input_generation{};

  friend bool operator==(const RotateVelocityCommand&, const RotateVelocityCommand&) = default;
};

} // namespace blob_royale::simulation

#endif
