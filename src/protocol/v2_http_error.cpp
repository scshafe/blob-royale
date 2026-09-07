#include "v2_http_error.hpp"

#include <cstdint>
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

std::uint16_t V2HttpError::status_code() const noexcept {
  return shared_error_.has_value() ? shared_error_->status_code() : std::uint16_t{400};
}

std::string_view V2HttpError::code() const noexcept {
  return shared_error_.has_value() ? shared_error_->code() : kInvalidForwardedClientCode;
}

std::string_view V2HttpError::message() const noexcept {
  return shared_error_.has_value() ? std::string_view{shared_error_->message()}
                                   : kInvalidForwardedClientMessage;
}

bool V2HttpError::retryable() const noexcept {
  return shared_error_.has_value() && shared_error_->retryable();
}

const HttpError* V2HttpError::shared_error() const& noexcept {
  return shared_error_.has_value() ? &*shared_error_ : nullptr;
}

} // namespace blob_royale::protocol
