#ifndef BLOB_ROYALE_PROTOCOL_V2_HTTP_ERROR_HPP
#define BLOB_ROYALE_PROTOCOL_V2_HTTP_ERROR_HPP

#include "http_error.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace blob_royale::protocol {

// Why a proxy-forwarded connection's forwarded client address was refused.
//
// **A closed enum and not free text.** No byte of an attacker-supplied forwarding header may reach
// a response body or a log line, so the response names the failure mode and never the value
// (`docs/protocol/v2.md` § "Error registry additions").
enum class ForwardedClientReason : std::uint8_t {
  kAbsent = 0,
  kMultipleValues = 1,
  kNotCanonical = 2,
};

[[nodiscard]] constexpr std::string_view
forwarded_client_reason_name(const ForwardedClientReason reason) noexcept {
  switch (reason) {
  case ForwardedClientReason::kAbsent:
    return "absent";
  case ForwardedClientReason::kMultipleValues:
    return "multiple_values";
  case ForwardedClientReason::kNotCanonical:
    return "not_canonical";
  }
  return "forwarded_client_reason_invalid";
}

// canonical: v2_http_error -- one member of the protocol v2 status/error registry.
//
// **v2's registry is v1's fourteen rows plus exactly one.** The fourteen shared rows are the same
// statuses, codes, retryability, and details in both versions, so they stay one `HttpError` value
// and are not transcribed; only the envelope around them differs, and that difference lives in the
// encoder. The added row, `400 PROTOCOL.INVALID_FORWARDED_CLIENT`, is deliberately **not**
// constructible as an `HttpError`: adding it to v1's enum would silently widen the set a v1 client
// must accept, which is the exact reason `docs/protocol/v2.md` § "Normative language and canonical
// artifacts" gives for v2 owning a separate error schema.
//
// The added row's message is a constant rather than a caller-supplied string. The reason is already
// a closed enum precisely so no forwarding-header byte can reach the body; letting a caller pass a
// message would reopen the same path through a different member, and there is exactly one thing to
// say about this failure.
// related: http_error.hpp -- the fourteen shared rows.
// related: docs/protocol/schema/v2/error-response.schema.json -- the closed wire shape.
class V2HttpError final {
public:
  static constexpr std::string_view kInvalidForwardedClientCode =
      "PROTOCOL.INVALID_FORWARDED_CLIENT";
  static constexpr std::string_view kInvalidForwardedClientMessage =
      "A proxy-forwarded connection must present exactly one canonical forwarded client address.";

  // One of the fourteen rows v2 shares with v1, unchanged in every field.
  [[nodiscard]] static V2HttpError shared(HttpError error);

  // The one row v2 adds. Never retryable: a forwarding header a proxy got wrong does not become
  // right by being sent again.
  [[nodiscard]] static V2HttpError invalid_forwarded_client(ForwardedClientReason reason);

  V2HttpError(const V2HttpError&) = default;
  V2HttpError(V2HttpError&&) noexcept = default;
  V2HttpError& operator=(const V2HttpError&) = default;
  V2HttpError& operator=(V2HttpError&&) noexcept = default;
  ~V2HttpError() = default;

  [[nodiscard]] std::uint16_t status_code() const noexcept;
  [[nodiscard]] std::string_view code() const noexcept;
  [[nodiscard]] std::string_view message() const noexcept;
  [[nodiscard]] bool retryable() const noexcept;

  // The shared row this carries, or nullptr for the v2-only row. The encoder reads the details
  // straight off it, so the details policy has one implementation across both versions.
  [[nodiscard]] const HttpError* shared_error() const& noexcept;
  [[nodiscard]] const HttpError* shared_error() const&& = delete;

  // Present exactly on the v2-only row, which is what makes `forwarded_client_reason` a required
  // detail there and an impossible one everywhere else.
  [[nodiscard]] std::optional<ForwardedClientReason> forwarded_client_reason() const noexcept {
    return forwarded_client_reason_;
  }

  friend bool operator==(const V2HttpError&, const V2HttpError&) = default;

private:
  explicit V2HttpError(HttpError shared_error) noexcept;
  explicit V2HttpError(ForwardedClientReason reason) noexcept;

  std::optional<HttpError> shared_error_;
  std::optional<ForwardedClientReason> forwarded_client_reason_;
};

} // namespace blob_royale::protocol

#endif
