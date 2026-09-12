#ifndef BLOB_ROYALE_GAMEPLAY_GAME_MODE_CONFIGURATION_HPP
#define BLOB_ROYALE_GAMEPLAY_GAME_MODE_CONFIGURATION_HPP

#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "movement_tuning.hpp"
#include "race/race_configuration.hpp"
#include "royale/royale_configuration.hpp"
#include "sandbox/sandbox_configuration.hpp"
#include "shared/ability_configuration.hpp"
#include "shared/hazard_archetype.hpp"

#include <vector>

namespace blob_royale::gameplay {

// canonical: game_mode_configuration -- the validated gameplay sections a registry row is handed.
//
// **One member per configured mode, and one per configured mechanic in `shared/`.** `[royale]`
// (`docs/architecture/0005-royale-mode.md` § "Mode configuration"), `[king_of_the_hill]`,
// and `[race]` (`docs/architecture/0007-king-of-the-hill-and-race-modes.md`) are the mode
// sections; `[sandbox]` declares free-play return timing. `hazards` is the
// first member that belongs to no mode at all: hazards are a mode-agnostic mechanic, so any mode
// may declare the systems that read the table and a mode that declares none simply never reads it,
// which is the same relationship `sandbox` already has with `[royale]`. `movement` is the shared
// authored pair used to seed MatchState, not a scalar captured by any mode's steering system.
// All modes read that current match-owned value through the same stateless steering system.
// `abilities` is the third such member and joins `hazards` and `movement` for the same reason: an
// ability is a mode-agnostic mechanic whose timings every mode's shared `ability` system reads, so
// the section answers to no game in particular (`shared/ability_configuration.hpp`).
//
// **`abilities` is deliberately the LAST member.** Two callers build this aggregate by positional
// brace initialization -- `defaults()` below and `src/application/application_config_loader.cpp`,
// which the loader's own worker appends to -- so a member inserted in the middle would silently
// re-pair every initializer after it with the wrong member instead of failing to compile. Appending
// keeps both call sites a one-line addition and keeps the failure mode a compile error.
//
// It exists so that `GameModeRegistry::Factory` has one signature. The alternative -- a factory per
// configured mode, or a registry that returns a name and lets the caller switch -- would put a
// second place that knows which games exist, and the registry is meant to be the only one
// (`game_mode_registry.hpp`).
//
// Adding a configured mode:
//
//   new  src/gameplay/<mode>/<mode>_configuration.{hpp,cpp}  the validated section
//   edit src/gameplay/game_mode_configuration.hpp            one member here
//   edit src/application/application_config_loader.cpp       the `[<mode>]` INI fields
//   edit src/gameplay/game_mode_registry.hpp                 one row
//
// Adding a hazard kind costs none of the four: it is one `[hazard.<kind>]` section in the
// configuration file and no C++ at all, which is what `shared/hazard_archetype.hpp` and the
// loader's section-family concept exist to make true.
// related: game_mode_registry.hpp -- what is handed one of these.
// related: royale/royale_configuration.hpp -- the one configured mode's section.
// related: shared/hazard_archetype.hpp -- one row of the hazard table.
// related: shared/ability_configuration.hpp -- the shared `[abilities]` timings, last member.
struct GameModeConfiguration final {
  RoyaleConfiguration royale;
  KingOfTheHillConfiguration king_of_the_hill;
  RaceConfiguration race;

  // Every declared `[hazard.<kind>]` section, validated, in the order the configuration file
  // declares them. **Empty is the ordinary case**: a configuration that names no hazard section
  // has no hazards, which is exactly what every configuration in the tree looked like before
  // hazards existed and is why adding them broke nothing. The order is the file's so that a
  // seeded spawner's choice among kinds is reproducible from the configuration alone.
  std::vector<HazardArchetype> hazards;

  // Required shared `[movement]` pair. Startup seeds both current and reset defaults from this
  // value; live changes belong exclusively to MatchState::movement and survive round resets.
  simulation::MovementTuning movement;
  SandboxConfiguration sandbox;

  // Required shared `[abilities]` section: the authored ability timings, already converted to tick
  // counts. Every mode hands its own copy to the shared `ability` system it declares; a system may
  // not point back at the mode the engine destroys after reading its declarations.
  AbilityConfiguration abilities;

  // Every configured mode's own declared defaults and no hazards, which is what the no-argument
  // `GameModeRegistry::create(name)` builds. Hazards have no defaults to declare because there is
  // no default kind: a kind exists only because a section declared it.
  [[nodiscard]] static GameModeConfiguration defaults() {
    return GameModeConfiguration{
        RoyaleConfiguration::defaults(),        KingOfTheHillConfiguration::defaults(),
        RaceConfiguration::defaults(),          {},
        simulation::MovementTuning::defaults(), SandboxConfiguration::defaults(),
        AbilityConfiguration::defaults()};
  }

  friend bool operator==(const GameModeConfiguration&, const GameModeConfiguration&) = default;
};

} // namespace blob_royale::gameplay

#endif
