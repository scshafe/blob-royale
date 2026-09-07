#ifndef BLOB_ROYALE_GAMEPLAY_GAMEPLAY_VALIDATION_ERROR_HPP
#define BLOB_ROYALE_GAMEPLAY_GAMEPLAY_VALIDATION_ERROR_HPP

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::gameplay {

// canonical: gameplay_validation_error -- the one exception vocabulary of blob_gameplay.
//
// One typed, coded validation error per domain library is this tree's convention:
// `SimulationValidationError` for `blob_simulation`, `ProtocolEncodingError` for `blob_protocol`,
// `GameServerError` for `blob_server`, `ApplicationInputError` for `blob_application`. A mode is a
// rule set in `blob_gameplay`, so its rejections carry `GAMEPLAY.*` codes and never borrow a
// `SIMULATION.*` one: the simulation's code list is the kernel's vocabulary and a mode adding to it
// would be the dependency running backwards (`docs/architecture/0002-simulation-architecture.md`
// § "Decision").
//
// It derives from `std::invalid_argument` exactly as `SimulationValidationError` does, so a caller
// that only needs "the declaration was rejected" -- `GameSimulation::create` propagating a mode's
// `validate_map`, or a configuration loader reporting a startup failure -- catches one type across
// both libraries while a caller that has to distinguish reads `code()`.
// related: game_mode_registry.hpp -- the unknown-mode rejection.
// related: sandbox/sandbox_mode.hpp -- the map rejection.
// related: royale/royale_configuration.hpp -- the `[royale]` balance-number rejections.
enum class GameplayValidationCode {
  kGameModeNameUnknown,
  kThrustMaximumNotFinite,
  kThrustMaximumOutOfRange,
  kSandboxMapWithoutSpawnPoint,
  kRoyaleScalarNotFinite,
  kRoyaleScalarOutOfRange,
  kRoyaleDurationTickOverflow,
  kRoyaleMapWithoutEnoughSpawnPoints,
  kRoyaleMapArenaWithinZoneMinimum,
  kRoyaleZoneEntityUnreserved,
  kRoyaleZoneAbsent,
  kRoyalePlacementLimitExceeded,
};

[[nodiscard]] constexpr std::string_view
gameplay_validation_code_name(const GameplayValidationCode code) noexcept {
  switch (code) {
  case GameplayValidationCode::kGameModeNameUnknown:
    return "GAMEPLAY.GAME_MODE_NAME_UNKNOWN";
  case GameplayValidationCode::kThrustMaximumNotFinite:
    return "GAMEPLAY.THRUST_MAXIMUM_NOT_FINITE";
  case GameplayValidationCode::kThrustMaximumOutOfRange:
    return "GAMEPLAY.THRUST_MAXIMUM_OUT_OF_RANGE";
  case GameplayValidationCode::kSandboxMapWithoutSpawnPoint:
    return "GAMEPLAY.SANDBOX_MAP_WITHOUT_SPAWN_POINT";
  case GameplayValidationCode::kRoyaleScalarNotFinite:
    return "GAMEPLAY.ROYALE_SCALAR_NOT_FINITE";
  case GameplayValidationCode::kRoyaleScalarOutOfRange:
    return "GAMEPLAY.ROYALE_SCALAR_OUT_OF_RANGE";
  case GameplayValidationCode::kRoyaleDurationTickOverflow:
    return "GAMEPLAY.ROYALE_DURATION_TICK_OVERFLOW";
  case GameplayValidationCode::kRoyaleMapWithoutEnoughSpawnPoints:
    return "GAMEPLAY.ROYALE_MAP_WITHOUT_ENOUGH_SPAWN_POINTS";
  case GameplayValidationCode::kRoyaleMapArenaWithinZoneMinimum:
    return "GAMEPLAY.ROYALE_MAP_ARENA_WITHIN_ZONE_MINIMUM";
  case GameplayValidationCode::kRoyaleZoneEntityUnreserved:
    return "GAMEPLAY.ROYALE_ZONE_ENTITY_UNRESERVED";
  case GameplayValidationCode::kRoyaleZoneAbsent:
    return "GAMEPLAY.ROYALE_ZONE_ABSENT";
  case GameplayValidationCode::kRoyalePlacementLimitExceeded:
    return "GAMEPLAY.ROYALE_PLACEMENT_LIMIT_EXCEEDED";
  }
  return "GAMEPLAY.VALIDATION_CODE_INVALID";
}

class GameplayValidationError final : public std::invalid_argument {
public:
  GameplayValidationError(const GameplayValidationCode validation_code, std::string context,
                          std::string detail)
      : std::invalid_argument(build_message(validation_code, context, detail)),
        validation_code_(validation_code), context_(std::move(context)),
        detail_(std::move(detail)) {}

  [[nodiscard]] GameplayValidationCode validation_code() const noexcept { return validation_code_; }

  [[nodiscard]] std::string_view code() const noexcept {
    return gameplay_validation_code_name(validation_code_);
  }

  [[nodiscard]] const std::string& context() const& noexcept { return context_; }
  [[nodiscard]] const std::string& context() const&& = delete;
  [[nodiscard]] const std::string& detail() const& noexcept { return detail_; }
  [[nodiscard]] const std::string& detail() const&& = delete;

private:
  [[nodiscard]] static std::string build_message(const GameplayValidationCode validation_code,
                                                 const std::string_view context,
                                                 const std::string_view detail) {
    std::string message;
    message.reserve(gameplay_validation_code_name(validation_code).size() + context.size() +
                    detail.size() + 4);
    message.append(gameplay_validation_code_name(validation_code));
    message.append(" [");
    message.append(context);
    message.append("]: ");
    message.append(detail);
    return message;
  }

  GameplayValidationCode validation_code_;
  std::string context_;
  std::string detail_;
};

} // namespace blob_royale::gameplay

#endif
