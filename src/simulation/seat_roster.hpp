#ifndef BLOB_ROYALE_SIMULATION_SEAT_ROSTER_HPP
#define BLOB_ROYALE_SIMULATION_SEAT_ROSTER_HPP

#include "controller_id.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "snake_case_identity.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::simulation {

// canonical: seat_kind_name -- the owned, bounded controller kind one seat declares for an NPC.
//
// A value, not a view, for the same reason `ContactRuleName` is one: the roster lives in
// `MatchState` and outlives whatever named the kind -- a configuration string, a command decoded
// from a frame -- so a borrowed `std::string_view` would be correct only as long as its owner
// outlived the match, and a dangling kind surfaces as garbage in a lobby rather than as a failure.
//
// Fixed capacity rather than `std::string` because **`MatchState` is copied into the working world
// at the start of every tick**. An owning allocation per seat would put an allocator on the tick's
// entry path for a value that is at most `kMaximumKindNameLength` characters, and it would make the
// per-tick cost of a lobby depend on how long a bot kind happens to be named. The unused tail is
// zero-filled so the defaulted comparison compares names rather than whatever the storage held.
//
// It is a near-copy of `ContactRuleName`'s storage boilerplate and deliberately not a shared
// template yet: the two validate against different rules -- a contact rule name is any snake_case
// identity within its own capacity, while a seat kind is published to a client and arrives from
// one, so it must satisfy the wire's `kind_name` grammar and length exactly. The *grammar* is
// already shared through `snake_case_identity.hpp`, which is the part that could drift; only the
// array and the accessors repeat. A third bounded name is the point at which they should become one
// type. related: snake_case_identity.hpp -- the one grammar this validates against. related:
// contact_rule_name.hpp -- the same storage shape under a different validation rule.
class SeatKindName final {
public:
  static constexpr std::size_t kCapacity = kMaximumKindNameLength;

  // Creates a validated kind name or throws SimulationValidationError. The rule is the published
  // `common.schema.json#/$defs/kind_name`, because this name is both accepted from a client and
  // published back to every client: a name this rejects could never be encoded.
  //
  // **Whether the named kind exists is a different question and is not asked here.** The registry
  // of controller kinds lives in `blob_controllers`, which the simulation cannot reach and must
  // not; the command that seats an NPC checks the name against that registry before it ever
  // reaches a tick (plan Step 3).
  [[nodiscard]] static SeatKindName create(const std::string_view value) {
    if (!is_wire_kind_name(value)) {
      throw SimulationValidationError(
          SimulationValidationCode::kSeatKindNameInvalid, "seat.npc_kind",
          "seat controller kind " + std::string(value) +
              " must be a non-empty snake_case identity within the published kind-name length");
    }

    SeatKindName name;
    std::copy(value.cbegin(), value.cend(), name.characters_.begin());
    name.length_ = value.size();
    return name;
  }

  // The unnamed kind, which no validated name can hold. It exists so `NpcSeat` is an aggregate with
  // a defined default and compares unequal to every real kind.
  SeatKindName() = default;
  SeatKindName(const SeatKindName&) = default;
  SeatKindName(SeatKindName&&) noexcept = default;
  SeatKindName& operator=(const SeatKindName&) = default;
  SeatKindName& operator=(SeatKindName&&) noexcept = default;
  ~SeatKindName() = default;

  [[nodiscard]] std::string_view value() const& noexcept {
    return std::string_view(characters_.data(), length_);
  }
  [[nodiscard]] std::string_view value() const&& = delete;

  [[nodiscard]] bool empty() const noexcept { return length_ == 0; }

  [[nodiscard]] friend bool operator==(const SeatKindName& left, const std::string_view right) {
    return left.value() == right;
  }

  friend bool operator==(const SeatKindName&, const SeatKindName&) = default;

private:
  std::array<char, kCapacity> characters_{};
  std::size_t length_{};
};

// canonical: seat -- one place in a pre-match lobby, in exactly one of its three states.
//
// A closed variant rather than a struct of optionals, because the three states are exclusive and a
// struct admits the two combinations that mean nothing -- a seat holding both a controller and a
// declared kind, and a seat holding neither while claiming to be filled. The engine asks a seat
// exactly one question, "are you filled", and that question is total over the variant.
struct EmptySeat final {
  friend bool operator==(const EmptySeat&, const EmptySeat&) = default;
};

// A seat one live controller holds. This is a person's seat today, and it is the arm a bot's
// session lands in once plan Step 6 has the runtime open one, because by then the bot *is* a
// controller and the simulation has no way to tell the two apart -- nor any reason to.
struct ControllerSeat final {
  ControllerId controller;

  friend bool operator==(const ControllerSeat&, const ControllerSeat&) = default;
};

// A seat **declared** for an NPC of a named kind, and the controller the runtime has created for
// it, if it has created one yet.
//
// This is the whole reason the roster is state rather than a derivation: the simulation cannot
// construct a controller -- they live in `blob_controllers` and are built by the application
// through `ControllerRegistry::create` -- so "fill this seat with a wanderer" can only be a
// declaration a tick records and the runtime reconciles against, which is the same
// observe-committed-state pattern `placement_recorder_system` already uses
// (`docs/architecture/0004-gameplay-architecture.md` § "Determinism obligations for framework
// code"). Reconciliation is plan Step 6; recording the declaration is this one.
//
// **`controller` is the reconciliation's own memory, and the seat carries it rather than a side
// table because without it the reconciliation cannot be idempotent.** The runtime's question each
// tick is "does this seat already hold the bot it asks for?", and the two answers it must tell
// apart are *this seat is a declaration nobody has built yet* and *this seat is a declaration whose
// bot is already alive*. A seat holding only a kind answers neither, so a reconciler would create a
// second wanderer every tick and thrash. It is `std::nullopt` for the whole window between the tick
// that declares the seat and the tick that observes the created session, which is at least one
// tick and is a state a client renders as "joining".
//
// **The kind survives the seating**, which is why an occupied NPC seat is still an `NpcSeat` and
// never becomes a `ControllerSeat`: the kind is what a client draws in the seat, what a
// reconciliation compares against, and what a player changed when they picked `chaser` over
// `wanderer`. Replacing the arm on arrival would destroy exactly the value the reconciliation needs
// on the next tick, and would make "this seat wants a chaser" indistinguishable from "a person sat
// down here".
struct NpcSeat final {
  SeatKindName kind;
  std::optional<ControllerId> controller;

  friend bool operator==(const NpcSeat&, const NpcSeat&) = default;
};

using Seat = std::variant<EmptySeat, ControllerSeat, NpcSeat>;

// A seat is filled when somebody is actually in it: a person, or the bot the runtime has created
// for a declared NPC seat. A declaration nobody has built a bot for is *occupied* -- it belongs to
// somebody, and a resize must not drop it -- but it is not filled, so a lobby of declarations
// cannot start a match with nobody in it (`docs/reviews/2026-09-08-lobby-and-hazard-review.md`,
// finding 4). The cost is that Start waits the one control poll the reconciliation takes to build
// the bot, which the wire already renders as the seat's joining state.
[[nodiscard]] constexpr bool seat_is_filled(const Seat& seat) noexcept {
  if (const auto* declared = std::get_if<NpcSeat>(&seat); declared != nullptr) {
    return declared->controller.has_value();
  }
  return std::holds_alternative<ControllerSeat>(seat);
}

// A seat is occupied when it is not empty: held by a person, or declared for a bot whether or not
// the bot exists yet. This is the rule a resize and a seating respect, because a declaration is
// somebody's decision even before the runtime has acted on it.
[[nodiscard]] constexpr bool seat_is_occupied(const Seat& seat) noexcept {
  return !std::holds_alternative<EmptySeat>(seat);
}

// canonical: seat_roster -- the ordered, bounded lobby every match starts from.
//
// **The roster is engine state and lives in `MatchState`, not in a mode's `ModeMatchState` arm.**
// The four-phase machine is engine-owned and generic and a mode supplies predicates rather than
// lifecycle machinery (`docs/architecture/0004-gameplay-architecture.md` § "Game modes and the
// match lifecycle"); the roster is the *input* to the machine's very first transition, so it sits
// where the phase it gates sits. Putting it in royale's arm would make the second competitive mode
// duplicate the roster, its wire schema, and the whole client lobby.
//
// The cost is stated honestly rather than hidden: `MatchState` grows, so world equality and the
// published snapshot grow with it, and a mode that never reads a seat -- `sandbox` -- carries a
// roster it ignores exactly as it ignores every other `[royale]` key. That is acceptable only
// because an empty roster is inert and a filled one is a handful of integers.
//
// **The seat count is itself state**, not configuration read once, because a player in the lobby
// changes it (plan Step 3). Its initial value is a required configuration key --
// `[royale] lobby_seat_count`, since this tree has no silently defaulted keys -- and the caller
// that constructs the initial world seeds the roster from it, exactly as that caller seeds the
// world's entities. A default-constructed roster has **no seats at all**, which is the only honest
// value for a world nobody declared a lobby for, and `is_full()` is false for it: a lobby with no
// seats is not a lobby everyone is sitting in, it is a lobby that was never set up.
//
// `start_requested` lives here rather than beside it because a start request is a statement about
// this roster and nothing else. It is one-shot: `MatchLifecycleSystem` clears it on every committed
// transition **into** `lobby`, so a finished match cannot re-enter `running` without someone
// pressing the button again.
// related: match_state.hpp -- the world state that holds one of these.
// related: match_lifecycle_system.hpp -- the only engine writer, and the one that clears the flag.
class SeatRoster final {
public:
  // A lobby of no seats cannot be sat in, so a *declared* roster has at least one seat. Zero is
  // reachable only as the default below, which is the absence of a declaration rather than a
  // declaration of nothing.
  static constexpr std::size_t kMinimumSeatCount = 1;
  static constexpr std::size_t kMaximumSeatCount = kMaximumLobbySeatCount;

  // No seats: the value every world holds until a caller declares a lobby.
  SeatRoster() = default;

  // `seat_count` empty seats and no pending start. Throws SimulationValidationError outside
  // `[kMinimumSeatCount, kMaximumSeatCount]`; the tighter bound -- a map's spawn-marker count --
  // is applied by the mode's `validate_map` to the configured count at startup and by kernel
  // phase 0 to every `set_seat_count` at run time (`game_simulation.cpp`, apply_lobby_command),
  // because the roster itself never sees a map.
  [[nodiscard]] static SeatRoster of_size(const std::size_t seat_count) {
    if (seat_count < kMinimumSeatCount || seat_count > kMaximumSeatCount) {
      throw SimulationValidationError(
          SimulationValidationCode::kSeatRosterSeatCountOutOfRange, "seat_roster.seat_count",
          "a declared lobby of " + std::to_string(seat_count) + " seats is outside [" +
              std::to_string(kMinimumSeatCount) + ", " + std::to_string(kMaximumSeatCount) + "]");
    }
    SeatRoster roster;
    roster.seats_.assign(seat_count, Seat{EmptySeat{}});
    return roster;
  }

  SeatRoster(const SeatRoster&) = default;
  SeatRoster(SeatRoster&&) noexcept = default;
  SeatRoster& operator=(const SeatRoster&) = default;
  SeatRoster& operator=(SeatRoster&&) noexcept = default;
  ~SeatRoster() = default;

  [[nodiscard]] std::size_t seat_count() const noexcept { return seats_.size(); }

  // The seats in index order, which is the order a client renders and the order every tie-break
  // over seats resolves in.
  [[nodiscard]] std::span<const Seat> seats() const& noexcept { return seats_; }
  [[nodiscard]] std::span<const Seat> seats() const&& = delete;

  // At least one seat, and every seat filled. The "at least one" is not pedantry: without it the
  // empty roster every non-lobby world holds would be vacuously full and any mode whose `can_start`
  // asked this would start a match nobody had joined.
  [[nodiscard]] bool is_full() const noexcept {
    return !seats_.empty() && std::all_of(seats_.cbegin(), seats_.cend(), seat_is_filled);
  }

  [[nodiscard]] bool start_requested() const noexcept { return start_requested_; }

  // Replaces one seat. Throws SimulationValidationError for an index this roster does not have,
  // because an out-of-range seat is a defect in whatever produced it rather than a rejected input:
  // a command carrying a seat index is range-checked at the boundary before it reaches a tick.
  void assign_seat(const std::size_t index, Seat seat) {
    if (index >= seats_.size()) {
      throw SimulationValidationError(
          SimulationValidationCode::kSeatRosterSeatIndexOutOfRange, "seat_roster.seat_index",
          "seat index " + std::to_string(index) + " names no seat in a roster of " +
              std::to_string(seats_.size()));
    }
    seats_[index] = std::move(seat);
  }

  // Resizes the lobby, and reports whether it did.
  //
  // **The stated rule for shrinking is that a lobby never shrinks past somebody who is sitting
  // down.** A shrink that would remove an occupied seat -- held by a person, or declared for a bot
  // whether or not it exists yet -- changes nothing and returns false; every other resize happens
  // and returns true. Growing appends empty seats at the high indices, so no
  // existing seat changes index and nobody's seat moves under them.
  //
  // The rule was chosen over the two alternatives for reasons worth recording, because **anyone in
  // the lobby may send this command**. *Truncating* -- dropping the tail whoever is in it -- makes
  // one player's slider the most destructive control in the room, and there is no undo for a bot
  // that was configured or a person who was seated. *Compacting* -- sliding occupants down into the
  // surviving indices -- is worse still: a seat index is how a client names a seat and how a person
  // recognizes their own, so compacting moves people without telling them and renames every seat
  // below the gap. Refusing is the only option a client can render before the fact: the seat-count
  // control's floor is one above the highest filled seat, so the rule is visible rather than
  // discovered by losing a seat.
  //
  // Refusal is a **no-op, not a rejection**, for the same reason a despawn naming an entity that
  // does not exist is ignored: the caller is a network session whose view of the roster lags the
  // world by a frame, and a hard failure would let one client stop the match
  // (`docs/architecture/0003-deterministic-simulation-contract.md` § "Accepted simulation input").
  //
  // Throws SimulationValidationError for a count outside `[kMinimumSeatCount, kMaximumSeatCount]`,
  // because that is a defect in whatever produced it rather than a lagging view: a command carrying
  // a seat count is range-checked at the boundary and again by `InputBatch::create` before it can
  // reach a tick. The map's spawn-marker ceiling is the caller's to apply before calling this, for
  // the reason given at `of_size`.
  [[nodiscard]] bool try_set_seat_count(const std::size_t seat_count) {
    if (seat_count < kMinimumSeatCount || seat_count > kMaximumSeatCount) {
      throw SimulationValidationError(
          SimulationValidationCode::kSeatRosterSeatCountOutOfRange, "seat_roster.seat_count",
          "a lobby of " + std::to_string(seat_count) + " seats is outside [" +
              std::to_string(kMinimumSeatCount) + ", " + std::to_string(kMaximumSeatCount) + "]");
    }
    if (seat_count < seats_.size() &&
        std::any_of(seats_.cbegin() + static_cast<std::ptrdiff_t>(seat_count), seats_.cend(),
                    seat_is_occupied)) {
      return false;
    }
    seats_.resize(seat_count, Seat{EmptySeat{}});
    return true;
  }

  // Idempotent in both directions: pressing Start twice is one request, and clearing a roster that
  // holds none is a no-op. The command that presses the button is `StartMatchCommand`, applied at
  // phase 0; `MatchLifecycleSystem` is the only caller of the clearing half.
  void request_start() noexcept { start_requested_ = true; }
  void clear_start_request() noexcept { start_requested_ = false; }

  friend bool operator==(const SeatRoster&, const SeatRoster&) = default;

private:
  std::vector<Seat> seats_;
  bool start_requested_{false};
};

// `MatchState` holds one of these and is copied into the working world at the start of every tick,
// so a throwing move here would be a throwing move of the whole match state.
static_assert(std::is_nothrow_move_constructible_v<SeatRoster>,
              "SeatRoster is moved with MatchState and must not throw doing it");

} // namespace blob_royale::simulation

#endif
