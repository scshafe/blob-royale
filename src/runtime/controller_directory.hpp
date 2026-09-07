#ifndef BLOB_ROYALE_RUNTIME_CONTROLLER_DIRECTORY_HPP
#define BLOB_ROYALE_RUNTIME_CONTROLLER_DIRECTORY_HPP

#include "controller_id.hpp"
#include "controller_presentation.hpp"
#include "runtime_limits.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace blob_royale::runtime {

// The total answer to one `ControllerDirectory::register_controller`.
enum class ControllerRegistrationResult : std::uint8_t {
  kRegistered = 0,
  // The id already names an entry. The existing entry is left exactly as it was.
  kRejectedDuplicateControllerId = 1,
  // The directory already holds kMaximumControllerDirectoryEntryCount open controllers.
  kRejectedDirectoryFull = 2,
  // The kind is empty, longer than kMaximumControllerKindLength, or carries a control character.
  kRejectedControllerKind = 3,
  // The display name is longer than kMaximumDisplayNameLength or carries a control character.
  kRejectedDisplayName = 4,
};

[[nodiscard]] constexpr std::string_view
controller_registration_result_name(const ControllerRegistrationResult result) noexcept {
  switch (result) {
  case ControllerRegistrationResult::kRegistered:
    return "registered";
  case ControllerRegistrationResult::kRejectedDuplicateControllerId:
    return "rejected_duplicate_controller_id";
  case ControllerRegistrationResult::kRejectedDirectoryFull:
    return "rejected_directory_full";
  case ControllerRegistrationResult::kRejectedControllerKind:
    return "rejected_controller_kind";
  case ControllerRegistrationResult::kRejectedDisplayName:
    return "rejected_display_name";
  }
  return "controller_registration_result_invalid";
}

// The total answer to one `ControllerDirectory::close`.
enum class ControllerCloseResult : std::uint8_t {
  kClosed = 0,
  // The id names no entry: either it was never registered, or it was already closed. Closing is
  // idempotent by contract, so this is a reportable outcome and never an error.
  kUnknownControllerId = 1,
};

[[nodiscard]] constexpr std::string_view
controller_close_result_name(const ControllerCloseResult result) noexcept {
  switch (result) {
  case ControllerCloseResult::kClosed:
    return "closed";
  case ControllerCloseResult::kUnknownControllerId:
    return "unknown_controller_id";
  }
  return "controller_close_result_invalid";
}

// canonical: controller_directory -- the runtime-owned map from ControllerId to what a client sees.
//
// **Why it exists.** Protocol v2 publishes a controller kind and a display name inside the wire
// `controllable` object, and both are joined here at the encoding boundary rather than stored in
// the `Controllable` component (`docs/architecture/0004-gameplay-architecture.md`, amendment of
// 2026-09-06). That keeps proxy-supplied strings out of the deterministic core: a tick can never
// read one, so a match cannot become a function of a header a proxy wrote.
//
// **Thread-safety contract.** Every operation is safe to call concurrently from any thread. A
// `std::shared_mutex` serializes writers and lets readers share, which is the shape of the actual
// access pattern: the network thread writes once per connection open and once per close, while an
// encoding pass performs one read per published entity. `find` returns a **copy**, so the returned
// value stays valid and unchanged after the entry is closed or the directory is mutated -- an
// encoder can never hold a reference into an entry a disconnect is erasing. The simulation worker
// never touches this object at all: it is presentation state and is deliberately outside the
// deterministic core, so no lock the network thread holds can ever delay a tick.
//
// **Duplicate registration is rejected and the first registration wins.** A controller's
// presentation identity is established exactly once, when its session opens. Overwriting on a
// second registration would let a later frame rename a player, and silently accepting it would
// hide the boundary defect that produced two registrations for one monotonic id;
// `kRejectedDuplicateControllerId` is returned instead and the stored entry is untouched.
//
// **An unknown id is a defined absent answer, not a failure.** `find` returns `std::nullopt` for an
// id that was never registered and for one whose session has closed. Both are ordinary: a snapshot
// retained by a reader can outlive the session of a controller it names, so an encoder must be able
// to publish an entity whose controller is already gone. Nothing throws here.
//
// **An entry lives exactly as long as its session.** `close` erases, so the bound is on
// *concurrently open* controllers rather than on lifetime churn: a server that accepts and drops
// connections forever never exhausts the directory.
// related: command_sink.hpp -- the only production writer, through `open_session`/`close_session`.
// related: controller_presentation.hpp -- the value one entry holds.
class ControllerDirectory final {
public:
  ControllerDirectory() = default;

  ControllerDirectory(const ControllerDirectory&) = delete;
  ControllerDirectory(ControllerDirectory&&) = delete;
  ControllerDirectory& operator=(const ControllerDirectory&) = delete;
  ControllerDirectory& operator=(ControllerDirectory&&) = delete;
  ~ControllerDirectory() = default;

  // Registers one controller's presentation values. Validates and bounds both strings before
  // storing them, so an oversized or control-character-carrying name never enters the process's
  // published state. Never throws.
  [[nodiscard]] ControllerRegistrationResult
  register_controller(simulation::ControllerId controller, std::string_view controller_kind,
                      std::string_view display_name);

  // Erases one entry. Idempotent: a second close reports `kUnknownControllerId` rather than
  // failing, because a session's close path may run more than once.
  [[nodiscard]] ControllerCloseResult close(simulation::ControllerId controller);

  // What a client should be told about this controller, or `std::nullopt` when the id names no
  // open controller. Returned by value.
  [[nodiscard]] std::optional<ControllerPresentation>
  find(simulation::ControllerId controller) const;

  // Whether this id names an open controller, which is exactly "this session may still submit".
  [[nodiscard]] bool contains(simulation::ControllerId controller) const;

  [[nodiscard]] std::size_t size() const;

  // Whether the two presentation strings would be accepted. Exposed so the command sink can refuse
  // a session before it consumes a ControllerId, without a second copy of the rules.
  [[nodiscard]] static bool is_valid_controller_kind(std::string_view controller_kind) noexcept;
  [[nodiscard]] static bool is_valid_display_name(std::string_view display_name) noexcept;

private:
  mutable std::shared_mutex mutex_;
  // Ordered so a diagnostic listing is in ascending ControllerId, which is the order every other
  // ordered thing in this system uses.
  std::map<simulation::ControllerId, ControllerPresentation> entries_;
};

} // namespace blob_royale::runtime

#endif
