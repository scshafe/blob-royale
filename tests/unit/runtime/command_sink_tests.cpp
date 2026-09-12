#include "../simulation/fixtures/npc_declaration_fixture.hpp"
#include "command_kind_mask.hpp"
#include "command_mailbox.hpp"
#include "command_registry.hpp"
#include "command_sink.hpp"
#include "command_sink_error.hpp"
#include "command_submission_result.hpp"
#include "commands/join_command.hpp"
#include "commands/leave_command.hpp"
#include "commands/shield_command.hpp"
#include "controller_directory.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "entity_id_allocator.hpp"
#include "fixtures/thrust_input_generation_fixture.hpp"
#include "runtime_limits.hpp"
#include "seat_roster.hpp"
#include "simulation_limits.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;
namespace npc_fixture = blob_royale::testing::npc_declaration_fixture;

namespace {

constexpr std::uint64_t kIssuedEntityIdCeiling = 10;
constexpr std::uint64_t kIssuedEntityId = 9;

// Everything one command source is given, owned together so a test reads like the runtime does.
struct CommandSinkFixture final {
  // The first controller id is explicit rather than defaulted, because it is a real decision: a
  // live runtime opens it above every controller id the loaded world already carries, so a session
  // can never be issued an id a seeded body holds and adopt that body instead of spawning
  // (`simulation_runtime.cpp`, first_session_controller_id). These tests build no world, so the
  // minimum is the honest floor here.
  explicit CommandSinkFixture(
      const simulation::CommandKindMask accepted_kinds = simulation::CommandKindMask::all(),
      const simulation::ControllerId::Value first_controller_id = simulation::kMinimumControllerId,
      simulation::NpcCatalogue npc_catalogue = simulation::NpcCatalogue::empty())
      : mailbox(accepted_kinds), allocator(runtime::EntityIdAllocator::create(
                                     simulation::EntityId::create(kIssuedEntityIdCeiling))),
        sink(mailbox, directory, allocator, first_controller_id, std::move(npc_catalogue)) {}

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

[[nodiscard]] simulation::Command
shield_fixture(const std::uint64_t entity_id,
               const std::optional<simulation::TickSequence> generation = {}) {
  return simulation::ShieldCommand{.entity = simulation::EntityId::create(entity_id),
                                   .input_generation = generation};
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

TEST_CASE("CommandSink admits exact catalogue selections and refuses missing unknown or mismatched "
          "profiles",
          "[unit][runtime][command_sink][npc_profile]") {
  CommandSinkFixture fixture(simulation::CommandKindMask::all(), simulation::kMinimumControllerId,
                             npc_fixture::catalogue());
  const auto controller = fixture.sink.open_session("session", "profile fixture");
  for (const auto& declaration : {npc_fixture::plain(), npc_fixture::profiled(),
                                  npc_fixture::profiled(npc_fixture::kOtherProfileName)}) {
    const auto command = npc_fixture::seat(declaration, controller.value());
    CHECK(fixture.sink.submit(controller, command) == runtime::CommandSubmissionResult::kAccepted);
    CHECK(fixture.mailbox.drain() == std::vector<simulation::Command>{command});
  }
  const auto before = fixture.mailbox.statistics().submitted_command_count;
  for (const auto& declaration :
       {simulation::NpcDeclaration{simulation::SeatKindName::create(npc_fixture::kProfileKind),
                                   std::nullopt},
        simulation::NpcDeclaration{simulation::SeatKindName::create(npc_fixture::kPlainKind),
                                   simulation::BotProfileName::create(npc_fixture::kProfileName)},
        npc_fixture::profiled("unknown")}) {
    const auto result =
        fixture.sink.submit(controller, npc_fixture::seat(declaration, controller.value()));
    CHECK(result == runtime::CommandSubmissionResult::kRejectedNpcDeclarationUnknown);
    CHECK(runtime::command_submission_result_name(result) == "rejected_npc_declaration_unknown");
  }
  CHECK(fixture.mailbox.statistics().submitted_command_count == before);
  CommandSinkFixture no_choices;
  const auto empty_controller = no_choices.sink.open_session("session", "empty catalogue");
  CHECK(no_choices.sink.submit(empty_controller,
                               npc_fixture::seat(npc_fixture::plain(), empty_controller.value())) ==
        runtime::CommandSubmissionResult::kRejectedNpcDeclarationUnknown);
}

TEST_CASE("CommandSink preserves literal indexed joins and rejects malformed guarded joins",
          "[unit][runtime][command_sink][npc_profile]") {
  CommandSinkFixture fixture(simulation::CommandKindMask::all(), npc_fixture::kBotController);
  const auto controller = fixture.sink.open_session("tactical", "join fixture");
  for (const auto& command :
       {npc_fixture::join(std::nullopt), npc_fixture::join(std::nullopt, std::nullopt),
        npc_fixture::join(npc_fixture::profiled())}) {
    CHECK(fixture.sink.submit(controller, command) == runtime::CommandSubmissionResult::kAccepted);
    CHECK(fixture.mailbox.drain() == std::vector<simulation::Command>{command});
  }
  for (const auto& command :
       {npc_fixture::join(npc_fixture::profiled(), std::nullopt),
        npc_fixture::join(simulation::NpcDeclaration{simulation::SeatKindName{}, std::nullopt})}) {
    const auto result = fixture.sink.submit(controller, command);
    CHECK(result == runtime::CommandSubmissionResult::kRejectedJoinDeclarationInvalid);
    CHECK(runtime::command_submission_result_name(result) == "rejected_join_declaration_invalid");
  }
  CHECK(fixture.mailbox.statistics().submitted_command_count == 3);
}

TEST_CASE(
    "CommandSink preserves omitted and maximum safe thrust input generations for every source",
    "[unit][runtime][command_sink][input_generation]") {
  for (const auto kind : runtime::thrust_input_fixture::kControllerKinds) {
    CAPTURE(kind);
    CommandSinkFixture fixture;
    const auto controller = fixture.sink.open_session(kind, "generation fixture");
    const auto absent = runtime::thrust_input_fixture::command(kIssuedEntityId, std::nullopt);
    REQUIRE(fixture.sink.submit(controller, absent) == runtime::CommandSubmissionResult::kAccepted);
    CHECK(fixture.mailbox.drain() == std::vector<simulation::Command>{absent});
    for (const auto value : runtime::thrust_input_fixture::kAcceptedGenerations) {
      for (const bool release : {false, true}) {
        CAPTURE(value, release);
        const auto command = runtime::thrust_input_fixture::command(
            kIssuedEntityId, simulation::TickSequence::create(value), release);
        REQUIRE(fixture.sink.submit(controller, command) ==
                runtime::CommandSubmissionResult::kAccepted);
        CHECK(fixture.mailbox.drain() == std::vector<simulation::Command>{command});
      }
    }
  }
}

TEST_CASE("CommandSink rejects zero thrust generation before mailbox admission including releases",
          "[unit][runtime][command_sink][input_generation]") {
  for (const auto kind : runtime::thrust_input_fixture::kControllerKinds) {
    CommandSinkFixture fixture;
    const auto controller = fixture.sink.open_session(kind, "generation fixture");
    for (const bool release : {false, true}) {
      CAPTURE(kind, release);
      const auto result = fixture.sink.submit(
          controller, runtime::thrust_input_fixture::command(
                          kIssuedEntityId, simulation::TickSequence::zero(), release));
      CHECK(result == runtime::CommandSubmissionResult::kRejectedThrustInputGenerationOutOfRange);
      CHECK(runtime::command_submission_result_name(result) ==
            "rejected_thrust_input_generation_out_of_range");
      CHECK_FALSE(runtime::command_submission_accepted(result));
    }
    CHECK(fixture.mailbox.statistics().submitted_command_count == 0);
    CHECK(fixture.mailbox.drain().empty());
  }
}

TEST_CASE("CommandSink preserves omitted and maximum safe shield generations for every source",
          "[unit][runtime][command_sink][shield][input_generation]") {
  // A shield carries no controller, so the anti-spoofing visitor answers nullopt for it and the
  // ownership question is settled by the entity the boundary stamped -- not by an actor field the
  // pulse would otherwise have to carry.
  for (const auto kind : runtime::thrust_input_fixture::kControllerKinds) {
    CAPTURE(kind);
    CommandSinkFixture fixture;
    const auto controller = fixture.sink.open_session(kind, "shield fixture");
    const auto absent = shield_fixture(kIssuedEntityId);
    REQUIRE(fixture.sink.submit(controller, absent) == runtime::CommandSubmissionResult::kAccepted);
    CHECK(fixture.mailbox.drain() == std::vector<simulation::Command>{absent});
    for (const auto value : runtime::thrust_input_fixture::kAcceptedGenerations) {
      CAPTURE(value);
      const auto command = shield_fixture(kIssuedEntityId, simulation::TickSequence::create(value));
      REQUIRE(fixture.sink.submit(controller, command) ==
              runtime::CommandSubmissionResult::kAccepted);
      CHECK(fixture.mailbox.drain() == std::vector<simulation::Command>{command});
    }
  }
}

TEST_CASE("CommandSink rejects zero shield generation before mailbox admission",
          "[unit][runtime][command_sink][shield][input_generation]") {
  // The boundary refuses what InputBatch::create would throw on, so one client's malformed pulse
  // cannot hard-fail the tick for everyone. The refusal names the shield: reusing the thrust value
  // would make the structured log describe a command this client never sent.
  for (const auto kind : runtime::thrust_input_fixture::kControllerKinds) {
    CAPTURE(kind);
    CommandSinkFixture fixture;
    const auto controller = fixture.sink.open_session(kind, "shield fixture");
    const auto result = fixture.sink.submit(
        controller, shield_fixture(kIssuedEntityId, simulation::TickSequence::zero()));
    CHECK(result == runtime::CommandSubmissionResult::kRejectedShieldInputGenerationOutOfRange);
    CHECK(runtime::command_submission_result_name(result) ==
          "rejected_shield_input_generation_out_of_range");
    CHECK_FALSE(runtime::command_submission_accepted(result));
    CHECK(fixture.mailbox.statistics().submitted_command_count == 0);
    CHECK(fixture.mailbox.drain().empty());
  }
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
  // The one submission the mailbox saw is the leave the close itself enqueued; the refused thrust
  // never reached it.
  REQUIRE(fixture.mailbox.statistics().submitted_command_count == 1);
}

TEST_CASE("CommandSink enqueues one leave for a closing session and none for a second close",
          "[unit][runtime][command_sink][leave]") {
  CommandSinkFixture fixture;
  const simulation::ControllerId controller = fixture.sink.open_session("session", "Ada");

  // The leave is in the mailbox before the identity is retired, stamped with the controller that
  // is leaving, so the tick can destroy whatever it drove without the session naming an entity.
  REQUIRE(fixture.sink.close_session(controller) == runtime::ControllerCloseResult::kClosed);
  const std::vector<simulation::Command> drained = fixture.mailbox.drain();
  REQUIRE(drained.size() == 1);
  CHECK(drained[0] == simulation::Command{simulation::LeaveCommand{controller}});

  // A second close is refused at the sink as a closed session, so its leave never reaches the
  // mailbox at all: one submission, one acceptance, nothing to drain.
  REQUIRE(fixture.sink.close_session(controller) ==
          runtime::ControllerCloseResult::kUnknownControllerId);
  CHECK(fixture.mailbox.drain().empty());
  CHECK(fixture.mailbox.statistics().submitted_command_count == 1);
  CHECK(fixture.mailbox.statistics().accepted_command_count == 1);
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

TEST_CASE("CommandSink refuses a lobby command stamped with a foreign controller",
          "[unit][runtime][command_sink][lobby]") {
  // The stamp is the server's own, so a foreign one means the boundary and the sink disagree about
  // who is submitting. It matters more here than for a spawn: a lobby command spends somebody's
  // decision -- a Start, a seat, a resize -- and one session must not spend another's.
  CommandSinkFixture fixture;
  const simulation::ControllerId controller = fixture.sink.open_session("session", "Ada");
  const simulation::ControllerId other = fixture.sink.open_session("session", "Bob");

  CHECK(fixture.sink.submit(controller, simulation::StartMatchCommand{.controller = other}) ==
        runtime::CommandSubmissionResult::kRejectedForeignController);
  CHECK(fixture.sink.submit(
            controller, simulation::SetSeatCountCommand{.controller = other, .seat_count = 4}) ==
        runtime::CommandSubmissionResult::kRejectedForeignController);
  // A join is server-issued, and the rule still holds: the reconciliation that joins a bot to its
  // seat submits as that bot, never as somebody else.
  CHECK(fixture.sink.submit(
            controller, simulation::JoinCommand{.controller = other, .seat_index = std::nullopt}) ==
        runtime::CommandSubmissionResult::kRejectedForeignController);
  CHECK(fixture.sink.submit(controller, simulation::StartMatchCommand{.controller = controller}) ==
        runtime::CommandSubmissionResult::kAccepted);
  CHECK(fixture.sink.submit(controller, simulation::JoinCommand{.controller = controller,
                                                                .seat_index = std::nullopt}) ==
        runtime::CommandSubmissionResult::kAccepted);
}

TEST_CASE("CommandSink refuses a seat index or a seat count outside the engine's own bound",
          "[unit][runtime][command_sink][lobby]") {
  // The same rule `InputBatch::create` enforces, refused here so that one client's out-of-range
  // frame cannot become a hard tick failure for everyone. It is the *value* bound and not the
  // running lobby's size: the sink holds no world, and an index inside the bound that names no seat
  // is ignored by the tick rather than refused here.
  CommandSinkFixture fixture;
  const simulation::ControllerId controller = fixture.sink.open_session("session", "Ada");
  constexpr std::uint64_t kSeatCeiling = simulation::kMaximumLobbySeatCount;

  CHECK(fixture.sink.submit(
            controller,
            simulation::SeatNpcCommand{.controller = controller,
                                       .seat_index = kSeatCeiling,
                                       .kind = simulation::SeatKindName::create("wanderer")}) ==
        runtime::CommandSubmissionResult::kRejectedSeatIndexOutOfRange);
  CHECK(fixture.sink.submit(controller, simulation::ClearSeatCommand{.controller = controller,
                                                                     .seat_index = kSeatCeiling}) ==
        runtime::CommandSubmissionResult::kRejectedSeatIndexOutOfRange);
  CHECK(fixture.sink.submit(controller, simulation::JoinCommand{.controller = controller,
                                                                .seat_index = kSeatCeiling}) ==
        runtime::CommandSubmissionResult::kRejectedSeatIndexOutOfRange);
  CHECK(fixture.sink.submit(controller, simulation::SetSeatCountCommand{.controller = controller,
                                                                        .seat_count = 0}) ==
        runtime::CommandSubmissionResult::kRejectedSeatCountOutOfRange);
  CHECK(fixture.sink.submit(controller,
                            simulation::SetSeatCountCommand{.controller = controller,
                                                            .seat_count = kSeatCeiling + 1}) ==
        runtime::CommandSubmissionResult::kRejectedSeatCountOutOfRange);

  // The bounds themselves are accepted, so the rejection is a bound and not an off-by-one.
  CHECK(fixture.sink.submit(controller,
                            simulation::ClearSeatCommand{.controller = controller,
                                                         .seat_index = kSeatCeiling - 1}) ==
        runtime::CommandSubmissionResult::kAccepted);
  CHECK(fixture.sink.submit(controller, simulation::JoinCommand{.controller = controller,
                                                                .seat_index = kSeatCeiling - 1}) ==
        runtime::CommandSubmissionResult::kAccepted);
  CHECK(fixture.sink.submit(controller,
                            simulation::SetSeatCountCommand{.controller = controller,
                                                            .seat_count = kSeatCeiling}) ==
        runtime::CommandSubmissionResult::kAccepted);
}
