#ifndef BLOB_ROYALE_APPLICATION_MATCH_STARTUP_VALIDATION_HPP
#define BLOB_ROYALE_APPLICATION_MATCH_STARTUP_VALIDATION_HPP

#include "match_configuration.hpp"

#include "shared/hazard_archetype.hpp"

#include "map_definition.hpp"
#include "simulation_config.hpp"

#include <span>

namespace blob_royale::application {

// canonical: match_startup_validation -- the cross-value startup rules a loaded match must satisfy.
//
// Each rule below spans two independently validated values, so neither value can own it: a map is
// authored without a configuration, a roster is authored without a map, and a protocol bound
// belongs to neither. They run once, before the first tick, so a configuration that could only
// fail mid-match fails at startup with a named cause instead.
// related: match_configuration.hpp -- the roster half.
// related: map_loader.hpp -- the map half.
// related: gameplay/shared/hazard_crossing.hpp -- the hazard half's arithmetic, owned there.

// Rejects a match whose worst-case population cannot be published in one snapshot.
//
// `snapshot-data.schema.json` bounds `entities` at 1,024 and **every published entity counts**:
// the map's static bodies, one entity a mode may create for itself (royale's zone), every seat a
// browser can take, every bot the roster names, and every hazard that can be in the air at the
// same moment. The encoder refuses a frame above the bound rather than dropping an entity, so a
// configuration that could reach it would serve a match that stops publishing partway through --
// to every client at once. It is arithmetic over values that are all known before the first tick,
// so it is a startup rejection.
//
// **The hazard table is a parameter rather than a lookup, and that is the whole reason this
// function takes three arguments.** Archetypes hang off `GameModeConfiguration` while a map and a
// roster are authored independently of any mode, so no one of the three values can own the rule;
// the composition root, which holds all three, passes them in. Passing a span rather than the mode
// configuration keeps this function's dependency the table itself: a mode that declares no hazards
// passes an empty span and the arithmetic below is unchanged from before hazards existed.
//
// **The per-kind arithmetic is not reproduced here.** How far a crossing can be and how long one
// lives are `gameplay/shared/hazard_crossing.hpp`'s, and `hazard_spawn_system` calls the same two
// functions at the distance it actually drew. A bound computed from a private copy of the spawner's
// formula would agree with the spawner exactly until one of the two was edited.
//
// Throws ApplicationInputError with `APPLICATION.MATCH.ENTITY_BUDGET_EXCEEDED`, naming every term
// including, per declared kind, how many of that kind the bound expects to be standing -- so an
// operator reading the rejection can tell which `[hazard.<kind>]` knob to turn.
void require_match_fits_snapshot_bound(
    const MatchConfiguration& match_configuration, const simulation::MapDefinition& map,
    std::span<const gameplay::HazardArchetype> hazard_archetypes);

// Rejects a map whose arena disagrees with the world scalars the configuration publishes.
//
// The kernel folds against `MapDefinition::bounds()`, while protocol v1's `/api/v1/config` serves
// `[world] width_world_units` and `[world] height_world_units` through `PublicConfiguration` and
// `ScenarioLoader` validates a seeded centre against them. Two arenas that disagree would draw a
// client's canvas at one size and simulate at another, with no error anywhere, so the two are
// required to be the same rectangle until the `[world]` keys retire into the map file.
//
// Throws ApplicationInputError with `APPLICATION.MATCH.MAP_BOUNDS_MISMATCH`.
void require_map_matches_published_world(const simulation::SimulationConfig& simulation_config,
                                         const simulation::MapDefinition& map);

} // namespace blob_royale::application

#endif
