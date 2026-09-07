#ifndef BLOB_ROYALE_GAMEPLAY_GAME_MODE_REGISTRY_HPP
#define BLOB_ROYALE_GAMEPLAY_GAME_MODE_REGISTRY_HPP

#include "game_mode.hpp"
#include "sandbox/sandbox_mode.hpp"

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: game_mode_registry -- the closed map from one mode name to its factory.
// @extension-point game_mode
//
// This is the one place a mode name becomes a mode. `[match] mode=` names a row here, the snapshot
// publishes the same name back through `GameSimulation::snapshot().match().mode_name()`, and no
// other file in the tree knows that `sandbox` or `royale` exists
// (`docs/architecture/0004-gameplay-architecture.md` § "Libraries, and where a new thing goes").
//
// **Adding a game touches exactly one existing file, and this is it:**
//
//   new  src/gameplay/<mode>/<mode>_mode.{hpp,cpp}  the mode class, its systems, rules, policies
//   new  src/gameplay/<mode>/*_system.{hpp,cpp}     any mechanic only that mode declares; one more
//                                                   than one mode declares goes in
//                                                   src/gameplay/shared/
//   edit src/gameplay/game_mode_registry.hpp        one include and one row in
//                                                   kGameModeRegistrations
//   edit match configuration                        `[match] mode=`
//   do not touch                                    blob_simulation, blob_runtime, blob_server,
//                                                   blob_protocol, or any other mode
//
// The table is `constexpr`, so an ambiguous registry -- two rows claiming one name -- fails to
// compile rather than resolving to whichever row was written first, the same way a kind registry's
// duplicate enumerator does (`kind_registry.hpp`; engine review finding 7).
//
// A factory takes no argument today because the only balance number any registered mode owns has a
// declared default. Plan Step 25 adds the validated `[<mode>]` configuration section a factory is
// handed; that is a change to this row shape, made once here, and not a change to any mode.
//
// Two implementations of this seam: `sandbox` below, and `royale` in plan Step 21.
// related: sandbox/sandbox_mode.hpp -- the registered mode.
// related: game_mode.hpp -- the seven declarations a registered factory produces.
class GameModeRegistry final {
public:
  // A mode factory produces one fully declared mode with its default configuration. It is a plain
  // function pointer rather than a `std::function` so the table is a compile-time constant and a
  // row cannot capture mutable state.
  using Factory = std::unique_ptr<const simulation::GameMode> (*)();

  struct Registration final {
    std::string_view name;
    Factory factory;

    friend bool operator==(const Registration&, const Registration&) = default;
  };

  // Every registered mode, in declared order. Published so a diagnostic, a `--help`, or a
  // configuration rejection can list what is playable without a second list.
  [[nodiscard]] static std::span<const Registration> registrations() noexcept;

  // Whether this name resolves. Total: an unknown name is `false`, not a failure.
  [[nodiscard]] static bool contains(std::string_view mode_name) noexcept;

  // The mode this name resolves to. Throws GameplayValidationError with
  // `GAMEPLAY.GAME_MODE_NAME_UNKNOWN` for a name no row declares, listing the names that do: a
  // silently defaulted mode would start the wrong game, and returning nullptr would let the caller
  // ignore the answer.
  [[nodiscard]] static std::unique_ptr<const simulation::GameMode>
  create(std::string_view mode_name);

  // The registered names, comma separated, for a rejection detail or a diagnostic.
  [[nodiscard]] static std::string registered_names();

  GameModeRegistry() = delete;
};

// The closed table. One row per game; the row is the whole registration.
inline constexpr std::array<GameModeRegistry::Registration, 1> kGameModeRegistrations{
    GameModeRegistry::Registration{SandboxMode::kModeName, &SandboxMode::create},
};

// A mode name is an identity, so two rows may not claim one. Checked over the whole table rather
// than pairwise, so a third game costs no new comparison to maintain.
constexpr bool game_mode_names_are_distinct() noexcept {
  for (std::size_t left = 0; left + 1 < kGameModeRegistrations.size(); ++left) {
    for (std::size_t right = left + 1; right < kGameModeRegistrations.size(); ++right) {
      if (kGameModeRegistrations[left].name == kGameModeRegistrations[right].name) {
        return false;
      }
    }
  }
  return true;
}

static_assert(game_mode_names_are_distinct(), "every registered game mode must claim its own name");

} // namespace blob_royale::gameplay

#endif
