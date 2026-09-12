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
// The list is grouped by owner: the registry, then the mechanics in `shared/` that any mode may
// declare, then one group per mode. A `GAMEPLAY.DURATION_*` or `GAMEPLAY.HAZARD_*` rejection is
// deliberately not `GAMEPLAY.ROYALE_*`, because a mode-agnostic mechanic that named one mode in its
// diagnostics would send a reader to the wrong section of the configuration file.
// related: game_mode_registry.hpp -- the unknown-mode rejection.
// related: shared/duration_ticks.hpp -- the `GAMEPLAY.DURATION_*` rejections of any authored
// duration, whichever section authored it.
// related: shared/hazard_archetype.hpp -- the `[hazard.<kind>]` rejections.
// related: sandbox/sandbox_mode.hpp -- the map rejection.
// related: royale/royale_configuration.hpp -- the `[royale]` balance-number rejections.
enum class GameplayValidationCode {
  kGameModeNameUnknown,
  kThrustMaximumNotFinite,
  kThrustMaximumOutOfRange,
  kDurationNotFinite,
  kDurationNegative,
  kDurationTickOverflow,
  kHazardKindNameInvalid,
  kHazardScalarNotFinite,
  kHazardScalarOutOfRange,
  kSandboxMapWithoutSpawnPoint,
  kRoyaleScalarNotFinite,
  kRoyaleScalarOutOfRange,
  kRoyaleMapArenaWithinZoneMinimum,
  kRoyaleZoneEntityUnreserved,
  kRoyaleZoneAbsent,
  kRoyalePlacementLimitExceeded,
  kRoyaleEliminatedEntityWithoutController,
  kKingOfTheHillScalarNotFinite,
  kKingOfTheHillScalarOutOfRange,
  kRaceScalarNotFinite,
  kRaceScalarOutOfRange,
  kRaceRoadNameInvalid,
  kKingOfTheHillTourWithoutDuration,
  kKingOfTheHillPointsToWinZero,
  kKingOfTheHillMapWithoutHill,
  kKingOfTheHillMapWithoutSpawnPoint,
  kKingOfTheHillHillEntityUnreserved,
  kKingOfTheHillHillAbsent,
  kKingOfTheHillMotionPolicyInvalid,
  kKingOfTheHillMotionStateInvalid,
  kKingOfTheHillRetargetTickOverflow,
  kRaceMapRoadMissing,
  kRaceMapWithoutCheckpoint,
  kRaceMapCheckpointOutsideCorridor,
  kRaceMapSpawnPointOutsideCorridor,
  kRaceMapWithoutSpawnPoint,
  kRaceCourseUnbound,
  kRaceProgressBeyondCourse,
  kRaceStandingLimitExceeded,
  kLocomotionPrecisionLost,
  kStatusActivationTickZero,
};

[[nodiscard]] constexpr std::string_view
gameplay_validation_code_name(const GameplayValidationCode code) noexcept {
  switch (code) {
  case GameplayValidationCode::kStatusActivationTickZero:
    return "GAMEPLAY.STATUS_ACTIVATION_TICK_ZERO";
  case GameplayValidationCode::kGameModeNameUnknown:
    return "GAMEPLAY.GAME_MODE_NAME_UNKNOWN";
  case GameplayValidationCode::kThrustMaximumNotFinite:
    return "GAMEPLAY.THRUST_MAXIMUM_NOT_FINITE";
  case GameplayValidationCode::kThrustMaximumOutOfRange:
    return "GAMEPLAY.THRUST_MAXIMUM_OUT_OF_RANGE";
  case GameplayValidationCode::kDurationNotFinite:
    return "GAMEPLAY.DURATION_NOT_FINITE";
  case GameplayValidationCode::kDurationNegative:
    return "GAMEPLAY.DURATION_NEGATIVE";
  case GameplayValidationCode::kDurationTickOverflow:
    return "GAMEPLAY.DURATION_TICK_OVERFLOW";
  case GameplayValidationCode::kHazardKindNameInvalid:
    return "GAMEPLAY.HAZARD_KIND_NAME_INVALID";
  case GameplayValidationCode::kHazardScalarNotFinite:
    return "GAMEPLAY.HAZARD_SCALAR_NOT_FINITE";
  case GameplayValidationCode::kHazardScalarOutOfRange:
    return "GAMEPLAY.HAZARD_SCALAR_OUT_OF_RANGE";
  case GameplayValidationCode::kSandboxMapWithoutSpawnPoint:
    return "GAMEPLAY.SANDBOX_MAP_WITHOUT_SPAWN_POINT";
  case GameplayValidationCode::kRoyaleScalarNotFinite:
    return "GAMEPLAY.ROYALE_SCALAR_NOT_FINITE";
  case GameplayValidationCode::kRoyaleScalarOutOfRange:
    return "GAMEPLAY.ROYALE_SCALAR_OUT_OF_RANGE";
  case GameplayValidationCode::kRoyaleMapArenaWithinZoneMinimum:
    return "GAMEPLAY.ROYALE_MAP_ARENA_WITHIN_ZONE_MINIMUM";
  case GameplayValidationCode::kRoyaleZoneEntityUnreserved:
    return "GAMEPLAY.ROYALE_ZONE_ENTITY_UNRESERVED";
  case GameplayValidationCode::kRoyaleZoneAbsent:
    return "GAMEPLAY.ROYALE_ZONE_ABSENT";
  case GameplayValidationCode::kRoyalePlacementLimitExceeded:
    return "GAMEPLAY.ROYALE_PLACEMENT_LIMIT_EXCEEDED";
  case GameplayValidationCode::kRoyaleEliminatedEntityWithoutController:
    return "GAMEPLAY.ROYALE_ELIMINATED_ENTITY_WITHOUT_CONTROLLER";
  case GameplayValidationCode::kKingOfTheHillScalarNotFinite:
    return "GAMEPLAY.KING_OF_THE_HILL_SCALAR_NOT_FINITE";
  case GameplayValidationCode::kKingOfTheHillScalarOutOfRange:
    return "GAMEPLAY.KING_OF_THE_HILL_SCALAR_OUT_OF_RANGE";
  case GameplayValidationCode::kRaceScalarNotFinite:
    return "GAMEPLAY.RACE_SCALAR_NOT_FINITE";
  case GameplayValidationCode::kRaceScalarOutOfRange:
    return "GAMEPLAY.RACE_SCALAR_OUT_OF_RANGE";
  case GameplayValidationCode::kRaceRoadNameInvalid:
    return "GAMEPLAY.RACE_ROAD_NAME_INVALID";
  case GameplayValidationCode::kKingOfTheHillTourWithoutDuration:
    return "GAMEPLAY.KING_OF_THE_HILL_TOUR_WITHOUT_DURATION";
  case GameplayValidationCode::kKingOfTheHillPointsToWinZero:
    return "GAMEPLAY.KING_OF_THE_HILL_POINTS_TO_WIN_ZERO";
  case GameplayValidationCode::kKingOfTheHillMapWithoutHill:
    return "GAMEPLAY.KING_OF_THE_HILL_MAP_WITHOUT_HILL";
  case GameplayValidationCode::kKingOfTheHillMapWithoutSpawnPoint:
    return "GAMEPLAY.KING_OF_THE_HILL_MAP_WITHOUT_SPAWN_POINT";
  case GameplayValidationCode::kKingOfTheHillHillEntityUnreserved:
    return "GAMEPLAY.KING_OF_THE_HILL_HILL_ENTITY_UNRESERVED";
  case GameplayValidationCode::kKingOfTheHillHillAbsent:
    return "GAMEPLAY.KING_OF_THE_HILL_HILL_ABSENT";
  case GameplayValidationCode::kKingOfTheHillMotionPolicyInvalid:
    return "GAMEPLAY.KING_OF_THE_HILL_MOTION_POLICY_INVALID";
  case GameplayValidationCode::kKingOfTheHillMotionStateInvalid:
    return "GAMEPLAY.KING_OF_THE_HILL_MOTION_STATE_INVALID";
  case GameplayValidationCode::kKingOfTheHillRetargetTickOverflow:
    return "GAMEPLAY.KING_OF_THE_HILL_RETARGET_TICK_OVERFLOW";
  case GameplayValidationCode::kRaceMapRoadMissing:
    return "GAMEPLAY.RACE_MAP_ROAD_MISSING";
  case GameplayValidationCode::kRaceMapWithoutCheckpoint:
    return "GAMEPLAY.RACE_MAP_WITHOUT_CHECKPOINT";
  case GameplayValidationCode::kRaceMapCheckpointOutsideCorridor:
    return "GAMEPLAY.RACE_MAP_CHECKPOINT_OUTSIDE_CORRIDOR";
  case GameplayValidationCode::kRaceMapSpawnPointOutsideCorridor:
    return "GAMEPLAY.RACE_MAP_SPAWN_POINT_OUTSIDE_CORRIDOR";
  case GameplayValidationCode::kRaceMapWithoutSpawnPoint:
    return "GAMEPLAY.RACE_MAP_WITHOUT_SPAWN_POINT";
  case GameplayValidationCode::kRaceCourseUnbound:
    return "GAMEPLAY.RACE_COURSE_UNBOUND";
  case GameplayValidationCode::kRaceProgressBeyondCourse:
    return "GAMEPLAY.RACE_PROGRESS_BEYOND_COURSE";
  case GameplayValidationCode::kRaceStandingLimitExceeded:
    return "GAMEPLAY.RACE_STANDING_LIMIT_EXCEEDED";
  case GameplayValidationCode::kLocomotionPrecisionLost:
    return "GAMEPLAY.LOCOMOTION_PRECISION_LOST";
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
