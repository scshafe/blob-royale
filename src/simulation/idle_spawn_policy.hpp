#ifndef BLOB_ROYALE_SIMULATION_IDLE_SPAWN_POLICY_HPP
#define BLOB_ROYALE_SIMULATION_IDLE_SPAWN_POLICY_HPP

#include "entity_id.hpp"
#include "spawn_policy.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace blob_royale::simulation {

// canonical: idle_spawn_policy -- the spawn policy the engine declares when no mode does.
//
// It defers every entity forever, so a spawn command creates its entity and leaves it unseated and
// no body ever appears at a position no declaration chose. Creating the entity is kernel mechanism
// and seating it is the mode's policy, so "no mode" cannot mean "seat it anywhere"
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick" phase 0).
// related: idle_match_objective.hpp -- the matching declaration for the other policy socket.
class IdleSpawnPolicy final : public SpawnPolicy {
public:
  IdleSpawnPolicy() = default;

  [[nodiscard]] std::optional<std::size_t>
  choose_spawn_point(const GameWorld&, const TickContext&, EntityId, std::size_t,
                     std::span<const bool>) const override {
    return std::nullopt;
  }
};

} // namespace blob_royale::simulation

#endif
