#ifndef BLOB_ROYALE_CONTROLLERS_CONTROLLER_REGISTRY_HPP
#define BLOB_ROYALE_CONTROLLERS_CONTROLLER_REGISTRY_HPP

#include "chaser_controller.hpp"
#include "controller.hpp"
#include "controller_id.hpp"
#include "creation_context.hpp"
#include "hill_seeker_controller.hpp"
#include "racer_controller.hpp"
#include "tactical_controller.hpp"
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
// This is the one place a bot kind name becomes a controller. Plain roster terms such as
// `wanderer:2` and profiled terms such as `tactical@steady:1` resolve the same rows here.
// `ControllerDirectory` publishes the kind as `controller_kind`; profile identity is carried
// separately by the NPC declaration. Application admission reads each row's requires_profile
// metadata instead of maintaining another list of plain or profiled kinds
// (`docs/architecture/0004-gameplay-architecture.md` § "Libraries, and where a new thing goes").
//
// **A new algorithm extends this registry; another tactical profile is configuration alone:**
//
//   new  src/controllers/<name>_controller.{hpp,cpp}   the behavior class and validated settings;
//                                                      a second personality is a value, not a class
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
// **The single factory path accepts ControllerId, the legacy room seed, and CreationContext.**
// The four diagnostic rows require empty context and retain their existing default personalities,
// seed handling, arithmetic, and repeated-observation behavior. Tactical requires both a borrowed
// validated TacticalProfile and TacticalSeedIdentity{raw match seed, lobby id, authored seat
// index}. Its factory copies both values and never retains the profile pointer. The default empty
// context preserves a plain call; it never asks the factory to invent a missing tactical profile.
//
// `[bot_profile.<name>]` authors four active settings for that one algorithm: seek probability,
// reaction delay, bounded aim error, and target persistence. Name resolution belongs to the
// application's immutable catalogue, not this closed algorithm registry. Tactical emits unit go
// or explicit coast, with seeds derived from authored identity/name and public running-start tick;
// its duplicate/older-tick rejection is not a change to any diagnostic row's lifetime.
//
// **`scripted_replay` is deliberately absent, and its absence is the rule this registry enforces.**
// A registered kind is one a roster line may name, and `ScriptedReplayController` is meaningless
// without the recorded log no configuration line carries; a row for it would make
// `bots=scripted_replay:1` produce a bot that silently decides nothing, which is the degraded
// success this codebase refuses. Fixtures construct it directly
// (`scripted_replay_controller.hpp`).
//
// Five registered implementations: plain `wanderer`, `chaser`, `hill_seeker`, and `racer`, followed
// by profiled `tactical`. Another profile adds no row. ADR 0004's off-thread LLM-driven controller
// remains another possible algorithm behind the same command-source role.
// related: wanderer_controller.hpp -- the first registered bot.
// related: chaser_controller.hpp -- the second.
// related: hill_seeker_controller.hpp -- the third, and the first that reads a mode's entity.
// related: tactical_controller.hpp -- one objective policy for every authored tactical profile.
// related: tactical_profile_catalogue.hpp -- ordered profile-name resolution, owned by controllers.
// related: creation_context.hpp -- the borrowed factory input and its required identity.
// related: controller.hpp -- the role a registered factory produces.
// related: ../gameplay/game_mode_registry.hpp -- the registry this mirrors.
class ControllerRegistry final {
public:
  // Produces one hosted bot under the sink-issued identity. Plain rows use their defaults and the
  // legacy room seed; profiled rows copy the context's validated profile and authored identity.
  // A function pointer keeps the table compile-time constant and cannot capture mutable state.
  using Factory = std::unique_ptr<Controller> (*)(simulation::ControllerId, std::uint64_t,
                                                  CreationContext);

  struct Registration final {
    std::string_view name;
    Factory factory;
    // Admission metadata, not a second selectable-name catalogue. True requires both context
    // fields.
    bool requires_profile;
  };

  // Every algorithm row, in declared order. Application composition projects plain rows and actual
  // configured profile choices into one NpcCatalogue; profiled rows are not bare selectable kinds.
  [[nodiscard]] static std::span<const Registration> registrations() noexcept;

  // Whether this name resolves. Total: an unknown name is `false`, not a failure.
  [[nodiscard]] static bool contains(std::string_view controller_kind) noexcept;
  // Stable pointer into the closed table; nullptr denotes an unknown kind. Admission reads the
  // same row's requires_profile policy as create, so names and required context cannot drift.
  [[nodiscard]] static const Registration* find(std::string_view controller_kind) noexcept;

  // The controller this name resolves to, never a default or nullptr. Throws
  // ControllersValidationError with CONTROLLERS.CONTROLLER_KIND_UNKNOWN for an unknown row, or
  // CONTROLLERS.CONTROLLER_CREATION_CONTEXT_INVALID when a plain row receives either context field
  // or a profiled row lacks either one. Profiled factories additionally validate authored identity.
  [[nodiscard]] static std::unique_ptr<Controller> create(std::string_view controller_kind,
                                                          simulation::ControllerId controller,
                                                          std::uint64_t seed,
                                                          CreationContext context = {});

  // The registered names, comma separated, for a rejection detail or a diagnostic.
  [[nodiscard]] static std::string registered_names();

  ControllerRegistry() = delete;
};

// The closed table. One row per bot; the row is the whole registration.
inline constexpr std::array<ControllerRegistry::Registration, 5> kControllerRegistrations{
    ControllerRegistry::Registration{
        WandererController::kControllerKind,
        +[](simulation::ControllerId id, std::uint64_t seed, CreationContext) {
          return WandererController::create(id, seed);
        },
        false},
    ControllerRegistry::Registration{
        ChaserController::kControllerKind,
        +[](simulation::ControllerId id, std::uint64_t seed, CreationContext) {
          return ChaserController::create(id, seed);
        },
        false},
    ControllerRegistry::Registration{
        HillSeekerController::kControllerKind,
        +[](simulation::ControllerId id, std::uint64_t seed, CreationContext) {
          return HillSeekerController::create(id, seed);
        },
        false},
    ControllerRegistry::Registration{
        RacerController::kControllerKind,
        +[](simulation::ControllerId id, std::uint64_t seed, CreationContext) {
          return RacerController::create(id, seed);
        },
        false},
    ControllerRegistry::Registration{
        TacticalController::kControllerKind,
        +[](simulation::ControllerId id, std::uint64_t, CreationContext context) {
          return TacticalController::create(id, *context.profile, *context.tactical_identity);
        },
        true},
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
