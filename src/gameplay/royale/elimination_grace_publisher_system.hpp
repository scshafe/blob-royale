#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_ELIMINATION_GRACE_PUBLISHER_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_ELIMINATION_GRACE_PUBLISHER_SYSTEM_HPP

#include "simulation_system.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: elimination_grace_publisher -- puts `G` on the wire so a client can count down.
// @extension-point simulation_system
//
// It writes one member of royale's mode-state block, `elimination_grace_ticks`, and does nothing
// else. It reads no world state, emits no event, creates and destroys nothing, and its output is
// the same number on every tick of a process.
//
// **Why a system exists for a constant.** A snapshot encoder can see only the world
// (`src/protocol/mode_state_wire_encoding.hpp`), and `elimination_grace_seconds` is a `[royale]`
// configuration key the mode holds, so the only way a mode's configuration reaches a client is for
// one of the mode's own systems to stamp it into committed state. The value is needed because the
// wire already publishes `ZoneExposure::outside_ticks` and a counter without its bound is
// uninterpretable: the client that shipped before this could only count exposure *up*, which
// answers a different question than "how long do I have".
//
// **Why not `placement_recorder`.** That system already rewrites the whole block at this stage and
// could carry one more assignment for free. It is named for what it does -- it ranks and removes
// the eliminated -- and a placement recorder that also publishes a balance constant is a name that
// stops describing its contents. Two systems here cost one pipeline entry and buy a list of stage
// members that still reads as an inventory of the mode's rules.
//
// **Why it is declared last.** It runs after `placement_recorder`, which means it is the final
// writer of the block within a tick and the published value cannot depend on whether some other
// writer happens to preserve members it does not know about. Ordering it first would work today
// only because `placement_recorder` copies the block out and assigns it back; that is an
// implementation detail of another system, and depending on it would make this one's correctness
// somebody else's to maintain. Nothing reads the member inside a tick, so running last costs
// nothing.
//
// The consequence worth stating plainly: **no committed royale tick ever publishes an unstamped
// grace.** `placement_recorder` assigns royale's arm on every tick including the first, and this
// runs after it in the same `kLifecycle` stage, so the first snapshot a session can receive --
// which is the first *completed* tick, because `SnapshotPublication` is not ready before one
// (`src/runtime/snapshot_publication.hpp`) -- already carries the configured value. There is no
// tick on which a client sees a zero that later flips.
// related: royale/zone_elimination_system.hpp -- the rule that enforces the number this publishes.
// related: royale/royale_mode_state.hpp -- the one answer to "what if another arm is held".
// related: mode_states/royale_placements_mode_state.hpp -- the block this writes one member of.
class EliminationGracePublisherSystem final : public simulation::SimulationSystem {
public:
  // The stable name the pipeline, diagnostics, and fixtures know this system by.
  static constexpr std::string_view kSystemName = "elimination_grace_publisher";

  // Takes the tick count rather than the whole `RoyaleConfiguration`, the way `thrust_steering`
  // takes its one scale: a system's members should be what it needs. That the number it publishes
  // is the number `zone_elimination` enforces is guaranteed at the one call site that builds both,
  // `RoyaleMode::systems()`, and pinned by a test that ticks a configured mode and reads the
  // published block back.
  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(std::uint64_t elimination_grace_ticks);

  // Public because the pipeline holds `std::unique_ptr<const SimulationSystem>` and
  // `std::make_unique` needs an accessible constructor.
  explicit EliminationGracePublisherSystem(std::uint64_t elimination_grace_ticks) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  // Never throws and never fails: there is no world state it could find inconsistent.
  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  std::uint64_t elimination_grace_ticks_;
};

} // namespace blob_royale::gameplay

#endif
