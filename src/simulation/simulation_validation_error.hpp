#ifndef BLOB_ROYALE_SIMULATION_SIMULATION_VALIDATION_ERROR_HPP
#define BLOB_ROYALE_SIMULATION_SIMULATION_VALIDATION_ERROR_HPP

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::simulation {

enum class SimulationValidationCode {
  kPhysicalScalarNotFinite,
  kPhysicalScalarOutOfRange,
  kVectorDivisionByZero,
  kEntityIdOutOfRange,
  kControllerIdOutOfRange,
  kTeamIdOutOfRange,
  kComponentStoreDuplicateEntityId,
  kComponentStoreLimitExceeded,
  kGameWorldPlayerLimitExceeded,
  kGameWorldDuplicateEntityId,
  kTickSequenceOutOfRange,
  kConfigWorldScalarNotFinite,
  kConfigWorldScalarOutOfRange,
  kConfigWorldTooSmallForPlayer,
  kConfigTickRateUnsupported,
  kConfigSpatialGridDimensionOutOfRange,
  kConfigSpatialGridCellLimitExceeded,
  kCandidatePairDuplicateEntityId,
  kSpatialGridGeometryInvalid,
  kSpatialGridCellCountExceeded,
  kSpatialGridMembershipLimitExceeded,
  kSpatialGridCandidatePairLimitExceeded,
  kSpatialGridPlayerCenterOutOfBounds,
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
  case SimulationValidationCode::kGameWorldPlayerLimitExceeded:
    return "SIMULATION.GAME_WORLD_PLAYER_LIMIT_EXCEEDED";
  case SimulationValidationCode::kGameWorldDuplicateEntityId:
    return "SIMULATION.GAME_WORLD_DUPLICATE_ENTITY_ID";
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
