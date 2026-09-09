#ifndef BLOB_ROYALE_APPLICATION_BOT_RECONCILIATION_HPP
#define BLOB_ROYALE_APPLICATION_BOT_RECONCILIATION_HPP

#include "controller_host.hpp"

#include "command_sink.hpp"

#include "controller_id.hpp"
#include "simulation_limits.hpp"
#include "tick_sequence.hpp"
#include "world_snapshot.hpp"

#include "structured_logger.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace blob_royale::application {

// canonical: seat_bot_reconciler -- a bot for every declared NPC seat, and none for a seat that is
// gone.
//
// The tick records that seat 3 wants a `chaser`; it cannot build one, because controllers live in
// `blob_controllers` and are constructed by the application through `ControllerRegistry::create`.
// So the composition root observes committed seat state after each control poll and makes the live
// bots match it, which is the same observe-committed-state pattern `placement_recorder_system`
// uses inside the tick (`docs/architecture/0004-gameplay-architecture.md` § "Determinism
// obligations for framework code"). Nothing here reads a world; it reads a published snapshot and
// acts through the same `CommandSink` a browser acts through.
//
// **Idempotent because the seat carries the controller.** A declared seat with no controller gets
// a bot: a session is opened, the controller is filed with the host, and one `join` naming that
// seat is submitted. Until the tick applies the join the seat still shows no controller, so the
// reconciler remembers the bots whose join is in flight and gives each one a second to land before
// concluding the seat was cleared, resized away, or taken by a person in the meantime -- in which
// case the bot sits nowhere and is retired. A bot whose seat stops holding it for any reason is
// retired the same way: its session is closed, which enqueues its `leave`, and it leaves the host.
//
// **A creation that fails is recorded per seat and not retried every poll.** A full controller
// directory or host would otherwise log the same refusal forty times a second; the seat is
// retried once its declaration changes, which is the only event that could make it succeed.
// related: ../simulation/commands/join_command.hpp -- the command a bot's seat is claimed with.
// related: blob_royale_application.cpp -- the control loop that polls this.
class SeatBotReconciler final {
public:
  // Committed ticks a submitted join is given to be observed in a snapshot before the bot it was
  // for is judged to sit nowhere. One second: a join lands on the next tick and is published at
  // presentation cadence, so the whole budget is headroom for a stalled runtime.
  static constexpr std::uint64_t kJoinObservationBudgetTicks =
      simulation::kSimulationTicksPerSecond;

  SeatBotReconciler(runtime::CommandSink& sink, controllers::ControllerHost& host,
                    std::uint64_t match_seed, observability::StructuredLogger& logger) noexcept;

  SeatBotReconciler(const SeatBotReconciler&) = delete;
  SeatBotReconciler(SeatBotReconciler&&) = delete;
  SeatBotReconciler& operator=(const SeatBotReconciler&) = delete;
  SeatBotReconciler& operator=(SeatBotReconciler&&) = delete;
  ~SeatBotReconciler() = default;

  // One pass over one published snapshot: retire the bots whose seats no longer hold them, then
  // create a bot for every declared seat nobody holds. Never throws; a creation failure is logged
  // and remembered.
  void reconcile(const simulation::WorldSnapshot& snapshot);

  // The bots this reconciler created and has not retired, whether or not their joins have landed.
  [[nodiscard]] std::size_t hosted_bot_count() const noexcept { return bots_.size(); }

private:
  struct HostedBot final {
    std::string kind;
    std::size_t seat_index;
    // Set while the join is in flight; cleared once a snapshot shows the seat holding the bot.
    std::optional<simulation::TickSequence> join_submitted_at;
  };

  void create_bot(std::string_view kind, std::size_t seat_index,
                  simulation::TickSequence observed_tick);
  void retire_bot(simulation::ControllerId controller, const HostedBot& bot,
                  std::string_view reason) noexcept;

  runtime::CommandSink* sink_;
  controllers::ControllerHost* host_;
  std::uint64_t match_seed_;
  observability::StructuredLogger* logger_;
  std::map<simulation::ControllerId, HostedBot> bots_;
  std::map<std::string, std::uint64_t> display_ordinals_;
  std::map<std::size_t, std::string> failed_seat_kinds_;
};

} // namespace blob_royale::application

#endif
