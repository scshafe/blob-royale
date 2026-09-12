#ifndef BLOB_ROYALE_APPLICATION_APPLICATION_INPUT_ERROR_HPP
#define BLOB_ROYALE_APPLICATION_APPLICATION_INPUT_ERROR_HPP

#include <stdexcept>
#include <string>
#include <string_view>

namespace blob_royale::application {

enum class ApplicationInputErrorCode {
  kCommandLineInvalid,
  kConfigurationFileMissing,
  kConfigurationPathNotRegularFile,
  kConfigurationFileTooLarge,
  kConfigurationFileReadFailed,
  kConfigurationSyntaxInvalid,
  kConfigurationSectionUnknown,
  kConfigurationSectionDuplicate,
  kConfigurationSectionLimitExceeded,
  kConfigurationKeyUnknown,
  kConfigurationKeyDuplicate,
  kConfigurationKeyMissing,
  kConfigurationValueInvalid,
  kConfigurationValueOutOfRange,
  kScenarioFileMissing,
  kScenarioPathNotRegularFile,
  kScenarioFileTooLarge,
  kScenarioFileReadFailed,
  kScenarioHeaderInvalid,
  kScenarioRowEmpty,
  kScenarioRowTooLong,
  kScenarioColumnCountInvalid,
  kScenarioValueInvalid,
  kScenarioValueNotFinite,
  kScenarioValueOutOfRange,
  kScenarioEntityIdOutOfRange,
  kScenarioEntityIdDuplicate,
  kScenarioPlayerLimitExceeded,
  kScenarioPositionOutOfBounds,
  kMatchModeNameInvalid,
  kMatchModeUnknown,
  kMatchMapNameInvalid,
  kMatchBotRosterInvalid,
  kMatchBotKindUnknown,
  kMatchBotRosterTooLarge,
  kMatchMapBoundsMismatch,
  kMatchEntityBudgetExceeded,
  kMatchBotsExceedSeats,
  kMatchLobbySeatCountOutOfRange,
  kMatchLobbyExceedsSpawnMarkers,
  kLobbiesCountOutOfRange,
  kLobbiesScenarioRequiresOneLobby,
  kMapFileMissing,
  kMapPathNotRegularFile,
  kMapFileTooLarge,
  kMapFileReadFailed,
  kMapHeaderInvalid,
  kMapRowEmpty,
  kMapRowTooLong,
  kMapColumnCountInvalid,
  kMapValueInvalid,
  kMapNameMismatch,
  kMatchBotProfileRequired,
  kMatchBotProfileUnexpected,
  kMatchBotProfileUnknown,
  kMatchBotProfileModeUnsupported,
};

[[nodiscard]] constexpr std::string_view
application_input_error_code_name(const ApplicationInputErrorCode code) noexcept {
  switch (code) {
  case ApplicationInputErrorCode::kCommandLineInvalid:
    return "APPLICATION.CLI.INVALID";
  case ApplicationInputErrorCode::kConfigurationFileMissing:
    return "APPLICATION.CONFIG.FILE_MISSING";
  case ApplicationInputErrorCode::kConfigurationPathNotRegularFile:
    return "APPLICATION.CONFIG.PATH_NOT_REGULAR_FILE";
  case ApplicationInputErrorCode::kConfigurationFileTooLarge:
    return "APPLICATION.CONFIG.FILE_TOO_LARGE";
  case ApplicationInputErrorCode::kConfigurationFileReadFailed:
    return "APPLICATION.CONFIG.FILE_READ_FAILED";
  case ApplicationInputErrorCode::kConfigurationSyntaxInvalid:
    return "APPLICATION.CONFIG.SYNTAX_INVALID";
  case ApplicationInputErrorCode::kConfigurationSectionUnknown:
    return "APPLICATION.CONFIG.SECTION_UNKNOWN";
  case ApplicationInputErrorCode::kConfigurationSectionDuplicate:
    return "APPLICATION.CONFIG.SECTION_DUPLICATE";
  case ApplicationInputErrorCode::kConfigurationSectionLimitExceeded:
    return "APPLICATION.CONFIG.SECTION_LIMIT_EXCEEDED";
  case ApplicationInputErrorCode::kConfigurationKeyUnknown:
    return "APPLICATION.CONFIG.KEY_UNKNOWN";
  case ApplicationInputErrorCode::kConfigurationKeyDuplicate:
    return "APPLICATION.CONFIG.KEY_DUPLICATE";
  case ApplicationInputErrorCode::kConfigurationKeyMissing:
    return "APPLICATION.CONFIG.KEY_MISSING";
  case ApplicationInputErrorCode::kConfigurationValueInvalid:
    return "APPLICATION.CONFIG.VALUE_INVALID";
  case ApplicationInputErrorCode::kConfigurationValueOutOfRange:
    return "APPLICATION.CONFIG.VALUE_OUT_OF_RANGE";
  case ApplicationInputErrorCode::kScenarioFileMissing:
    return "APPLICATION.SCENARIO.FILE_MISSING";
  case ApplicationInputErrorCode::kScenarioPathNotRegularFile:
    return "APPLICATION.SCENARIO.PATH_NOT_REGULAR_FILE";
  case ApplicationInputErrorCode::kScenarioFileTooLarge:
    return "APPLICATION.SCENARIO.FILE_TOO_LARGE";
  case ApplicationInputErrorCode::kScenarioFileReadFailed:
    return "APPLICATION.SCENARIO.FILE_READ_FAILED";
  case ApplicationInputErrorCode::kScenarioHeaderInvalid:
    return "APPLICATION.SCENARIO.HEADER_INVALID";
  case ApplicationInputErrorCode::kScenarioRowEmpty:
    return "APPLICATION.SCENARIO.ROW_EMPTY";
  case ApplicationInputErrorCode::kScenarioRowTooLong:
    return "APPLICATION.SCENARIO.ROW_TOO_LONG";
  case ApplicationInputErrorCode::kScenarioColumnCountInvalid:
    return "APPLICATION.SCENARIO.COLUMN_COUNT_INVALID";
  case ApplicationInputErrorCode::kScenarioValueInvalid:
    return "APPLICATION.SCENARIO.VALUE_INVALID";
  case ApplicationInputErrorCode::kScenarioValueNotFinite:
    return "APPLICATION.SCENARIO.VALUE_NOT_FINITE";
  case ApplicationInputErrorCode::kScenarioValueOutOfRange:
    return "APPLICATION.SCENARIO.VALUE_OUT_OF_RANGE";
  case ApplicationInputErrorCode::kScenarioEntityIdOutOfRange:
    return "APPLICATION.SCENARIO.ENTITY_ID_OUT_OF_RANGE";
  case ApplicationInputErrorCode::kScenarioEntityIdDuplicate:
    return "APPLICATION.SCENARIO.ENTITY_ID_DUPLICATE";
  case ApplicationInputErrorCode::kScenarioPlayerLimitExceeded:
    return "APPLICATION.SCENARIO.PLAYER_LIMIT_EXCEEDED";
  case ApplicationInputErrorCode::kScenarioPositionOutOfBounds:
    return "APPLICATION.SCENARIO.POSITION_OUT_OF_BOUNDS";
  case ApplicationInputErrorCode::kMatchModeNameInvalid:
    return "APPLICATION.MATCH.MODE_NAME_INVALID";
  case ApplicationInputErrorCode::kMatchModeUnknown:
    return "APPLICATION.MATCH.MODE_UNKNOWN";
  case ApplicationInputErrorCode::kMatchMapNameInvalid:
    return "APPLICATION.MATCH.MAP_NAME_INVALID";
  case ApplicationInputErrorCode::kMatchBotRosterInvalid:
    return "APPLICATION.MATCH.BOT_ROSTER_INVALID";
  case ApplicationInputErrorCode::kMatchBotKindUnknown:
    return "APPLICATION.MATCH.BOT_KIND_UNKNOWN";
  case ApplicationInputErrorCode::kMatchBotRosterTooLarge:
    return "APPLICATION.MATCH.BOT_ROSTER_TOO_LARGE";
  case ApplicationInputErrorCode::kMatchMapBoundsMismatch:
    return "APPLICATION.MATCH.MAP_BOUNDS_MISMATCH";
  case ApplicationInputErrorCode::kMatchEntityBudgetExceeded:
    return "APPLICATION.MATCH.ENTITY_BUDGET_EXCEEDED";
  case ApplicationInputErrorCode::kMatchBotsExceedSeats:
    return "APPLICATION.MATCH.BOTS_EXCEED_SEATS";
  case ApplicationInputErrorCode::kMatchLobbySeatCountOutOfRange:
    return "APPLICATION.MATCH.LOBBY_SEAT_COUNT_OUT_OF_RANGE";
  case ApplicationInputErrorCode::kMatchLobbyExceedsSpawnMarkers:
    return "APPLICATION.MATCH.LOBBY_EXCEEDS_SPAWN_MARKERS";
  case ApplicationInputErrorCode::kLobbiesCountOutOfRange:
    return "APPLICATION.LOBBIES.COUNT_OUT_OF_RANGE";
  case ApplicationInputErrorCode::kLobbiesScenarioRequiresOneLobby:
    return "APPLICATION.LOBBIES.SCENARIO_REQUIRES_ONE_LOBBY";
  case ApplicationInputErrorCode::kMapFileMissing:
    return "APPLICATION.MAP.FILE_MISSING";
  case ApplicationInputErrorCode::kMapPathNotRegularFile:
    return "APPLICATION.MAP.PATH_NOT_REGULAR_FILE";
  case ApplicationInputErrorCode::kMapFileTooLarge:
    return "APPLICATION.MAP.FILE_TOO_LARGE";
  case ApplicationInputErrorCode::kMapFileReadFailed:
    return "APPLICATION.MAP.FILE_READ_FAILED";
  case ApplicationInputErrorCode::kMapHeaderInvalid:
    return "APPLICATION.MAP.HEADER_INVALID";
  case ApplicationInputErrorCode::kMapRowEmpty:
    return "APPLICATION.MAP.ROW_EMPTY";
  case ApplicationInputErrorCode::kMapRowTooLong:
    return "APPLICATION.MAP.ROW_TOO_LONG";
  case ApplicationInputErrorCode::kMapColumnCountInvalid:
    return "APPLICATION.MAP.COLUMN_COUNT_INVALID";
  case ApplicationInputErrorCode::kMapValueInvalid:
    return "APPLICATION.MAP.VALUE_INVALID";
  case ApplicationInputErrorCode::kMapNameMismatch:
    return "APPLICATION.MAP.NAME_MISMATCH";
  case ApplicationInputErrorCode::kMatchBotProfileRequired:
    return "APPLICATION.MATCH.BOT_PROFILE_REQUIRED";
  case ApplicationInputErrorCode::kMatchBotProfileUnexpected:
    return "APPLICATION.MATCH.BOT_PROFILE_UNEXPECTED";
  case ApplicationInputErrorCode::kMatchBotProfileUnknown:
    return "APPLICATION.MATCH.BOT_PROFILE_UNKNOWN";
  case ApplicationInputErrorCode::kMatchBotProfileModeUnsupported:
    return "APPLICATION.MATCH.BOT_PROFILE_MODE_UNSUPPORTED";
  }
  return "APPLICATION.INPUT.ERROR_CODE_INVALID";
}

// canonical: application_input_error -- all CLI, INI, and scenario boundary failures.
class ApplicationInputError final : public std::runtime_error {
public:
  // Constructs a descriptive boundary error with a stable machine code and source context.
  ApplicationInputError(ApplicationInputErrorCode error_code, std::string context,
                        std::string detail);

  [[nodiscard]] ApplicationInputErrorCode error_code() const noexcept { return error_code_; }
  [[nodiscard]] std::string_view code() const noexcept {
    return application_input_error_code_name(error_code_);
  }
  [[nodiscard]] const std::string& context() const& noexcept { return context_; }
  [[nodiscard]] const std::string& context() const&& = delete;
  [[nodiscard]] const std::string& detail() const& noexcept { return detail_; }
  [[nodiscard]] const std::string& detail() const&& = delete;

private:
  [[nodiscard]] static std::string build_message(ApplicationInputErrorCode error_code,
                                                 std::string_view context, std::string_view detail);

  ApplicationInputErrorCode error_code_;
  std::string context_;
  std::string detail_;
};

} // namespace blob_royale::application

#endif
