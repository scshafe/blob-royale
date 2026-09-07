#ifndef BLOB_ROYALE_SIMULATION_SIMULATION_VALIDATION_ERROR_HPP
#define BLOB_ROYALE_SIMULATION_SIMULATION_VALIDATION_ERROR_HPP

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::simulation {

// canonical: simulation_validation_error -- the one exception vocabulary of blob_simulation.
//
// **Every rejection and every violated invariant in this domain is a SimulationValidationError
// carrying one greppable code.** There is no second vocabulary: the engine invariants that used to
// throw a bare `std::logic_error` -- an unknown id reached through the spatial index, an incoherent
// wall-motion list, a committed index that is not a rebuild of the committed world -- carry
// `SIMULATION.GAME_SIMULATION_*` codes like every other failure here (engine review finding 13).
// A caller that has to distinguish an input rejection from a broken invariant reads the code; two
// exception types would make that distinction a `catch` clause and would leave half the failures
// unnameable in a log filter.
enum class SimulationValidationCode {
  kPhysicalScalarNotFinite,
  kPhysicalScalarOutOfRange,
  kVectorDivisionByZero,
  kEntityIdOutOfRange,
  kControllerIdOutOfRange,
  kTeamIdOutOfRange,
  kComponentStoreDuplicateEntityId,
  kComponentStoreLimitExceeded,
  kCommandKindMaskUnknownBit,
  kEntityIdReservationLimitExceeded,
  kEntityIdReservationOutOfRange,
  kEntityIdReservationExhausted,
  kInputBatchCommandLimitExceeded,
  kInputBatchCommandKindNotAccepted,
  kInputBatchThrustDirectionOutOfRange,
  kInputBatchSpawnAndDespawnConflict,
  kGameWorldEntityLimitExceeded,
  kGameWorldDuplicateEntityId,
  kGameWorldEventLimitExceeded,
  kDeterministicRandomBoundEmpty,
  kSpawnPolicyIndexOutOfRange,
  kSpawnPolicyPointOccupied,
  kMapSpawnPointNotSeatable,
  kGameSimulationSetupConflict,
  kSystemPipelineSystemMissing,
  kSystemPipelineSystemNameEmpty,
  kSystemPipelineDuplicateSystemName,
  kSystemPipelineStageUnknown,
  kContactResponseBodiesAbsent,
  kContactRuleNameInvalid,
  kContactRulePredicateMissing,
  kContactRuleResponseMissing,
  kContactRuleTableDuplicateRuleName,
  kArenaBoundsScalarNotFinite,
  kArenaBoundsScalarOutOfRange,
  kMapNameInvalid,
  kMapStaticBodyNotStatic,
  kMapStaticBodyOutOfBounds,
  kMapStaticBodyLimitExceeded,
  kMapMarkerKindInvalid,
  kMapMarkerOutOfBounds,
  kMapMarkerLimitExceeded,
  kMapMetadataKeyInvalid,
  kMapMetadataValueInvalid,
  kMapMetadataDuplicateKey,
  kMapMetadataLimitExceeded,
  kGameSimulationBodyOutOfBounds,
  kGameSimulationSpatialIndexUnknownEntityId,
  kGameSimulationSpatialIndexStale,
  kGameSimulationWallMotionIncoherent,
  kTickSequenceOutOfRange,
  kConfigWorldScalarNotFinite,
  kConfigWorldScalarOutOfRange,
  kConfigWorldTooSmallForPlayer,
  kConfigTickRateUnsupported,
  kConfigDragNotFinite,
  kConfigDragOutOfRange,
  kConfigSpatialGridDimensionOutOfRange,
  kConfigSpatialGridCellLimitExceeded,
  kCandidatePairDuplicateEntityId,
  kSpatialGridGeometryInvalid,
  kSpatialGridCellCountExceeded,
  kSpatialGridMembershipLimitExceeded,
  kSpatialGridCandidatePairLimitExceeded,
  kSpatialGridPlayerCenterOutOfBounds,
  kSpatialGridStaticBodyOutOfBounds,
  kSpatialGridPointOutOfBounds,
  kSpatialGridCellCoordinateOutOfBounds,
};

[[nodiscard]] constexpr std::string_view
simulation_validation_code_name(const SimulationValidationCode code) noexcept {
  switch (code) {
  case SimulationValidationCode::kPhysicalScalarNotFinite:
    return "SIMULATION.PHYSICAL_SCALAR_NOT_FINITE";
  case SimulationValidationCode::kPhysicalScalarOutOfRange:
    return "SIMULATION.PHYSICAL_SCALAR_OUT_OF_RANGE";
  case SimulationValidationCode::kVectorDivisionByZero:
    return "SIMULATION.VECTOR_DIVISION_BY_ZERO";
  case SimulationValidationCode::kEntityIdOutOfRange:
    return "SIMULATION.ENTITY_ID_OUT_OF_RANGE";
  case SimulationValidationCode::kControllerIdOutOfRange:
    return "SIMULATION.CONTROLLER_ID_OUT_OF_RANGE";
  case SimulationValidationCode::kTeamIdOutOfRange:
    return "SIMULATION.TEAM_ID_OUT_OF_RANGE";
  case SimulationValidationCode::kComponentStoreDuplicateEntityId:
    return "SIMULATION.COMPONENT_STORE_DUPLICATE_ENTITY_ID";
  case SimulationValidationCode::kComponentStoreLimitExceeded:
    return "SIMULATION.COMPONENT_STORE_LIMIT_EXCEEDED";
  case SimulationValidationCode::kCommandKindMaskUnknownBit:
    return "SIMULATION.COMMAND_KIND_MASK_UNKNOWN_BIT";
  case SimulationValidationCode::kEntityIdReservationLimitExceeded:
    return "SIMULATION.ENTITY_ID_RESERVATION_LIMIT_EXCEEDED";
  case SimulationValidationCode::kEntityIdReservationOutOfRange:
    return "SIMULATION.ENTITY_ID_RESERVATION_OUT_OF_RANGE";
  case SimulationValidationCode::kEntityIdReservationExhausted:
    return "SIMULATION.ENTITY_ID_RESERVATION_EXHAUSTED";
  case SimulationValidationCode::kInputBatchCommandLimitExceeded:
    return "SIMULATION.INPUT_BATCH_COMMAND_LIMIT_EXCEEDED";
  case SimulationValidationCode::kInputBatchCommandKindNotAccepted:
    return "SIMULATION.INPUT_BATCH_COMMAND_KIND_NOT_ACCEPTED";
  case SimulationValidationCode::kInputBatchThrustDirectionOutOfRange:
    return "SIMULATION.INPUT_BATCH_THRUST_DIRECTION_OUT_OF_RANGE";
  case SimulationValidationCode::kInputBatchSpawnAndDespawnConflict:
    return "SIMULATION.INPUT_BATCH_SPAWN_AND_DESPAWN_CONFLICT";
  case SimulationValidationCode::kGameWorldEntityLimitExceeded:
    return "SIMULATION.GAME_WORLD_ENTITY_LIMIT_EXCEEDED";
  case SimulationValidationCode::kGameWorldDuplicateEntityId:
    return "SIMULATION.GAME_WORLD_DUPLICATE_ENTITY_ID";
  case SimulationValidationCode::kGameWorldEventLimitExceeded:
    return "SIMULATION.GAME_WORLD_EVENT_LIMIT_EXCEEDED";
  case SimulationValidationCode::kDeterministicRandomBoundEmpty:
    return "SIMULATION.DETERMINISTIC_RANDOM_BOUND_EMPTY";
  case SimulationValidationCode::kSpawnPolicyIndexOutOfRange:
    return "SIMULATION.SPAWN_POLICY_INDEX_OUT_OF_RANGE";
  case SimulationValidationCode::kSpawnPolicyPointOccupied:
    return "SIMULATION.SPAWN_POLICY_POINT_OCCUPIED";
  case SimulationValidationCode::kMapSpawnPointNotSeatable:
    return "SIMULATION.MAP_SPAWN_POINT_NOT_SEATABLE";
  case SimulationValidationCode::kGameSimulationSetupConflict:
    return "SIMULATION.GAME_SIMULATION_SETUP_CONFLICT";
  case SimulationValidationCode::kSystemPipelineSystemMissing:
    return "SIMULATION.SYSTEM_PIPELINE_SYSTEM_MISSING";
  case SimulationValidationCode::kSystemPipelineSystemNameEmpty:
    return "SIMULATION.SYSTEM_PIPELINE_SYSTEM_NAME_EMPTY";
  case SimulationValidationCode::kSystemPipelineDuplicateSystemName:
    return "SIMULATION.SYSTEM_PIPELINE_DUPLICATE_SYSTEM_NAME";
  case SimulationValidationCode::kSystemPipelineStageUnknown:
    return "SIMULATION.SYSTEM_PIPELINE_STAGE_UNKNOWN";
  case SimulationValidationCode::kContactResponseBodiesAbsent:
    return "SIMULATION.CONTACT_RESPONSE_BODIES_ABSENT";
  case SimulationValidationCode::kContactRuleNameInvalid:
    return "SIMULATION.CONTACT_RULE_NAME_INVALID";
  case SimulationValidationCode::kContactRulePredicateMissing:
    return "SIMULATION.CONTACT_RULE_PREDICATE_MISSING";
  case SimulationValidationCode::kContactRuleResponseMissing:
    return "SIMULATION.CONTACT_RULE_RESPONSE_MISSING";
  case SimulationValidationCode::kContactRuleTableDuplicateRuleName:
    return "SIMULATION.CONTACT_RULE_TABLE_DUPLICATE_RULE_NAME";
  case SimulationValidationCode::kArenaBoundsScalarNotFinite:
    return "SIMULATION.ARENA_BOUNDS_SCALAR_NOT_FINITE";
  case SimulationValidationCode::kArenaBoundsScalarOutOfRange:
    return "SIMULATION.ARENA_BOUNDS_SCALAR_OUT_OF_RANGE";
  case SimulationValidationCode::kMapNameInvalid:
    return "SIMULATION.MAP_NAME_INVALID";
  case SimulationValidationCode::kMapStaticBodyNotStatic:
    return "SIMULATION.MAP_STATIC_BODY_NOT_STATIC";
  case SimulationValidationCode::kMapStaticBodyOutOfBounds:
    return "SIMULATION.MAP_STATIC_BODY_OUT_OF_BOUNDS";
  case SimulationValidationCode::kMapStaticBodyLimitExceeded:
    return "SIMULATION.MAP_STATIC_BODY_LIMIT_EXCEEDED";
  case SimulationValidationCode::kMapMarkerKindInvalid:
    return "SIMULATION.MAP_MARKER_KIND_INVALID";
  case SimulationValidationCode::kMapMarkerOutOfBounds:
    return "SIMULATION.MAP_MARKER_OUT_OF_BOUNDS";
  case SimulationValidationCode::kMapMarkerLimitExceeded:
    return "SIMULATION.MAP_MARKER_LIMIT_EXCEEDED";
  case SimulationValidationCode::kMapMetadataKeyInvalid:
    return "SIMULATION.MAP_METADATA_KEY_INVALID";
  case SimulationValidationCode::kMapMetadataValueInvalid:
    return "SIMULATION.MAP_METADATA_VALUE_INVALID";
  case SimulationValidationCode::kMapMetadataDuplicateKey:
    return "SIMULATION.MAP_METADATA_DUPLICATE_KEY";
  case SimulationValidationCode::kMapMetadataLimitExceeded:
    return "SIMULATION.MAP_METADATA_LIMIT_EXCEEDED";
  case SimulationValidationCode::kGameSimulationBodyOutOfBounds:
    return "SIMULATION.GAME_SIMULATION_BODY_OUT_OF_BOUNDS";
  case SimulationValidationCode::kGameSimulationSpatialIndexUnknownEntityId:
    return "SIMULATION.GAME_SIMULATION_SPATIAL_INDEX_UNKNOWN_ENTITY_ID";
  case SimulationValidationCode::kGameSimulationSpatialIndexStale:
    return "SIMULATION.GAME_SIMULATION_SPATIAL_INDEX_STALE";
  case SimulationValidationCode::kGameSimulationWallMotionIncoherent:
    return "SIMULATION.GAME_SIMULATION_WALL_MOTION_INCOHERENT";
  case SimulationValidationCode::kTickSequenceOutOfRange:
    return "SIMULATION.TICK_SEQUENCE_OUT_OF_RANGE";
  case SimulationValidationCode::kConfigWorldScalarNotFinite:
    return "SIMULATION.CONFIG.WORLD_SCALAR_NOT_FINITE";
  case SimulationValidationCode::kConfigWorldScalarOutOfRange:
    return "SIMULATION.CONFIG.WORLD_SCALAR_OUT_OF_RANGE";
  case SimulationValidationCode::kConfigWorldTooSmallForPlayer:
    return "SIMULATION.CONFIG.WORLD_TOO_SMALL_FOR_PLAYER";
  case SimulationValidationCode::kConfigTickRateUnsupported:
    return "SIMULATION.CONFIG.TICK_RATE_UNSUPPORTED";
  case SimulationValidationCode::kConfigDragNotFinite:
    return "SIMULATION.CONFIG.DRAG_NOT_FINITE";
  case SimulationValidationCode::kConfigDragOutOfRange:
    return "SIMULATION.CONFIG.DRAG_OUT_OF_RANGE";
  case SimulationValidationCode::kConfigSpatialGridDimensionOutOfRange:
    return "SIMULATION.CONFIG.SPATIAL_GRID_DIMENSION_OUT_OF_RANGE";
  case SimulationValidationCode::kConfigSpatialGridCellLimitExceeded:
    return "SIMULATION.CONFIG.SPATIAL_GRID_CELL_LIMIT_EXCEEDED";
  case SimulationValidationCode::kCandidatePairDuplicateEntityId:
    return "SIMULATION.CANDIDATE_PAIR_DUPLICATE_ENTITY_ID";
  case SimulationValidationCode::kSpatialGridGeometryInvalid:
    return "SIMULATION.SPATIAL_GRID_GEOMETRY_INVALID";
  case SimulationValidationCode::kSpatialGridCellCountExceeded:
    return "SIMULATION.SPATIAL_GRID_CELL_COUNT_EXCEEDED";
  case SimulationValidationCode::kSpatialGridMembershipLimitExceeded:
    return "SIMULATION.SPATIAL_GRID_MEMBERSHIP_LIMIT_EXCEEDED";
  case SimulationValidationCode::kSpatialGridCandidatePairLimitExceeded:
    return "SIMULATION.SPATIAL_GRID_CANDIDATE_PAIR_LIMIT_EXCEEDED";
  case SimulationValidationCode::kSpatialGridPlayerCenterOutOfBounds:
    return "SIMULATION.SPATIAL_GRID_PLAYER_CENTER_OUT_OF_BOUNDS";
  case SimulationValidationCode::kSpatialGridStaticBodyOutOfBounds:
    return "SIMULATION.SPATIAL_GRID_STATIC_BODY_OUT_OF_BOUNDS";
  case SimulationValidationCode::kSpatialGridPointOutOfBounds:
    return "SIMULATION.SPATIAL_GRID_POINT_OUT_OF_BOUNDS";
  case SimulationValidationCode::kSpatialGridCellCoordinateOutOfBounds:
    return "SIMULATION.SPATIAL_GRID_CELL_COORDINATE_OUT_OF_BOUNDS";
  }
  return "SIMULATION.VALIDATION_CODE_INVALID";
}

class SimulationValidationError final : public std::invalid_argument {
public:
  SimulationValidationError(const SimulationValidationCode validation_code, std::string context,
                            std::string detail)
      : std::invalid_argument(build_message(validation_code, context, detail)),
        validation_code_(validation_code), context_(std::move(context)),
        detail_(std::move(detail)) {}

  [[nodiscard]] SimulationValidationCode validation_code() const noexcept {
    return validation_code_;
  }

  [[nodiscard]] std::string_view code() const noexcept {
    return simulation_validation_code_name(validation_code_);
  }

  [[nodiscard]] const std::string& context() const& noexcept { return context_; }
  [[nodiscard]] const std::string& context() const&& = delete;
  [[nodiscard]] const std::string& detail() const& noexcept { return detail_; }
  [[nodiscard]] const std::string& detail() const&& = delete;

private:
  [[nodiscard]] static std::string build_message(const SimulationValidationCode validation_code,
                                                 const std::string_view context,
                                                 const std::string_view detail) {
    std::string message;
    message.reserve(simulation_validation_code_name(validation_code).size() + context.size() +
                    detail.size() + 4);
    message.append(simulation_validation_code_name(validation_code));
    message.append(" [");
    message.append(context);
    message.append("]: ");
    message.append(detail);
    return message;
  }

  SimulationValidationCode validation_code_;
  std::string context_;
  std::string detail_;
};

} // namespace blob_royale::simulation

#endif
