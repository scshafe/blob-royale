#ifndef BLOB_ROYALE_GAMEPLAY_GAME_MODE_CONFIGURATION_HPP
#define BLOB_ROYALE_GAMEPLAY_GAME_MODE_CONFIGURATION_HPP

#include "royale/royale_configuration.hpp"

namespace blob_royale::gameplay {

// canonical: game_mode_configuration -- the validated `[<mode>]` sections a registry row is handed.
//
// **One member per mode that declares a configuration section.** `[royale]` is the only such
// section today (`docs/architecture/0005-royale-mode.md` § "Mode configuration"); `sandbox`
// declares none and its factory reads nothing from this value. A mode reads only its own member,
// which is what keeps a balance change to one game a change to one member.
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
// related: game_mode_registry.hpp -- what is handed one of these.
// related: royale/royale_configuration.hpp -- the one configured mode's section.
struct GameModeConfiguration final {
  RoyaleConfiguration royale;

  // Every configured mode's own declared defaults, which is what the no-argument
  // `GameModeRegistry::create(name)` builds.
  [[nodiscard]] static GameModeConfiguration defaults() {
    return GameModeConfiguration{RoyaleConfiguration::defaults()};
  }

  friend bool operator==(const GameModeConfiguration&, const GameModeConfiguration&) = default;
};

} // namespace blob_royale::gameplay

#endif
