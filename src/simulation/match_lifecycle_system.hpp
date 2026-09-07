#ifndef BLOB_ROYALE_SIMULATION_MATCH_LIFECYCLE_SYSTEM_HPP
#define BLOB_ROYALE_SIMULATION_MATCH_LIFECYCLE_SYSTEM_HPP

#include "match_lifecycle_durations.hpp"
#include "match_objective.hpp"
#include "simulation_system.hpp"

#include <memory>
#include <string_view>

namespace blob_royale::simulation {

// canonical: match_lifecycle_system -- the engine's match machine, run last at kLifecycle.
//
// It is a SimulationSystem like any other, because there is no second kind of tick participant.
// The engine constructs it from the mode's declared MatchObjective and **appends it last at
// kLifecycle, where it is not removable**, so a mode's own lifecycle systems always observe the
// phase the previous tick committed and always run before this tick's transition is evaluated
// (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the match lifecycle").
//
// **It commits at most one phase transition per tick.** That bound is the totality rule of
// `docs/architecture/0003-deterministic-simulation-contract.md` § "Canonical tick": a mode whose
// durations are all zero advances exactly one phase per tick and terminates, instead of chaining
// `lobby -> countdown -> running -> ended -> lobby` inside one tick and never converging.
//
// The five transitions are the complete machine:
//
//   lobby     -> countdown  when the objective can start
//   countdown -> lobby      when it can no longer start
//   countdown -> running    after countdown_ticks
//   running   -> ended      when the objective is decided
//   ended     -> lobby      after restart_delay_ticks
//
// A committed transition writes MatchState -- the phase, its start tick, the running start tick,
// and the committed MatchOutcome -- **and nothing else**. A consequence a mode wants from a
// transition is that mode's own system, ordered ahead of this one by declaration.
//
// The system holds only immutable configuration, as the interface requires: the objective it was
// constructed with and the durations that objective declared once.
// related: match_state.hpp -- the only state this writes.
// related: match_objective.hpp -- the three declarations this asks.
class MatchLifecycleSystem final : public SimulationSystem {
public:
  // The stable name this system is known by in the pipeline, diagnostics, and fixtures. A mode
  // that declares a system of the same name is rejected when the pipeline is built, which is what
  // keeps the engine's own system unshadowable.
  static constexpr std::string_view kSystemName = "match_lifecycle";

  // Reads `durations()` once, here, because the two phase lengths are configuration and not a
  // per-tick question.
  explicit MatchLifecycleSystem(std::unique_ptr<const MatchObjective> objective);

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  void apply(GameWorld& world, const TickContext& context) const override;

private:
  std::unique_ptr<const MatchObjective> objective_;
  MatchLifecycleDurations durations_;
};

} // namespace blob_royale::simulation

#endif
