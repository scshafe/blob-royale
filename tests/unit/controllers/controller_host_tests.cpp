#include "controller_host.hpp"

#include "command_registry.hpp"
#include "command_sink.hpp"
#include "commands/thrust_command.hpp"
#include "controller.hpp"
#include "controller_id.hpp"
#include "controllers_limits.hpp"
#include "controllers_test_fixture.hpp"
#include "controllers_validation_error.hpp"
#include "entity_id.hpp"
#include "game_simulation.hpp"
#include "game_world.hpp"
#include "observation.hpp"
#include "player_snapshot.hpp"
#include "sandbox/sandbox_mode.hpp"
#include "simulation_limits.hpp"
#include "simulation_runtime.hpp"
#include "snapshot_publication.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"
#include "wanderer_controller.hpp"
#include "world_snapshot.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace controllers = blob_royale::controllers;
namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;
namespace testing = blob_royale::testing;

namespace {

constexpr std::uint64_t kFirstController = simulation::kMinimumControllerId;
constexpr std::uint64_t kSecondController = simulation::kMinimumControllerId + 1;
constexpr std::uint64_t kThirdController = simulation::kMinimumControllerId + 2;
constexpr std::chrono::seconds kPublicationDeadline{5};
constexpr std::chrono::seconds kMovementDeadline{10};

// **The host's construction surface is the whole capability argument, so it is asserted at compile
// time.** A `ControllerHost` that could be built from the simulation, the world, or the runtime
// would be a bot holding something no network session holds, and no runtime test could rule that
// out as completely as the type system does.
static_assert(std::is_constructible_v<controllers::ControllerHost,
                                      const runtime::SnapshotPublication&, runtime::CommandSink&>,
              "a host is built from exactly the two capabilities a network session holds");
static_assert(!std::is_constructible_v<controllers::ControllerHost, simulation::GameSimulation&>,
              "a host must not be constructible from the simulation");
static_assert(!std::is_constructible_v<controllers::ControllerHost, simulation::GameWorld&>,
              "a host must not be constructible from the world");
static_assert(!std::is_constructible_v<controllers::ControllerHost, runtime::SimulationRuntime&>,
              "a host must not be constructible from the runtime");
static_assert(!std::is_constructible_v<controllers::ControllerHost, runtime::SimulationRuntime&,
                                       runtime::CommandSink&>,
              "no overload admits the runtime beside the sink");

// What one `RecordingController` should do, so a host test states the behavior it needs rather than
// growing a fourth positional constructor argument.
struct RecordingOptions final {
  // Returned verbatim from every `decide`.
  std::vector<simulation::Command> emitted{};
  // Appended to with the deciding controller's id, so a test observes pass order directly.
  std::vector<std::uint64_t>* order{nullptr};
  // Throws instead of returning, to exercise the host's isolation.
  bool throws{false};
  // Blocks inside `decide` until this publication moves past the observed tick. It is how the
  // "one acquisition per pass" test forces a real publish to land mid-pass.
  const runtime::SnapshotPublication* advance_barrier{nullptr};
};

// A controller that records what it observed and returns commands a test named. Everything a host
// test asserts about ordering, isolation, and refusal is observable through this one value.
class RecordingController final : public controllers::Controller {
public:
  RecordingController(const simulation::ControllerId controller, RecordingOptions options)
      : controllers::Controller(controller), options_(std::move(options)) {}

  [[nodiscard]] std::string_view kind() const noexcept override { return "recording"; }

  [[nodiscard]] const std::optional<simulation::TickSequence>&
  observed_tick_sequence() const noexcept {
    return observed_tick_sequence_;
  }
  // Whether the barrier publication actually moved past the observed tick while this controller was
  // inside `decide`.
  [[nodiscard]] bool observed_publication_advance() const noexcept {
    return observed_publication_advance_;
  }

private:
  [[nodiscard]] std::vector<simulation::Command>
  decide_from_observation(const controllers::Observation& observation) override {
    observed_tick_sequence_ = observation.tick_sequence();
    if (options_.order != nullptr) {
      options_.order->push_back(observation.controller().value());
    }
    if (options_.advance_barrier != nullptr) {
      const std::chrono::steady_clock::time_point deadline =
          std::chrono::steady_clock::now() + kPublicationDeadline;
      while (std::chrono::steady_clock::now() < deadline) {
        if (options_.advance_barrier->latest()->tick_sequence() > observation.tick_sequence()) {
          observed_publication_advance_ = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
    if (options_.throws) {
      throw std::runtime_error("recording controller failed on purpose");
    }
    return options_.emitted;
  }

  RecordingOptions options_;
  std::optional<simulation::TickSequence> observed_tick_sequence_;
  bool observed_publication_advance_{false};
};

[[nodiscard]] std::unique_ptr<RecordingController> recording(const std::uint64_t controller,
                                                             RecordingOptions options) {
  return std::make_unique<RecordingController>(simulation::ControllerId::create(controller),
                                               std::move(options));
}

[[nodiscard]] simulation::Command thrust_for(const simulation::EntityId entity, const double x,
                                             const double y) {
  return simulation::Command{
      simulation::ThrustCommand{.entity = entity, .direction = simulation::Vector2::create(x, y)}};
}

// A sandbox simulation on the four-marker fixture map, ready to be handed to a runtime.
[[nodiscard]] simulation::GameSimulation live_sandbox() {
  return testing::gameplay_simulation(blob_royale::gameplay::SandboxMode::create(),
                                      testing::controllers_map_of(4));
}

} // namespace

TEST_CASE("ControllerHost files controllers at their ascending ControllerId position",
          "[unit][controllers][controller_host]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 3);
  std::vector<std::uint64_t> order;

  // Added in descending order on purpose: the pass order must be the identity order, not the
  // insertion order.
  fixture.host().add(recording(kThirdController, RecordingOptions{.order = &order}));
  fixture.host().add(recording(kFirstController, RecordingOptions{.order = &order}));
  fixture.host().add(recording(kSecondController, RecordingOptions{.order = &order}));
  REQUIRE(fixture.host().size() == 3);

  static_cast<void>(fixture.host().decide_once());

  CHECK(order == std::vector<std::uint64_t>{kFirstController, kSecondController, kThirdController});
  CHECK(fixture.host().contains(simulation::ControllerId::create(kSecondController)));
  CHECK_FALSE(fixture.host().contains(simulation::ControllerId::create(77)));
}

TEST_CASE("ControllerHost submits one pass's commands in ascending ControllerId order",
          "[unit][controllers][controller_host]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 3);
  const simulation::EntityId first = *fixture.entity_of(kFirstController);
  const simulation::EntityId second = *fixture.entity_of(kSecondController);
  const simulation::EntityId third = *fixture.entity_of(kThirdController);

  fixture.host().add(
      recording(kThirdController, RecordingOptions{.emitted = {thrust_for(third, 0.0, 1.0)}}));
  fixture.host().add(
      recording(kFirstController, RecordingOptions{.emitted = {thrust_for(first, 1.0, 0.0)}}));
  fixture.host().add(
      recording(kSecondController, RecordingOptions{.emitted = {thrust_for(second, -1.0, 0.0)}}));

  const controllers::ControllerHostPass pass = fixture.host().decide_once();

  CHECK(pass.deciding_controller_count == 3);
  CHECK(pass.decided_command_count == 3);
  CHECK(pass.accepted_command_count == 3);
  CHECK(pass.refused_command_count == 0);
  CHECK(pass.failed_controller_count == 0);

  // The mailbox keeps submission order for distinct identities, so draining it observes the order
  // the host submitted in.
  const std::vector<simulation::Command> drained = fixture.mailbox().drain();
  REQUIRE(drained.size() == 3);
  CHECK(simulation::addressed_identity_of(drained[0]).ordering_key() == first.value());
  CHECK(simulation::addressed_identity_of(drained[1]).ordering_key() == second.value());
  CHECK(simulation::addressed_identity_of(drained[2]).ordering_key() == third.value());
}

TEST_CASE("ControllerHost gives every controller in one pass the same acquired snapshot",
          "[unit][controllers][controller_host]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 2);
  std::unique_ptr<RecordingController> owned_first =
      recording(kFirstController, RecordingOptions{});
  std::unique_ptr<RecordingController> owned_second =
      recording(kSecondController, RecordingOptions{});
  const RecordingController* const first = owned_first.get();
  const RecordingController* const second = owned_second.get();
  fixture.host().add(std::move(owned_first));
  fixture.host().add(std::move(owned_second));

  const controllers::ControllerHostPass pass = fixture.host().decide_once();

  REQUIRE(first->observed_tick_sequence().has_value());
  REQUIRE(second->observed_tick_sequence().has_value());
  CHECK(*first->observed_tick_sequence() == *second->observed_tick_sequence());
  CHECK(pass.observed_tick_sequence == *first->observed_tick_sequence());
}

TEST_CASE("ControllerHost decides for a controller that has no live entity",
          "[unit][controllers][controller_host]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const simulation::ControllerId joining = fixture.sink().open_session("wanderer", "Joining");
  fixture.host().add(controllers::WandererController::create(joining, 4242));

  const controllers::ControllerHostPass pass = fixture.host().decide_once();

  // Absent is a defined state: the controller decided, and what it decided was to ask for a body.
  CHECK(pass.deciding_controller_count == 1);
  CHECK(pass.decided_command_count == 1);
  CHECK(pass.accepted_command_count == 1);
  const std::vector<simulation::Command> drained = fixture.mailbox().drain();
  REQUIRE(drained.size() == 1);
  CHECK(simulation::command_kind_of(drained.front()) == simulation::CommandKind::kSpawn);
  CHECK(simulation::addressed_identity_of(drained.front()).ordering_key() == joining.value());
}

TEST_CASE("ControllerHost isolates and counts a controller that throws",
          "[unit][controllers][controller_host]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 3);
  const simulation::EntityId first = *fixture.entity_of(kFirstController);
  const simulation::EntityId third = *fixture.entity_of(kThirdController);
  std::vector<std::uint64_t> order;

  fixture.host().add(
      recording(kFirstController,
                RecordingOptions{.emitted = {thrust_for(first, 1.0, 0.0)}, .order = &order}));
  fixture.host().add(recording(
      kSecondController,
      RecordingOptions{.emitted = {thrust_for(first, 0.0, 1.0)}, .order = &order, .throws = true}));
  fixture.host().add(
      recording(kThirdController,
                RecordingOptions{.emitted = {thrust_for(third, -1.0, 0.0)}, .order = &order}));

  const controllers::ControllerHostPass pass = fixture.host().decide_once();

  // The pass continued: the failing controller neither ended it nor took its neighbours' commands
  // with it, which is exactly what a misbehaving network session cannot do either.
  CHECK(order == std::vector<std::uint64_t>{kFirstController, kSecondController, kThirdController});
  CHECK(pass.failed_controller_count == 1);
  CHECK(pass.deciding_controller_count == 2);
  CHECK(pass.decided_command_count == 2);
  CHECK(pass.accepted_command_count == 2);

  // And nothing was swallowed.
  REQUIRE(fixture.host().last_failure().has_value());
  CHECK(fixture.host().last_failure()->controller ==
        simulation::ControllerId::create(kSecondController));
  CHECK(fixture.host().last_failure()->controller_kind == "recording");
  CHECK(fixture.host().last_failure()->message == "recording controller failed on purpose");
  CHECK(fixture.host().last_failure()->observed_tick_sequence == pass.observed_tick_sequence);
  CHECK(fixture.host().statistics().failed_controller_count == 1);

  const std::vector<simulation::Command> drained = fixture.mailbox().drain();
  REQUIRE(drained.size() == 2);
  CHECK(simulation::addressed_identity_of(drained[0]).ordering_key() == first.value());
  CHECK(simulation::addressed_identity_of(drained[1]).ordering_key() == third.value());
}

TEST_CASE("ControllerHost counts a submission the sink refuses and keeps going",
          "[unit][controllers][controller_host]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 2);
  const simulation::EntityId first = *fixture.entity_of(kFirstController);
  const simulation::EntityId second = *fixture.entity_of(kSecondController);

  // An out-of-range thrust component is refused by value, and the second command of the same
  // controller is still offered: each command is refused on its own merits.
  fixture.host().add(recording(
      kFirstController,
      RecordingOptions{.emitted = {thrust_for(first, 2.0, 0.0), thrust_for(first, 0.5, 0.0)}}));
  fixture.host().add(
      recording(kSecondController, RecordingOptions{.emitted = {thrust_for(second, 0.0, 1.0)}}));

  const controllers::ControllerHostPass pass = fixture.host().decide_once();

  CHECK(pass.decided_command_count == 3);
  CHECK(pass.accepted_command_count == 2);
  CHECK(pass.refused_command_count == 1);
  CHECK(fixture.host().statistics().refused_command_count == 1);
  // The value the sink refused never reached the mailbox, so a bot's bad value is stopped at the
  // same boundary a network frame's bad value is.
  CHECK(fixture.mailbox().statistics().submitted_command_count == 2);
  CHECK(fixture.mailbox().drain().size() == 2);
}

TEST_CASE("ControllerHost counts a submission from a session the sink has closed",
          "[unit][controllers][controller_host]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 2);
  const simulation::EntityId first = *fixture.entity_of(kFirstController);
  fixture.host().add(
      recording(kFirstController, RecordingOptions{.emitted = {thrust_for(first, 1.0, 0.0)}}));
  CHECK(fixture.sink().close_session(simulation::ControllerId::create(kFirstController)) ==
        runtime::ControllerCloseResult::kClosed);

  const controllers::ControllerHostPass pass = fixture.host().decide_once();

  CHECK(pass.deciding_controller_count == 1);
  CHECK(pass.refused_command_count == 1);
  CHECK(pass.accepted_command_count == 0);
  CHECK(fixture.mailbox().drain().empty());
}

TEST_CASE("ControllerHost accumulates every pass into its statistics",
          "[unit][controllers][controller_host]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  const simulation::EntityId first = *fixture.entity_of(kFirstController);
  fixture.host().add(
      recording(kFirstController, RecordingOptions{.emitted = {thrust_for(first, 1.0, 0.0)}}));

  static_cast<void>(fixture.host().decide_once());
  static_cast<void>(fixture.host().decide_once());

  CHECK(fixture.host().statistics().pass_count == 2);
  CHECK(fixture.host().statistics().decided_command_count == 2);
  CHECK(fixture.host().statistics().accepted_command_count == 2);
  CHECK(fixture.host().statistics().failed_controller_count == 0);
}

TEST_CASE("ControllerHost refuses an absent controller and a duplicated identity",
          "[unit][controllers][controller_host]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  fixture.host().add(recording(kFirstController, RecordingOptions{}));

  CHECK_THROWS_AS(fixture.host().add(nullptr), controllers::ControllersValidationError);
  try {
    fixture.host().add(recording(kFirstController, RecordingOptions{}));
    FAIL("a second controller must not claim an issued ControllerId");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() ==
          controllers::ControllersValidationCode::kControllerIdDuplicate);
    CHECK(error.code() == "CONTROLLERS.CONTROLLER_ID_DUPLICATE");
  }
  CHECK(fixture.host().size() == 1);
}

TEST_CASE("ControllerHost refuses a roster above the accepted maximum",
          "[unit][controllers][controller_host]") {
  testing::ControllersFixture fixture(testing::controllers_map_of(4), 1);
  for (std::size_t index = 0; index < controllers::kMaximumHostedControllerCount; ++index) {
    fixture.host().add(recording(simulation::kMinimumControllerId + index, RecordingOptions{}));
  }

  try {
    fixture.host().add(
        recording(simulation::kMinimumControllerId + controllers::kMaximumHostedControllerCount,
                  RecordingOptions{}));
    FAIL("a host must refuse a roster above the accepted maximum");
  } catch (const controllers::ControllersValidationError& error) {
    CHECK(error.validation_code() == controllers::ControllersValidationCode::kControllerHostFull);
  }
}

TEST_CASE("ControllerHost acquires one snapshot per pass while a runtime publishes under it",
          "[unit][controllers][controller_host][concurrency]") {
  runtime::SimulationRuntime simulation_runtime(live_sandbox());
  controllers::ControllerHost host(simulation_runtime.snapshot_publication(),
                                   simulation_runtime.command_sink());
  // The first controller blocks inside `decide` until the worker publishes a later tick, so the
  // second one decides *after* the world has demonstrably moved on.
  std::unique_ptr<RecordingController> owned_first =
      recording(kFirstController,
                RecordingOptions{.advance_barrier = &simulation_runtime.snapshot_publication()});
  std::unique_ptr<RecordingController> owned_second =
      recording(kSecondController, RecordingOptions{});
  const RecordingController* const first = owned_first.get();
  const RecordingController* const second = owned_second.get();
  host.add(std::move(owned_first));
  host.add(std::move(owned_second));
  simulation_runtime.start();

  const controllers::ControllerHostPass pass = host.decide_once();
  simulation_runtime.stop();

  REQUIRE(first->observed_tick_sequence().has_value());
  REQUIRE(second->observed_tick_sequence().has_value());
  // The barrier proves the publication really did advance mid-pass, so the equality below is one
  // acquisition rather than a still world.
  CHECK(first->observed_publication_advance());
  CHECK(*first->observed_tick_sequence() == *second->observed_tick_sequence());
  CHECK(pass.observed_tick_sequence == *first->observed_tick_sequence());
}

TEST_CASE("ControllerHost drives two sandbox bots that both spawn and both move",
          "[unit][controllers][controller_host][concurrency]") {
  runtime::SimulationRuntime simulation_runtime(live_sandbox());
  controllers::ControllerHost host(simulation_runtime.snapshot_publication(),
                                   simulation_runtime.command_sink());
  const simulation::ControllerId left =
      simulation_runtime.command_sink().open_session("wanderer", "Left");
  const simulation::ControllerId right =
      simulation_runtime.command_sink().open_session("wanderer", "Right");
  host.add(controllers::WandererController::create(
      left, 11, controllers::WandererController::Personality{.reaction_delay_frames = 0}));
  host.add(controllers::WandererController::create(
      right, 22, controllers::WandererController::Personality{.reaction_delay_frames = 0}));
  simulation_runtime.start();

  // The bots act through the identical path a network session acts through: `decide_once` reads the
  // publication and writes the sink, and nothing in this test touches the simulation.
  const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now() + kMovementDeadline;
  bool both_moving = false;
  while (!both_moving && std::chrono::steady_clock::now() < deadline) {
    static_cast<void>(host.decide_once());
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    const std::shared_ptr<const simulation::WorldSnapshot> snapshot =
        simulation_runtime.snapshot_publication().latest();
    std::size_t moving = 0;
    for (const simulation::PlayerSnapshot& player : snapshot->players()) {
      if (player.velocity().magnitude() > 0.0) {
        ++moving;
      }
    }
    both_moving = snapshot->players().size() == 2 && moving == 2;
  }
  const controllers::ControllerHost::Statistics statistics = host.statistics();
  simulation_runtime.stop();

  // A worker failure would look exactly like a slow bot from out here, so it is surfaced rather
  // than reported as a timeout.
  CHECK_NOTHROW(simulation_runtime.rethrow_if_failed());
  CHECK(both_moving);
  CHECK(statistics.failed_controller_count == 0);
  CHECK(statistics.accepted_command_count > 0);
}
