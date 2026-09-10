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
// **Nothing here is thrown during a decision pass by production code.** Every code below names a
// *construction* or *composition* defect -- an unknown bot kind in a roster, two controllers
// claiming one durable identity, a personality outside its accepted range, an observation built for
// a different controller. A controller that misbehaves at decision time is isolated and counted by
// `ControllerHost`, not thrown out of, because a bot holds exactly the capabilities a network
// session holds and neither may stop the match (`controller_host.hpp`).
// related: controller_registry.hpp -- the unknown-kind rejection.
// related: controller_host.hpp -- the composition rejections and the failures it counts instead.
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
  kScriptedReplayLogLimitExceeded,
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
  case ControllersValidationCode::kScriptedReplayLogLimitExceeded:
    return "CONTROLLERS.SCRIPTED_REPLAY_LOG_LIMIT_EXCEEDED";
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
