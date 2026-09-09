#include "v2_http_error.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

namespace blob_royale::protocol {

V2HttpError V2HttpError::shared(HttpError error) { return V2HttpError{std::move(error)}; }

V2HttpError V2HttpError::invalid_forwarded_client(const ForwardedClientReason reason) {
  return V2HttpError{reason};
}

V2HttpError::V2HttpError(HttpError shared_error) noexcept
    : shared_error_(std::move(shared_error)) {}

V2HttpError::V2HttpError(const ForwardedClientReason reason) noexcept
    : forwarded_client_reason_(reason) {}

V2HttpError V2HttpError::lobby_not_found() {
  return V2HttpError{LobbyErrorKind::kNotFound, std::nullopt};
}

V2HttpError V2HttpError::lobby_full(const std::uint64_t lobby_id) {
  return V2HttpError{LobbyErrorKind::kFull, lobby_id};
}

V2HttpError V2HttpError::lobby_unavailable(const std::uint64_t lobby_id) {
  return V2HttpError{LobbyErrorKind::kUnavailable, lobby_id};
}

V2HttpError::V2HttpError(const LobbyErrorKind lobby_error,
                         const std::optional<std::uint64_t> lobby_id) noexcept
    : lobby_error_(lobby_error), lobby_id_(lobby_id) {}

std::uint16_t V2HttpError::status_code() const noexcept {
  if (shared_error_.has_value()) {
    return shared_error_->status_code();
  }
  if (lobby_error_.has_value()) {
    switch (*lobby_error_) {
    case LobbyErrorKind::kNotFound:
      return std::uint16_t{404};
    case LobbyErrorKind::kFull:
      return std::uint16_t{409};
    case LobbyErrorKind::kUnavailable:
      return std::uint16_t{503};
    }
  }
  return std::uint16_t{400};
}

std::string_view V2HttpError::code() const noexcept {
  if (shared_error_.has_value()) {
    return shared_error_->code();
  }
  if (lobby_error_.has_value()) {
    switch (*lobby_error_) {
    case LobbyErrorKind::kNotFound:
      return kLobbyNotFoundCode;
    case LobbyErrorKind::kFull:
      return kLobbyFullCode;
    case LobbyErrorKind::kUnavailable:
      return kLobbyUnavailableCode;
    }
  }
  return kInvalidForwardedClientCode;
}

std::string_view V2HttpError::message() const noexcept {
  if (shared_error_.has_value()) {
    return std::string_view{shared_error_->message()};
  }
  if (lobby_error_.has_value()) {
    switch (*lobby_error_) {
    case LobbyErrorKind::kNotFound:
      return kLobbyNotFoundMessage;
    case LobbyErrorKind::kFull:
      return kLobbyFullMessage;
    case LobbyErrorKind::kUnavailable:
      return kLobbyUnavailableMessage;
    }
  }
  return kInvalidForwardedClientMessage;
}

bool V2HttpError::retryable() const noexcept {
  if (shared_error_.has_value()) {
    return shared_error_->retryable();
  }
  return lobby_error_.has_value() && *lobby_error_ != LobbyErrorKind::kNotFound;
}

const HttpError* V2HttpError::shared_error() const& noexcept {
  return shared_error_.has_value() ? &*shared_error_ : nullptr;
}

} // namespace blob_royale::protocol
