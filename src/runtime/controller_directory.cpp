#include "controller_directory.hpp"

#include <mutex>
#include <shared_mutex>
#include <string>

namespace blob_royale::runtime {
namespace {

// A printable, single-line string bounded in bytes. Control characters are refused rather than
// stripped: silently rewriting a proxy-supplied name would publish something the operator never
// configured and hide the boundary that produced it. UTF-8 continuation bytes are all >= 0x80 and
// pass, so a non-ASCII display name is stored verbatim and escaped by the wire encoder.
[[nodiscard]] bool is_bounded_printable(const std::string_view value,
                                        const std::size_t maximum_length) noexcept {
  if (value.size() > maximum_length) {
    return false;
  }
  for (const char character : value) {
    const auto byte = static_cast<unsigned char>(character);
    if (byte < 0x20 || byte == 0x7F) {
      return false;
    }
  }
  return true;
}

} // namespace

bool ControllerDirectory::is_valid_controller_kind(
    const std::string_view controller_kind) noexcept {
  return !controller_kind.empty() &&
         is_bounded_printable(controller_kind, kMaximumControllerKindLength);
}

bool ControllerDirectory::is_valid_display_name(const std::string_view display_name) noexcept {
  // An empty display name is accepted: a trusted proxy may supply no name, and the wire encoder
  // rather than this directory decides what a nameless player renders as.
  return is_bounded_printable(display_name, kMaximumDisplayNameLength);
}

ControllerRegistrationResult
ControllerDirectory::register_controller(const simulation::ControllerId controller,
                                         const std::string_view controller_kind,
                                         const std::string_view display_name) {
  if (!is_valid_controller_kind(controller_kind)) {
    return ControllerRegistrationResult::kRejectedControllerKind;
  }
  if (!is_valid_display_name(display_name)) {
    return ControllerRegistrationResult::kRejectedDisplayName;
  }

  const std::lock_guard lock(mutex_);
  if (entries_.contains(controller)) {
    return ControllerRegistrationResult::kRejectedDuplicateControllerId;
  }
  if (entries_.size() >= kMaximumControllerDirectoryEntryCount) {
    return ControllerRegistrationResult::kRejectedDirectoryFull;
  }
  entries_.emplace(controller,
                   ControllerPresentation{.controller_kind = std::string(controller_kind),
                                          .display_name = std::string(display_name)});
  return ControllerRegistrationResult::kRegistered;
}

ControllerCloseResult ControllerDirectory::close(const simulation::ControllerId controller) {
  const std::lock_guard lock(mutex_);
  if (entries_.erase(controller) == 0) {
    return ControllerCloseResult::kUnknownControllerId;
  }
  return ControllerCloseResult::kClosed;
}

std::optional<ControllerPresentation>
ControllerDirectory::find(const simulation::ControllerId controller) const {
  const std::shared_lock lock(mutex_);
  const auto entry = entries_.find(controller);
  if (entry == entries_.end()) {
    return std::nullopt;
  }
  return entry->second;
}

bool ControllerDirectory::contains(const simulation::ControllerId controller) const {
  const std::shared_lock lock(mutex_);
  return entries_.contains(controller);
}

std::size_t ControllerDirectory::size() const {
  const std::shared_lock lock(mutex_);
  return entries_.size();
}

} // namespace blob_royale::runtime
