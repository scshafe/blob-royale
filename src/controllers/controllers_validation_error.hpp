#ifndef BLOB_ROYALE_CONTROLLERS_CONTROLLERS_VALIDATION_ERROR_HPP
#define BLOB_ROYALE_CONTROLLERS_CONTROLLERS_VALIDATION_ERROR_HPP

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::controllers {

// canonical: controllers_validation_error -- the one exception vocabulary of blob_controllers.
//
// One typed, coded validation error per domain library is this tree's convention:
// `SimulationValidationError` for `blob_simulation`, `GameplayValidationError` for `blob_gameplay`,
// `CommandSinkError` for `blob_runtime`, `ProtocolEncodingError` for `blob_protocol`. A controller
// is a decision maker in `blob_controllers`, so its rejections carry `CONTROLLERS.*` codes and
// never borrow a `SIMULATION.*` or `RUNTIME.*` one: those are the vocabularies of the libraries
// below, and a controller adding to one would be the dependency running backwards
// (`docs/architecture/0002-simulation-architecture.md` § "Decision").
//
// It derives from `std::invalid_argument` exactly as the other two do, so a composition root that
// only needs "the declaration was rejected" catches one type across all three while a caller that
// has to distinguish reads `code()`.
//
// Most codes below name a *construction* or *composition* defect -- an unknown bot kind in a
// roster, two controllers
// claiming one durable identity, a personality outside its accepted range, an observation built for
// a different controller. `RACER_COURSE_INVALID` additionally reports a malformed published course
// during a decision; accepted race maps cannot produce it.
// `TACTICAL_PROFILE_OBJECTIVE_WEIGHTS_DEGENERATE` is the one code here that rejects a section whose
// every value is individually in range: it names a *combination* that cannot express a preference,
// and `tactical_profile.cpp` states why an authored one is always an omission.
// `TACTICAL_PROFILE_ROAD_CAUTION_INVALID` is the one tactical-profile code with a half-open domain:
// its zero *inverts* the race recovery test rather than disabling it, so that key adopts the
// `(0,1]` `RACER_CAUTION_FRACTION_OUT_OF_RANGE` already enforces two files over, and this code is
// what a positional `Section` site that value-initialized the key to zero hits.
// A decision failure is isolated and
// counted by `ControllerHost`, not thrown out of, because a bot holds exactly the capabilities a
// network session holds and neither may stop the match (`controller_host.hpp`). related:
// controller_registry.hpp -- the unknown-kind rejection. related: controller_host.hpp -- the
// composition rejections and the failures it counts instead.
enum class ControllersValidationCode {
  kControllerKindUnknown,
  kControllerAbsent,
  kControllerIdDuplicate,
  kControllerHostFull,
  kObservationSnapshotAbsent,
  kObservationControllerMismatch,
  kWandererReactionDelayOutOfRange,
  kChaserAggressionWeightNotFinite,
  kChaserAggressionWeightOutOfRange,
  kHillSeekerWeightNotFinite,
  kHillSeekerWeightOutOfRange,
  kRacerCautionFractionNotFinite,
  kRacerCautionFractionOutOfRange,
  kRacerCourseInvalid,
  kScriptedReplayLogLimitExceeded,
  kTacticalProfileNameInvalid,
  kTacticalProfileProbabilityInvalid,
  kTacticalProfileReactionDelayInvalid,
  kTacticalProfileAimErrorInvalid,
  kTacticalProfilePersistenceInvalid,
  kTacticalProfileObjectiveWeightInvalid,
  kTacticalProfileObjectiveWeightsDegenerate,
  kTacticalProfileRiskToleranceInvalid,
  kTacticalProfilePredictionHorizonInvalid,
  kTacticalProfileChargeScreenInvalid,
  kTacticalProfileShieldAnticipationInvalid,
  kTacticalProfileRoadCautionInvalid,
  kTacticalProfileArrivalBrakeInvalid,
  kTacticalProfileExposurePreferenceInvalid,
  kTacticalProfileMinimumOpeningInvalid,
  kTacticalProfileCatalogueFull,
  kTacticalProfileNameDuplicate,
  kControllerCreationContextInvalid,
  kTacticalSeedIdentityInvalid,
  kTacticalModeUnsupported,
  kTacticalCandidateLimitExceeded,
  kTacticalObjectiveInvalid,
  kTacticalRunningTickInvalid,
};

[[nodiscard]] constexpr std::string_view
controllers_validation_code_name(const ControllersValidationCode code) noexcept {
  switch (code) {
  case ControllersValidationCode::kControllerKindUnknown:
    return "CONTROLLERS.CONTROLLER_KIND_UNKNOWN";
  case ControllersValidationCode::kControllerAbsent:
    return "CONTROLLERS.CONTROLLER_ABSENT";
  case ControllersValidationCode::kControllerIdDuplicate:
    return "CONTROLLERS.CONTROLLER_ID_DUPLICATE";
  case ControllersValidationCode::kControllerHostFull:
    return "CONTROLLERS.CONTROLLER_HOST_FULL";
  case ControllersValidationCode::kObservationSnapshotAbsent:
    return "CONTROLLERS.OBSERVATION_SNAPSHOT_ABSENT";
  case ControllersValidationCode::kObservationControllerMismatch:
    return "CONTROLLERS.OBSERVATION_CONTROLLER_MISMATCH";
  case ControllersValidationCode::kWandererReactionDelayOutOfRange:
    return "CONTROLLERS.WANDERER_REACTION_DELAY_OUT_OF_RANGE";
  case ControllersValidationCode::kChaserAggressionWeightNotFinite:
    return "CONTROLLERS.CHASER_AGGRESSION_WEIGHT_NOT_FINITE";
  case ControllersValidationCode::kChaserAggressionWeightOutOfRange:
    return "CONTROLLERS.CHASER_AGGRESSION_WEIGHT_OUT_OF_RANGE";
  case ControllersValidationCode::kHillSeekerWeightNotFinite:
    return "CONTROLLERS.HILL_SEEKER_WEIGHT_NOT_FINITE";
  case ControllersValidationCode::kHillSeekerWeightOutOfRange:
    return "CONTROLLERS.HILL_SEEKER_WEIGHT_OUT_OF_RANGE";
  case ControllersValidationCode::kRacerCautionFractionNotFinite:
    return "CONTROLLERS.RACER_CAUTION_FRACTION_NOT_FINITE";
  case ControllersValidationCode::kRacerCautionFractionOutOfRange:
    return "CONTROLLERS.RACER_CAUTION_FRACTION_OUT_OF_RANGE";
  case ControllersValidationCode::kRacerCourseInvalid:
    return "CONTROLLERS.RACER_COURSE_INVALID";
  case ControllersValidationCode::kScriptedReplayLogLimitExceeded:
    return "CONTROLLERS.SCRIPTED_REPLAY_LOG_LIMIT_EXCEEDED";
  case ControllersValidationCode::kTacticalProfileNameInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_NAME_INVALID";
  case ControllersValidationCode::kTacticalProfileProbabilityInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_PROBABILITY_INVALID";
  case ControllersValidationCode::kTacticalProfileReactionDelayInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_REACTION_DELAY_INVALID";
  case ControllersValidationCode::kTacticalProfileAimErrorInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_AIM_ERROR_INVALID";
  case ControllersValidationCode::kTacticalProfilePersistenceInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_PERSISTENCE_INVALID";
  case ControllersValidationCode::kTacticalProfileObjectiveWeightInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_OBJECTIVE_WEIGHT_INVALID";
  case ControllersValidationCode::kTacticalProfileObjectiveWeightsDegenerate:
    return "CONTROLLERS.TACTICAL_PROFILE_OBJECTIVE_WEIGHTS_DEGENERATE";
  case ControllersValidationCode::kTacticalProfileRiskToleranceInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_RISK_TOLERANCE_INVALID";
  case ControllersValidationCode::kTacticalProfilePredictionHorizonInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_PREDICTION_HORIZON_INVALID";
  case ControllersValidationCode::kTacticalProfileChargeScreenInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_CHARGE_SCREEN_INVALID";
  case ControllersValidationCode::kTacticalProfileShieldAnticipationInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_SHIELD_ANTICIPATION_INVALID";
  case ControllersValidationCode::kTacticalProfileRoadCautionInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_ROAD_CAUTION_INVALID";
  case ControllersValidationCode::kTacticalProfileArrivalBrakeInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_ARRIVAL_BRAKE_INVALID";
  case ControllersValidationCode::kTacticalProfileExposurePreferenceInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_EXPOSURE_PREFERENCE_INVALID";
  case ControllersValidationCode::kTacticalProfileMinimumOpeningInvalid:
    return "CONTROLLERS.TACTICAL_PROFILE_MINIMUM_OPENING_INVALID";
  case ControllersValidationCode::kTacticalProfileCatalogueFull:
    return "CONTROLLERS.TACTICAL_PROFILE_CATALOGUE_FULL";
  case ControllersValidationCode::kTacticalProfileNameDuplicate:
    return "CONTROLLERS.TACTICAL_PROFILE_NAME_DUPLICATE";
  case ControllersValidationCode::kControllerCreationContextInvalid:
    return "CONTROLLERS.CONTROLLER_CREATION_CONTEXT_INVALID";
  case ControllersValidationCode::kTacticalSeedIdentityInvalid:
    return "CONTROLLERS.TACTICAL_SEED_IDENTITY_INVALID";
  case ControllersValidationCode::kTacticalModeUnsupported:
    return "CONTROLLERS.TACTICAL_MODE_UNSUPPORTED";
  case ControllersValidationCode::kTacticalCandidateLimitExceeded:
    return "CONTROLLERS.TACTICAL_CANDIDATE_LIMIT_EXCEEDED";
  case ControllersValidationCode::kTacticalObjectiveInvalid:
    return "CONTROLLERS.TACTICAL_OBJECTIVE_INVALID";
  case ControllersValidationCode::kTacticalRunningTickInvalid:
    return "CONTROLLERS.TACTICAL_RUNNING_TICK_INVALID";
  }
  return "CONTROLLERS.VALIDATION_CODE_INVALID";
}

class ControllersValidationError final : public std::invalid_argument {
public:
  ControllersValidationError(const ControllersValidationCode validation_code, std::string context,
                             std::string detail)
      : std::invalid_argument(build_message(validation_code, context, detail)),
        validation_code_(validation_code), context_(std::move(context)),
        detail_(std::move(detail)) {}

  [[nodiscard]] ControllersValidationCode validation_code() const noexcept {
    return validation_code_;
  }

  [[nodiscard]] std::string_view code() const noexcept {
    return controllers_validation_code_name(validation_code_);
  }

  [[nodiscard]] const std::string& context() const& noexcept { return context_; }
  [[nodiscard]] const std::string& context() const&& = delete;
  [[nodiscard]] const std::string& detail() const& noexcept { return detail_; }
  [[nodiscard]] const std::string& detail() const&& = delete;

private:
  [[nodiscard]] static std::string build_message(const ControllersValidationCode validation_code,
                                                 const std::string_view context,
                                                 const std::string_view detail) {
    std::string message;
    message.reserve(controllers_validation_code_name(validation_code).size() + context.size() +
                    detail.size() + 4);
    message.append(controllers_validation_code_name(validation_code));
    message.append(" [");
    message.append(context);
    message.append("]: ");
    message.append(detail);
    return message;
  }

  ControllersValidationCode validation_code_;
  std::string context_;
  std::string detail_;
};

} // namespace blob_royale::controllers

#endif
