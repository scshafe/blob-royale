#include "command_kind_mask.hpp"
#include "command_mailbox.hpp"
#include "command_registry.hpp"
#include "command_sink.hpp"
#include "command_sink_error.hpp"
#include "command_submission_result.hpp"
#include "controller_directory.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "entity_id_allocator.hpp"
#include "runtime_limits.hpp"
#include "simulation_limits.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;

namespace {

constexpr std::uint64_t kIssuedEntityIdCeiling = 10;
constexpr std::uint64_t kIssuedEntityId = 9;

// Everything one command source is given, owned together so a test reads like the runtime does.
struct CommandSinkFixture final {
  explicit CommandSinkFixture(
      const simulation::CommandKindMask accepted_kinds = simulation::CommandKindMask::all())
      : mailbox(accepted_kinds), allocator(runtime::EntityIdAllocator::create(
                                     simulation::EntityId::create(kIssuedEntityIdCeiling))),
        sink(mailbox, directory, allocator) {}

  runtime::CommandMailbox mailbox;
  runtime::ControllerDirectory directory;
  runtime::EntityIdAllocator allocator;
  runtime::CommandSink sink;
};

[[nodiscard]] simulation::Command thrust_fixture(const std::uint64_t entity_id, const double x,
                                                 const double y) {
  return simulation::ThrustCommand{.entity = simulation::EntityId::create(entity_id),
                                   .direction = simulation::Vector2::create(x, y)};
}

} // namespace

TEST_CASE("CommandSink issues monotonic ControllerIds and registers their presentation values",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture;

  const simulation::ControllerId first = fixture.sink.open_session("session", "Ada");
  const simulation::ControllerId second = fixture.sink.open_session("wanderer", "Wanderer 1");

  // A ControllerId and not an EntityId: the engine draws entity ids inside `step` from the tick's
  // reservation, and a controller outlives every entity it drives.
  REQUIRE(first.value() == simulation::kMinimumControllerId);
  REQUIRE(second.value() == first.value() + 1);

  const std::optional<runtime::ControllerPresentation> presentation =
      fixture.directory.find(second);
  REQUIRE(presentation.has_value());
  REQUIRE(presentation->controller_kind == "wanderer");
  REQUIRE(presentation->display_name == "Wanderer 1");
}

TEST_CASE("CommandSink refuses a session whose controller kind is rejected",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture;

  REQUIRE_THROWS_AS(fixture.sink.open_session("", "Ada"), runtime::CommandSinkError);
  REQUIRE(fixture.directory.size() == 0);
}

TEST_CASE("CommandSink refuses a session whose proxy-supplied display name is rejected",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture;
  const std::string oversized(runtime::kMaximumDisplayNameLength + 1, 'n');

  REQUIRE_THROWS_AS(fixture.sink.open_session("session", oversized), runtime::CommandSinkError);
  REQUIRE_THROWS_AS(fixture.sink.open_session("session", std::string("Ada\nBob")),
                    runtime::CommandSinkError);
  REQUIRE(fixture.directory.size() == 0);
}

TEST_CASE("CommandSink names the reason a session could not be opened",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture;

  try {
    static_cast<void>(fixture.sink.open_session("", "Ada"));
    FAIL("open_session must refuse an empty controller kind");
  } catch (const runtime::CommandSinkError& error) {
    REQUIRE(error.error_code() == runtime::CommandSinkErrorCode::kControllerKindRejected);
    REQUIRE(error.code() == "RUNTIME.CONTROLLER_KIND_REJECTED");
    REQUIRE(error.context() == "command_sink.controller_kind");
  }
}

TEST_CASE("CommandSink refuses a session once the controller directory is full",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture;
  for (std::size_t index = 0; index < runtime::kMaximumControllerDirectoryEntryCount; ++index) {
    static_cast<void>(fixture.sink.open_session("session", "Ada"));
  }

  REQUIRE_THROWS_AS(fixture.sink.open_session("session", "Ada"), runtime::CommandSinkError);
}

TEST_CASE("CommandSink forwards an open session's command to the mailbox",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture;
  const simulation::ControllerId controller = fixture.sink.open_session("session", "Ada");

  REQUIRE(fixture.sink.submit(controller, thrust_fixture(kIssuedEntityId, 1.0, 0.0)) ==
          runtime::CommandSubmissionResult::kAccepted);
  REQUIRE(fixture.mailbox.statistics().pending_command_count == 1);
}

TEST_CASE("CommandSink refuses a command from a controller that never opened a session",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture;

  REQUIRE(fixture.sink.submit(simulation::ControllerId::create(simulation::kMinimumControllerId),
                              thrust_fixture(kIssuedEntityId, 1.0, 0.0)) ==
          runtime::CommandSubmissionResult::kRejectedSessionNotOpen);
  REQUIRE(fixture.mailbox.statistics().submitted_command_count == 0);
}

TEST_CASE("CommandSink refuses a command submitted after the session closed",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture;
  const simulation::ControllerId controller = fixture.sink.open_session("session", "Ada");
  REQUIRE(fixture.sink.close_session(controller) == runtime::ControllerCloseResult::kClosed);

  REQUIRE(fixture.sink.submit(controller, thrust_fixture(kIssuedEntityId, 1.0, 0.0)) ==
          runtime::CommandSubmissionResult::kRejectedSessionNotOpen);
  REQUIRE(fixture.mailbox.statistics().submitted_command_count == 0);
}

TEST_CASE("CommandSink reports a second close of one session rather than failing",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture;
  const simulation::ControllerId controller = fixture.sink.open_session("session", "Ada");

  REQUIRE(fixture.sink.close_session(controller) == runtime::ControllerCloseResult::kClosed);
  REQUIRE(fixture.sink.close_session(controller) ==
          runtime::ControllerCloseResult::kUnknownControllerId);
}

TEST_CASE("CommandSink accepts a spawn stamped with the session's own controller",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture;
  const simulation::ControllerId controller = fixture.sink.open_session("session", "Ada");

  REQUIRE(fixture.sink.submit(controller, simulation::SpawnCommand{.controller = controller}) ==
          runtime::CommandSubmissionResult::kAccepted);
}

TEST_CASE("CommandSink refuses a spawn stamped with a foreign controller",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture;
  const simulation::ControllerId controller = fixture.sink.open_session("session", "Ada");
  const simulation::ControllerId other = fixture.sink.open_session("session", "Bob");

  REQUIRE(fixture.sink.submit(controller, simulation::SpawnCommand{.controller = other}) ==
          runtime::CommandSubmissionResult::kRejectedForeignController);
  REQUIRE(fixture.mailbox.statistics().submitted_command_count == 0);
}

TEST_CASE("CommandSink refuses a kind the running mode does not accept",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture(
      simulation::CommandKindMask::create({simulation::CommandKind::kThrust}));
  const simulation::ControllerId controller = fixture.sink.open_session("session", "Ada");

  REQUIRE(fixture.sink.submit(controller, simulation::SpawnCommand{.controller = controller}) ==
          runtime::CommandSubmissionResult::kRejectedUnacceptedKind);
}

TEST_CASE("CommandSink refuses a thrust direction outside the accepted range",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture;
  const simulation::ControllerId controller = fixture.sink.open_session("session", "Ada");

  // Refused here rather than in InputBatch::create, where it would be a hard tick failure one
  // client could trigger for everyone.
  REQUIRE(fixture.sink.submit(controller, thrust_fixture(kIssuedEntityId, 1.5, 0.0)) ==
          runtime::CommandSubmissionResult::kRejectedThrustDirectionOutOfRange);
  REQUIRE(fixture.sink.submit(controller, thrust_fixture(kIssuedEntityId, 0.0, -1.5)) ==
          runtime::CommandSubmissionResult::kRejectedThrustDirectionOutOfRange);
  REQUIRE(fixture.sink.submit(controller, thrust_fixture(kIssuedEntityId, 1.0, -1.0)) ==
          runtime::CommandSubmissionResult::kAccepted);
}

TEST_CASE("CommandSink refuses a despawn naming an id no tick has issued",
          "[unit][runtime][command_sink]") {
  CommandSinkFixture fixture;
  const simulation::ControllerId controller = fixture.sink.open_session("session", "Ada");

  // An id at or above the cursor lies inside a future tick's reservation, which is the one despawn
  // InputBatch::create rejects outright.
  REQUIRE(fixture.sink.submit(controller,
                              simulation::DespawnCommand{.entity = simulation::EntityId::create(
                                                             kIssuedEntityIdCeiling)}) ==
          runtime::CommandSubmissionResult::kRejectedUnissuedEntityId);
  REQUIRE(fixture.sink.submit(controller,
                              simulation::DespawnCommand{
                                  .entity = simulation::EntityId::create(kIssuedEntityId)}) ==
          runtime::CommandSubmissionResult::kAccepted);
}
