#ifndef BLOB_ROYALE_CONTROLLERS_SCRIPTED_REPLAY_CONTROLLER_HPP
#define BLOB_ROYALE_CONTROLLERS_SCRIPTED_REPLAY_CONTROLLER_HPP

#include "command_registry.hpp"
#include "controller.hpp"
#include "controller_id.hpp"
#include "observation.hpp"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace blob_royale::controllers {

// canonical: scripted_replay_controller -- drives a recorded command log, one step per pass.
// @extension-point controller
//
// **The third real implementation of the controller role**, which is the evidence
// `docs/architecture/0002-simulation-architecture.md` § "OOP policy" accepts the interface on: a
// networked session, an in-process bot, and this. ADR 0004 § "Controllers" names it as the
// controller fixtures use.
//
// **It is deterministic by construction rather than by discipline.** Its output is a function of
// its own cursor and its log and of nothing else -- it does not read the observation at all, which
// is why `decide_from_observation` names its parameter and ignores it. That is exactly what a
// fixture wants: a command source whose behavior is stated in the test rather than inferred from
// the world the test is trying to assert about.
//
// **One step per decision pass, in order, and nothing after the log runs out.** The log is a list
// of steps and step `k` is what the controller returns from its `k`-th pass; an exhausted
// controller returns an empty list forever, which is a controller that has finished rather than one
// that has failed. Cadence is the host's business: the controller counts passes, not ticks, so the
// same log drives the same decisions whether the host runs at 20 Hz or is stepped once per test
// assertion.
//
// **The log is literal.** A step carries whole `Command` values, entity ids included, exactly as
// they were recorded. Late-binding a thrust to "whatever body I am driving now" would make the
// replay a function of the world again and would cost this controller the one property it exists to
// have; a fixture that needs an id the engine chose reads it from the reservation policy
// (`tests/fixtures/replay_fixture.hpp`) or from a snapshot.
//
// **It is deliberately not in `controller_registry.hpp`.** A registered kind is one `[match] bots=`
// may name, and a scripted controller is meaningless without the log that no configuration line
// carries; registering it would make `bots=scripted_replay:1` produce a controller that silently
// decides nothing, which is the degraded success this codebase refuses. Fixtures construct it
// directly.
// related: controller_registry.hpp -- the two kinds a roster may name, and why this is not one.
// related: ../../tests/fixtures/replay_fixture.hpp -- the recorded `(map, mode configuration, seed,
// command log)` fixture format this is the live-host counterpart of.
class ScriptedReplayController final : public Controller {
public:
  static constexpr std::string_view kControllerKind = "scripted_replay";

  // One decision pass of the recorded log. An empty step is a pass that decides nothing, which is
  // how a log expresses waiting.
  using Step = std::vector<simulation::Command>;

  // Throws ControllersValidationError with `CONTROLLERS.SCRIPTED_REPLAY_LOG_LIMIT_EXCEEDED` above
  // `kMaximumScriptedReplayStepCount`.
  [[nodiscard]] static std::unique_ptr<Controller> create(simulation::ControllerId controller,
                                                          std::vector<Step> log);

  // Public because `create` hands the controller over as a `std::unique_ptr<Controller>` and
  // `std::make_unique` needs an accessible constructor. **`create` is the validating entry point**;
  // this one takes the log as given.
  ScriptedReplayController(simulation::ControllerId controller, std::vector<Step> log) noexcept;

  [[nodiscard]] std::string_view kind() const noexcept override { return kControllerKind; }

  // Steps the recorded log holds.
  [[nodiscard]] std::size_t step_count() const noexcept { return log_.size(); }

  // Steps already driven. It is also the index of the step the next pass will drive.
  [[nodiscard]] std::size_t completed_step_count() const noexcept { return cursor_; }

  // Whether every recorded step has been driven, so every later pass decides nothing.
  [[nodiscard]] bool is_exhausted() const noexcept { return cursor_ >= log_.size(); }

private:
  [[nodiscard]] std::vector<simulation::Command>
  decide_from_observation(const Observation& observation) override;

  std::vector<Step> log_;
  std::size_t cursor_{0};
};

} // namespace blob_royale::controllers

#endif
