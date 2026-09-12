#ifndef BLOB_ROYALE_GAMEPLAY_SHARED_CREATE_CROSSING_HAZARD_HPP
#define BLOB_ROYALE_GAMEPLAY_SHARED_CREATE_CROSSING_HAZARD_HPP

#include "entity_id.hpp"
#include "motion_contact_observation.hpp"

#include <optional>

namespace blob_royale::simulation {
class ArenaBounds;
class GameWorld;
} // namespace blob_royale::simulation

namespace blob_royale::gameplay {

class HazardArchetype;

// canonical: create_crossing_hazard -- draw and seat one complete crossing hazard.
//
// The caller owns scheduling and must check the body's capacity and this tick's remaining entity
// reservation before calling. This operation consumes three hazards-stream draws, one reserved id,
// and writes PhysicsBody, Lifetime, and the optional LethalOnContact marker. Bounds and archetype
// are validated values; seconds_per_tick is the positive duration supplied by TickContext.
// Returns the created EntityId. SimulationValidationError from body/store/id validation propagates;
// the enclosing GameSimulation transaction, not this operation, owns rollback on failure.
//
// The zero drag and crossing bounds behavior are one decision with hazard_lifetime_ticks: the
// lifetime's distance/speed arithmetic describes a body that neither slows nor reflects at walls.
// related: shared/hazard_crossing.hpp -- unchanged canonical draw and lifetime arithmetic.
// related: shared/hazard_spawn_system.hpp -- owns due-ness and pre-draw capacity checks.
// Instance policy overrides the archetype default; explicit closing impact means sparse absence.
// Invalid override values fail before consuming randomness or an id.
[[nodiscard]] simulation::EntityId create_crossing_hazard(
    simulation::GameWorld& world, const simulation::ArenaBounds& bounds,
    const HazardArchetype& archetype, double seconds_per_tick,
    std::optional<simulation::ContactEffectPolicy> instance_override = std::nullopt);

} // namespace blob_royale::gameplay

#endif
