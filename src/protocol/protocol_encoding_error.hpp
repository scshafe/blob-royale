#ifndef BLOB_ROYALE_PROTOCOL_PROTOCOL_ENCODING_ERROR_HPP
#define BLOB_ROYALE_PROTOCOL_PROTOCOL_ENCODING_ERROR_HPP

#include <stdexcept>
#include <string>
#include <string_view>

namespace blob_royale::protocol {

enum class ProtocolEncodingErrorCode {
  kRequestIdInvalid,
  kPublicConfigurationInvalid,
  kHttpErrorInvalid,
  kOutputByteLimitInvalid,
  kMessageSequenceOutOfRange,
  kTimestampInvalid,
  kSnapshotTickOutOfRange,
  kEncodedPayloadTooLarge,
  kSessionWelcomeInvalid,
  kSnapshotEntityOrderInvalid,
  kSnapshotEntityLimitExceeded,
  kPlacementLimitExceeded,
  kSeatLimitExceeded,
  kMatchModeNameInvalid,
  kComponentValueOutOfRange,
  kLobbyDirectoryInvalid,
  kRandomDrawCountOutOfRange,
  kMovementTuningResultInvalid,
  kMovementTuningStateInvalid,
};

[[nodiscard]] constexpr std::string_view
protocol_encoding_error_code_name(ProtocolEncodingErrorCode error_code) noexcept {
  switch (error_code) {
  case ProtocolEncodingErrorCode::kRequestIdInvalid:
    return "PROTOCOL.ENCODING.REQUEST_ID_INVALID";
  case ProtocolEncodingErrorCode::kPublicConfigurationInvalid:
    return "PROTOCOL.ENCODING.PUBLIC_CONFIGURATION_INVALID";
  case ProtocolEncodingErrorCode::kHttpErrorInvalid:
    return "PROTOCOL.ENCODING.HTTP_ERROR_INVALID";
  case ProtocolEncodingErrorCode::kOutputByteLimitInvalid:
    return "PROTOCOL.ENCODING.OUTPUT_BYTE_LIMIT_INVALID";
  case ProtocolEncodingErrorCode::kMessageSequenceOutOfRange:
    return "PROTOCOL.ENCODING.MESSAGE_SEQUENCE_OUT_OF_RANGE";
  case ProtocolEncodingErrorCode::kTimestampInvalid:
    return "PROTOCOL.ENCODING.TIMESTAMP_INVALID";
  case ProtocolEncodingErrorCode::kSnapshotTickOutOfRange:
    return "PROTOCOL.ENCODING.SNAPSHOT_TICK_OUT_OF_RANGE";
  case ProtocolEncodingErrorCode::kEncodedPayloadTooLarge:
    return "PROTOCOL.ENCODING.PAYLOAD_TOO_LARGE";
  case ProtocolEncodingErrorCode::kSessionWelcomeInvalid:
    return "PROTOCOL.ENCODING.SESSION_WELCOME_INVALID";
  case ProtocolEncodingErrorCode::kSnapshotEntityOrderInvalid:
    return "PROTOCOL.ENCODING.SNAPSHOT_ENTITY_ORDER_INVALID";
  case ProtocolEncodingErrorCode::kSnapshotEntityLimitExceeded:
    return "PROTOCOL.ENCODING.SNAPSHOT_ENTITY_LIMIT_EXCEEDED";
  case ProtocolEncodingErrorCode::kPlacementLimitExceeded:
    return "PROTOCOL.ENCODING.PLACEMENT_LIMIT_EXCEEDED";
  case ProtocolEncodingErrorCode::kSeatLimitExceeded:
    return "PROTOCOL.ENCODING.SEAT_LIMIT_EXCEEDED";
  case ProtocolEncodingErrorCode::kMatchModeNameInvalid:
    return "PROTOCOL.ENCODING.MATCH_MODE_NAME_INVALID";
  case ProtocolEncodingErrorCode::kComponentValueOutOfRange:
    return "PROTOCOL.ENCODING.COMPONENT_VALUE_OUT_OF_RANGE";
  case ProtocolEncodingErrorCode::kLobbyDirectoryInvalid:
    return "PROTOCOL.ENCODING.LOBBY_DIRECTORY_INVALID";
  case ProtocolEncodingErrorCode::kRandomDrawCountOutOfRange:
    return "PROTOCOL.ENCODING.RANDOM_DRAW_COUNT_OUT_OF_RANGE";
  case ProtocolEncodingErrorCode::kMovementTuningResultInvalid:
    return "PROTOCOL.ENCODING.MOVEMENT_TUNING_RESULT_INVALID";
  case ProtocolEncodingErrorCode::kMovementTuningStateInvalid:
    return "PROTOCOL.ENCODING.MOVEMENT_TUNING_STATE_INVALID";
  }
  return "PROTOCOL.ENCODING.ERROR_CODE_INVALID";
}

class ProtocolEncodingError final : public std::runtime_error {
public:
  // Reports one rejected protocol value or bounded-encoding failure with stable context.
  ProtocolEncodingError(ProtocolEncodingErrorCode error_code, std::string context,
                        std::string detail);

  [[nodiscard]] ProtocolEncodingErrorCode error_code() const noexcept { return error_code_; }
  [[nodiscard]] std::string_view code() const noexcept {
    return protocol_encoding_error_code_name(error_code_);
  }
  [[nodiscard]] const std::string& context() const& noexcept { return context_; }
  [[nodiscard]] const std::string& context() const&& = delete;
  [[nodiscard]] const std::string& detail() const& noexcept { return detail_; }
  [[nodiscard]] const std::string& detail() const&& = delete;

private:
  [[nodiscard]] static std::string build_message(ProtocolEncodingErrorCode error_code,
                                                 std::string_view context, std::string_view detail);

  ProtocolEncodingErrorCode error_code_;
  std::string context_;
  std::string detail_;
};

} // namespace blob_royale::protocol

#endif
