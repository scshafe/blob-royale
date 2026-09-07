#include "controller_host.hpp"

#include "command_submission_result.hpp"
#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "observation.hpp"
#include "world_snapshot.hpp"

#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::controllers {

ControllerHost::ControllerHost(const runtime::SnapshotPublication& publication,
                               runtime::CommandSink& sink) noexcept
    : publication_(&publication), sink_(&sink) {}

void ControllerHost::add(std::unique_ptr<Controller> controller) {
  if (controller == nullptr) {
    throw ControllersValidationError(ControllersValidationCode::kControllerAbsent,
                                     "controller_host.add.controller",
                                     "a host cannot run an absent controller");
  }
  if (controllers_.size() >= kMaximumHostedControllerCount) {
    throw ControllersValidationError(
        ControllersValidationCode::kControllerHostFull, "controller_host.add.controllers",
        "the host already runs the accepted maximum of " +
            std::to_string(kMaximumHostedControllerCount) + " controllers");
  }

  const simulation::ControllerId identity = controller->controller();
  const std::size_t position = ascending_position_of(identity);
  if (position < controllers_.size() && controllers_[position]->controller() == identity) {
    throw ControllersValidationError(
        ControllersValidationCode::kControllerIdDuplicate, "controller_host.add.controller_id",
        "ControllerId " + std::to_string(identity.value()) + " is already claimed by hosted " +
            std::string(controllers_[position]->kind()) +
            "; CommandSink::open_session issues each identity exactly once");
  }
  controllers_.insert(controllers_.begin() + static_cast<std::ptrdiff_t>(position),
                      std::move(controller));
}

bool ControllerHost::contains(const simulation::ControllerId controller) const noexcept {
  const std::size_t position = ascending_position_of(controller);
  return position < controllers_.size() && controllers_[position]->controller() == controller;
}

ControllerHostPass ControllerHost::decide_once() {
  // The one acquisition of the pass. Every controller below observes this exact retained value, so
  // a tick published mid-pass cannot split one roster across two worlds.
  const std::shared_ptr<const simulation::WorldSnapshot> snapshot = publication_->latest();
  ControllerHostPass pass{.observed_tick_sequence = snapshot->tick_sequence(),
                          .deciding_controller_count = 0,
                          .decided_command_count = 0,
                          .accepted_command_count = 0,
                          .refused_command_count = 0,
                          .failed_controller_count = 0};

  for (const std::unique_ptr<Controller>& controller : controllers_) {
    const Observation observation = Observation::create(snapshot, controller->controller());

    std::vector<simulation::Command> decided;
    try {
      decided = controller->decide(observation);
    } catch (const std::exception& failure) {
      ++pass.failed_controller_count;
      record_failure(*controller, failure.what(), pass.observed_tick_sequence);
      continue;
    } catch (...) {
      // A throw that is not a `std::exception` carries nothing readable, so the message says that
      // rather than pretending to quote one.
      ++pass.failed_controller_count;
      record_failure(*controller, "a non-standard exception left decide",
                     pass.observed_tick_sequence);
      continue;
    }

    ++pass.deciding_controller_count;
    pass.decided_command_count += decided.size();
    for (const simulation::Command& command : decided) {
      // Stamped with the deciding controller's own identity, which is what makes a bot's command
      // indistinguishable from a session's and keeps a controller from commanding a foreign
      // identity: the sink refuses a spawn carrying anyone else's `ControllerId`.
      const runtime::CommandSubmissionResult result =
          sink_->submit(controller->controller(), command);
      if (runtime::command_submission_accepted(result)) {
        ++pass.accepted_command_count;
      } else {
        ++pass.refused_command_count;
      }
    }
  }

  ++statistics_.pass_count;
  statistics_.decided_command_count += pass.decided_command_count;
  statistics_.accepted_command_count += pass.accepted_command_count;
  statistics_.refused_command_count += pass.refused_command_count;
  statistics_.failed_controller_count += pass.failed_controller_count;
  return pass;
}

std::size_t
ControllerHost::ascending_position_of(const simulation::ControllerId controller) const noexcept {
  // Linear over a roster bounded by `kMaximumHostedControllerCount` and walked only when a
  // controller is added or looked up by identity, never inside a decision pass, so a second index
  // to keep in agreement with the vector would buy nothing.
  std::size_t position = 0;
  while (position < controllers_.size() && controllers_[position]->controller() < controller) {
    ++position;
  }
  return position;
}

void ControllerHost::record_failure(const Controller& controller, std::string message,
                                    const simulation::TickSequence observed_tick_sequence) {
  last_failure_ = ControllerFailure{.controller = controller.controller(),
                                    .controller_kind = std::string(controller.kind()),
                                    .message = std::move(message),
                                    .observed_tick_sequence = observed_tick_sequence};
}

} // namespace blob_royale::controllers
