#include "scripted_replay_controller.hpp"

#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::controllers {

std::unique_ptr<Controller>
ScriptedReplayController::create(const simulation::ControllerId controller, std::vector<Step> log) {
  if (log.size() > kMaximumScriptedReplayStepCount) {
    throw ControllersValidationError(ControllersValidationCode::kScriptedReplayLogLimitExceeded,
                                     "scripted_replay_controller.log",
                                     "recorded log of " + std::to_string(log.size()) +
                                         " steps exceeds the accepted maximum " +
                                         std::to_string(kMaximumScriptedReplayStepCount));
  }
  return std::make_unique<ScriptedReplayController>(controller, std::move(log));
}

ScriptedReplayController::ScriptedReplayController(const simulation::ControllerId controller,
                                                   std::vector<Step> log) noexcept
    : Controller(controller), log_(std::move(log)) {}

std::vector<simulation::Command>
ScriptedReplayController::decide_from_observation(const Observation& /*observation*/) {
  // The observation is deliberately unread: a scripted replay's whole value is that its output is a
  // function of its log and its cursor alone. See the class comment.
  if (cursor_ >= log_.size()) {
    return {};
  }
  const std::size_t step = cursor_;
  ++cursor_;
  return log_[step];
}

} // namespace blob_royale::controllers
