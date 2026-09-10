#ifndef BLOB_ROYALE_CONTROLLERS_CONTROLLER_REGISTRY_HPP
#define BLOB_ROYALE_CONTROLLERS_CONTROLLER_REGISTRY_HPP

#include "chaser_controller.hpp"
#include "controller.hpp"
#include "controller_id.hpp"
#include "hill_seeker_controller.hpp"
#include "racer_controller.hpp"
#include "wanderer_controller.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace blob_royale::controllers {

// canonical: controller_registry -- the closed map from one bot kind name to its factory.
// @extension-point controller
//
// This is the one place a bot kind name becomes a controller. `[match] bots=wanderer:2,chaser:1`
// names rows here, `ControllerDirectory` publishes the same name back as the wire
// `controller_kind`, and no other file in the tree knows that `wanderer` or `chaser` exists
// (`docs/architecture/0004-gameplay-architecture.md` § "Libraries, and where a new thing goes").
//
// **Adding a bot touches exactly one existing file, and this is it:**
//
//   new  src/controllers/<name>_controller.{hpp,cpp}   the behavior class and its Personality
//                                                      struct; a personality is a *value*, so a
//                                                      second mood is a second configuration and
//                                                      never a second class
//   new  tests/unit/controllers/<name>_controller_tests.cpp  mirroring the source
//   edit src/controllers/controller_registry.hpp       one include and one row in
//                                                      kControllerRegistrations
//   edit src/controllers/CMakeLists.txt                the new .cpp
//   edit match configuration                           `[match] bots=`
//   do not touch                                       blob_simulation, blob_runtime,
//                                                      blob_gameplay, blob_server, blob_protocol,
//                                                      ControllerHost, or any other bot
//
// The table is `constexpr`, so an ambiguous registry -- two rows claiming one name -- fails to
// compile rather than resolving to whichever row was written first, exactly as
// `game_mode_registry.hpp` and the kind registries do (`src/simulation/kind_registry.hpp`; engine
// review finding 7).
//
// **A factory takes a `ControllerId` and a seed, and no personality.** Those two are per-instance
// identity and reproducibility values rather than balance: the identity is the one
// `CommandSink::open_session` just issued, and the seed comes from match configuration so a match
// replays. Every registered kind declares a default personality, and a bot whose personality is
// configured is constructed through its own three-argument `create`. That is the same shape
// `game_mode_registry.hpp` uses and for the same reason, and plan Step 25 -- which hands a mode
// factory its validated `[<mode>]` section -- is where a roster line gains per-bot personality
// values, changing this row shape once and changing no controller.
//
// **`scripted_replay` is deliberately absent, and its absence is the rule this registry enforces.**
// A registered kind is one a roster line may name, and `ScriptedReplayController` is meaningless
// without the recorded log no configuration line carries; a row for it would make
// `bots=scripted_replay:1` produce a bot that silently decides nothing, which is the degraded
// success this codebase refuses. Fixtures construct it directly
// (`scripted_replay_controller.hpp`).
//
// Four implementations of this seam, all registered below: `wanderer`, `chaser`, `hill_seeker`,
// and `racer`. ADR 0004's second named implementation of the *controller* seam is an off-thread
// LLM-driven controller, which is a new file plus one row here and nothing else.
// related: wanderer_controller.hpp -- the first registered bot.
// related: chaser_controller.hpp -- the second.
// related: hill_seeker_controller.hpp -- the third, and the first that reads a mode's entity.
// related: controller.hpp -- the role a registered factory produces.
// related: ../gameplay/game_mode_registry.hpp -- the registry this mirrors.
class ControllerRegistry final {
public:
  // A controller factory produces one hosted bot with its default personality, under the durable
  // identity the sink issued it and the seed match configuration chose. It is a plain function
  // pointer rather than a `std::function` so the table is a compile-time constant and a row cannot
  // capture mutable state.
  using Factory = std::unique_ptr<Controller> (*)(simulation::ControllerId, std::uint64_t);

  struct Registration final {
    std::string_view name;
    Factory factory;
  };

  // Every registered kind, in declared order. Published so a diagnostic, a `--help`, or a
  // configuration rejection can list what is playable without a second list.
  [[nodiscard]] static std::span<const Registration> registrations() noexcept;

  // Whether this name resolves. Total: an unknown name is `false`, not a failure.
  [[nodiscard]] static bool contains(std::string_view controller_kind) noexcept;

  // The controller this name resolves to. Throws ControllersValidationError with
  // `CONTROLLERS.CONTROLLER_KIND_UNKNOWN` for a name no row declares, listing the names that do: a
  // silently defaulted bot would put the wrong behavior in the match, and returning nullptr would
  // let the caller ignore the answer.
  [[nodiscard]] static std::unique_ptr<Controller>
  create(std::string_view controller_kind, simulation::ControllerId controller, std::uint64_t seed);

  // The registered names, comma separated, for a rejection detail or a diagnostic.
  [[nodiscard]] static std::string registered_names();

  ControllerRegistry() = delete;
};

// The closed table. One row per bot; the row is the whole registration.
inline constexpr std::array<ControllerRegistry::Registration, 4> kControllerRegistrations{
    ControllerRegistry::Registration{WandererController::kControllerKind,
                                     &WandererController::create},
    ControllerRegistry::Registration{ChaserController::kControllerKind, &ChaserController::create},
    ControllerRegistry::Registration{HillSeekerController::kControllerKind,
                                     &HillSeekerController::create},
    ControllerRegistry::Registration{RacerController::kControllerKind, &RacerController::create},
};

// A controller kind is an identity, so two rows may not claim one. Checked over the whole table
// rather than pairwise, so a third bot costs no new comparison to maintain.
constexpr bool controller_kinds_are_distinct() noexcept {
  for (std::size_t left = 0; left + 1 < kControllerRegistrations.size(); ++left) {
    for (std::size_t right = left + 1; right < kControllerRegistrations.size(); ++right) {
      if (kControllerRegistrations[left].name == kControllerRegistrations[right].name) {
        return false;
      }
    }
  }
  return true;
}

static_assert(controller_kinds_are_distinct(),
              "every registered controller kind must claim its own name");

} // namespace blob_royale::controllers

#endif
