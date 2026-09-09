#include "bot_reconciliation.hpp"

#include "controller_registry.hpp"

#include "command_registry.hpp"
#include "commands/join_command.hpp"
#include "seat_roster.hpp"

#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace blob_royale::application {
namespace {

// Whether this seat is the NPC seat this bot holds.
[[nodiscard]] bool seat_holds_bot(const simulation::Seat& seat,
                                  const simulation::ControllerId controller) noexcept {
  const auto* declared = std::get_if<simulation::NpcSeat>(&seat);
  return declared != nullptr && declared->controller == controller;
}

// Whether this seat declares an NPC kind and holds no bot yet.
[[nodiscard]] const simulation::NpcSeat*
unfilled_declaration(const simulation::Seat& seat) noexcept {
  const auto* declared = std::get_if<simulation::NpcSeat>(&seat);
  return declared != nullptr && !declared->controller.has_value() ? declared : nullptr;
}

} // namespace

SeatBotReconciler::SeatBotReconciler(runtime::CommandSink& sink, controllers::ControllerHost& host,
                                     const std::uint64_t match_seed, const std::uint64_t lobby_id,
                                     observability::StructuredLogger& logger) noexcept
    : sink_(&sink), host_(&host), match_seed_(match_seed), lobby_id_(lobby_id), logger_(&logger) {}

void SeatBotReconciler::reconcile(const simulation::WorldSnapshot& snapshot,
                                  const bool room_abandoned) {
  const simulation::SeatRoster& seats = snapshot.match().seats();
  const simulation::TickSequence observed = snapshot.tick_sequence();

  if (room_abandoned) {
    for (auto entry = bots_.begin(); entry != bots_.end();) {
      retire_bot(entry->first, entry->second, "room_abandoned");
      entry = bots_.erase(entry);
    }
    return;
  }

  // 1. Retire every bot whose seat no longer holds it, once its join has had its budget to land.
  for (auto entry = bots_.begin(); entry != bots_.end();) {
    const simulation::ControllerId controller = entry->first;
    HostedBot& bot = entry->second;
    const bool seated = bot.seat_index < seats.seat_count() &&
                        seat_holds_bot(seats.seats()[bot.seat_index], controller);
    if (seated) {
      bot.join_submitted_at.reset();
      ++entry;
      continue;
    }
    const bool join_in_flight =
        bot.join_submitted_at.has_value() &&
        observed.value() < bot.join_submitted_at->value() + kJoinObservationBudgetTicks;
    if (join_in_flight) {
      ++entry;
      continue;
    }
    retire_bot(controller, bot, bot.join_submitted_at.has_value() ? "join_refused" : "seat_lost");
    entry = bots_.erase(entry);
  }

  // 2. A bot for every declared seat nobody holds and no in-flight join is about to fill.
  for (std::size_t index = 0; index < seats.seat_count(); ++index) {
    const simulation::NpcSeat* declared = unfilled_declaration(seats.seats()[index]);
    if (declared == nullptr) {
      failed_seat_kinds_.erase(index);
      continue;
    }
    const std::string_view kind = declared->kind.value();
    if (const auto failed = failed_seat_kinds_.find(index);
        failed != failed_seat_kinds_.end() && failed->second == kind) {
      continue;
    }
    bool join_in_flight = false;
    for (const auto& [controller, bot] : bots_) {
      if (bot.seat_index == index && bot.join_submitted_at.has_value()) {
        join_in_flight = true;
        break;
      }
    }
    if (join_in_flight) {
      continue;
    }
    create_bot(kind, index, observed);
  }
}

void SeatBotReconciler::create_bot(const std::string_view kind, const std::size_t seat_index,
                                   const simulation::TickSequence observed_tick) {
  // Named from the roster rather than by anyone, exactly as the startup roster's bots were: the
  // kind and a per-kind ordinal, so "wanderer 2" is the second wanderer this process ever built.
  const std::uint64_t ordinal = ++display_ordinals_[std::string(kind)];
  const std::string display_name = std::string(kind) + " " + std::to_string(ordinal);
  try {
    const simulation::ControllerId controller = sink_->open_session(kind, display_name);
    try {
      host_->add(controllers::ControllerRegistry::create(kind, controller, match_seed_));
    } catch (...) {
      // The registry has no such kind, or the host is full: the session just opened would
      // otherwise stay in the directory with nobody behind it.
      static_cast<void>(sink_->close_session(controller));
      throw;
    }
    static_cast<void>(sink_->submit(
        controller, simulation::Command{simulation::JoinCommand{controller, seat_index}}));
    bots_.emplace(controller, HostedBot{std::string(kind), seat_index, observed_tick});
    logger_->write({.severity = observability::LogSeverity::kInfo,
                    .event = "controllers.bot_created",
                    .lobby_id = lobby_id_,
                    .detail = "controller_id=" + std::to_string(controller.value()) + " kind=" +
                              std::string(kind) + " seat_index=" + std::to_string(seat_index)});
  } catch (const std::exception& failure) {
    // A full directory, a full host, or a registry that cannot build the kind. The seat stays
    // declared and unfilled, which is visible to every client, and this seat is not retried until
    // its declaration changes.
    failed_seat_kinds_[seat_index] = std::string(kind);
    logger_->write({.severity = observability::LogSeverity::kError,
                    .event = "controllers.bot_creation_failed",
                    .lobby_id = lobby_id_,
                    .error_code = "CONTROLLERS.BOT_CREATION_FAILED",
                    .detail = "kind=" + std::string(kind) + " seat_index=" +
                              std::to_string(seat_index) + " reason=" + failure.what()});
  }
}

void SeatBotReconciler::retire_bot(const simulation::ControllerId controller, const HostedBot& bot,
                                   const std::string_view reason) noexcept {
  try {
    // Closing the session enqueues the bot's leave, which destroys its body and, if its seat still
    // held it, returns that seat to a bare declaration (`commands/leave_command.hpp`).
    static_cast<void>(sink_->close_session(controller));
    static_cast<void>(host_->remove(controller));
    logger_->write({.severity = observability::LogSeverity::kInfo,
                    .event = "controllers.bot_retired",
                    .lobby_id = lobby_id_,
                    .detail = "controller_id=" + std::to_string(controller.value()) + " kind=" +
                              bot.kind + " seat_index=" + std::to_string(bot.seat_index) +
                              " reason=" + std::string(reason)});
  } catch (...) {
    // Neither operation throws by contract; a bookkeeping path may not propagate regardless.
  }
}

} // namespace blob_royale::application
