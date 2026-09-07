#ifndef BLOB_ROYALE_CONTROLLERS_CONTROLLERS_LIMITS_HPP
#define BLOB_ROYALE_CONTROLLERS_CONTROLLERS_LIMITS_HPP

#include "runtime_limits.hpp"
#include "simulation_limits.hpp"

#include <cstddef>
#include <cstdint>

namespace blob_royale::controllers {

// canonical: controllers_limits -- every bound this library's values enforce.
//
// The same discipline `runtime_limits.hpp` applies to a network boundary, applied to a composition
// boundary: a bot roster and a recorded command log are authored inputs, so every unbounded thing
// one of them can ask for is bounded here rather than in the value that consumes it, and each bound
// is tied by a static assertion to the limit below it that it must not exceed.
// related: runtime_limits.hpp -- the runtime's own ceilings, which these defer to.
// related: controller_host.hpp -- the host that enforces the roster bound.

// Controllers one host may hold at once. A hosted bot opens a session in the `ControllerDirectory`
// exactly as a network player does, so the honest ceiling is the directory's: a roster larger than
// that could not all obtain a `ControllerId` in the first place.
inline constexpr std::size_t kMaximumHostedControllerCount =
    runtime::kMaximumControllerDirectoryEntryCount;

static_assert(kMaximumHostedControllerCount <= runtime::kMaximumControllerDirectoryEntryCount,
              "a hosted controller holds a ControllerDirectory entry, so a host cannot hold more "
              "controllers than the directory holds entries");

// One `ScriptedReplayController`'s recorded log, in decision passes. A fixture authors this list
// literally, so the bound exists to make a runaway generator a named rejection at construction
// rather than an allocation failure at some later pass.
inline constexpr std::size_t kMaximumScriptedReplayStepCount = 65'536;

// How many decision passes one `WandererController` heading may be held. A reaction delay is a
// presentation-frame count, so the bound is generous rather than tight; what it rules out is a
// roster value that would make a bot decide once and then never again for the life of the process.
inline constexpr std::uint32_t kMaximumWandererReactionDelayFrames = 1'000;

// Committed ticks a controller waits before repeating a spawn request that has gone unanswered.
//
// **It is neither one-shot nor every-pass, and both extremes are wrong.** Asking once and never
// again would strand a bot silently and permanently whenever its spawn was refused -- a full
// mailbox or a closed session -- which is the degraded success this codebase refuses. Asking every
// pass creates a *second body for one controller*: a request is in flight for at least one tick, so
// a bot deciding faster than the world publishes would ask again while the engine was already
// seating it. A hundred milliseconds at the fixed 400 Hz tick rate is long enough that a request
// has been drained, applied, and published many times over, and short enough that a genuinely
// dropped spawn is recovered before a player would notice.
//
// **This narrows a gap it cannot close.** Nothing in the engine refuses a spawn from a controller
// that already drives a body -- `SpawnCommand` addresses a `ControllerId`, `InputBatch` keeps at
// most one spawn per controller *per tick*, and phase 0 creates an entity for each -- so a network
// client that sent two spawns in two ticks would get two blobs exactly as a bot would. The rule
// that closes it belongs in kernel phase 0 or in a mode's `SpawnPolicy`, not here.
inline constexpr std::uint64_t kSpawnRequestRetryTicks = simulation::kSimulationTicksPerSecond / 10;

static_assert(kSpawnRequestRetryTicks >= 2,
              "a spawn request is in flight for at least one tick, so a retry interval below two "
              "committed ticks would ask again while the engine was already seating the body");

// The inclusive range of a `ChaserController` aggression weight. It scales a unit direction, and a
// thrust direction component is a unit-interval intent, so a weight above one would produce a
// command the `CommandSink` refuses (`simulation_limits.hpp`
// `kMaximumThrustDirectionComponentMagnitude`).
inline constexpr double kMinimumChaserAggressionWeight = 0.0;
inline constexpr double kMaximumChaserAggressionWeight = 1.0;

} // namespace blob_royale::controllers

#endif
