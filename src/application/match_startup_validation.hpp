#ifndef BLOB_ROYALE_APPLICATION_MATCH_STARTUP_VALIDATION_HPP
#define BLOB_ROYALE_APPLICATION_MATCH_STARTUP_VALIDATION_HPP

#include "map_definition.hpp"
#include "match_configuration.hpp"
#include "simulation_config.hpp"

namespace blob_royale::application {

// canonical: match_startup_validation -- the cross-value startup rules a loaded match must satisfy.
//
// Each rule below spans two independently validated values, so neither value can own it: a map is
// authored without a configuration, a roster is authored without a map, and a protocol bound
// belongs to neither. They run once, before the first tick, so a configuration that could only
// fail mid-match fails at startup with a named cause instead.
// related: match_configuration.hpp -- the roster half.
// related: map_loader.hpp -- the map half.

// Rejects a match whose worst-case population cannot be published in one snapshot.
//
// `snapshot-data.schema.json` bounds `entities` at 1,024 and **every published entity counts**:
// the map's static bodies, one entity a mode may create for itself (royale's zone), every seat a
// browser can take, and every bot the roster names. The encoder refuses a frame above the bound
// rather than dropping an entity, so a configuration that could reach it would serve a match that
// stops publishing partway through -- to every client at once. It is arithmetic over values that
// are all known before the first tick, so it is a startup rejection.
//
// Throws ApplicationInputError with `APPLICATION.MATCH.ENTITY_BUDGET_EXCEEDED`, naming every term.
void require_match_fits_snapshot_bound(const MatchConfiguration& match_configuration,
                                       const simulation::MapDefinition& map);

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
