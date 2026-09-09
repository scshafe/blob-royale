#ifndef BLOB_ROYALE_SIMULATION_COMMANDS_LEAVE_COMMAND_HPP
#define BLOB_ROYALE_SIMULATION_COMMANDS_LEAVE_COMMAND_HPP

#include "controller_id.hpp"

namespace blob_royale::simulation {

// canonical: leave_command -- one controller is gone, and everything it drove goes with it.
//
// Server-issued, never a client's: `CommandSink::close_session` enqueues one for the controller it
// is retiring, so the tick learns a session ended from the one place that knows. It names the
// controller rather than an entity because the controller is the identity that survives: a session
// owns at most one entity but may own none yet (a spawn still in the mailbox), or one with no body
// (deferred by a full spawn ring), and only the tick can say which. Phase 0 destroys every entity
// whose `Controllable` names this controller, pending or seated, and vacates any seat it holds -- a
// person's seat empties, an NPC seat keeps its declaration and loses its bot.
//
// It is the reason a session no longer has to know its own entity to leave cleanly. Despawning by
// entity from the session left a body behind whenever the socket closed between the slot that
// submitted the spawn and the slot that would have observed the result
// (`docs/reviews/2026-09-08-lobby-and-hazard-review.md`, finding 1). A leave is ordered after every
// other kind in phase 0, so a spawn and a leave in one batch net to nothing, and a spawn drained on
// an earlier tick is destroyed by the leave that follows it.
//
// A leave naming a controller that drives nothing and sits nowhere is a no-op, which is what makes
// a close path that runs twice harmless and a replayed log carrying a leave for a never-seated
// controller legal.
// related: command_registry.hpp -- the closed list of command kinds.
// related: ../../runtime/command_sink.hpp -- the one issuer.
struct LeaveCommand final {
  ControllerId controller;

  friend bool operator==(const LeaveCommand&, const LeaveCommand&) = default;
};

} // namespace blob_royale::simulation

#endif
