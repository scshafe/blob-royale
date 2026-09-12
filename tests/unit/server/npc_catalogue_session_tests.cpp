#include "../simulation/fixtures/npc_declaration_fixture.hpp"
#include "server_test_fixture.hpp"

#include "command_decoding.hpp"
#include "command_submission_result.hpp"
#include "session_welcome.hpp"

#include <catch2/catch_test_macros.hpp>

namespace server = blob_royale::server;
namespace protocol = blob_royale::protocol;
namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::npc_declaration_fixture;

TEST_CASE(
    "Session capability retains one profile catalogue for welcome decoder and runtime admission",
    "[unit][server][npc_profile]") {
  const auto catalogue = fixture::catalogue();
  const server::test_fixture::MatchSessionFixture state(catalogue);
  const auto context = state.context();
  CHECK(context.npc_catalogue() == fixture::catalogue());
  CHECK(context.npc_profiles().size() == catalogue.profiles().size());
  const auto controller = context.command_sink().open_session("session", "profile reader");
  const auto decoded = protocol::decode_command_envelope(
      R"({"kind":"seat_npc","payload":{"seat_index":1,"npc_kind":"tactical","profile_name":"steady"}})",
      context.accepted_command_kinds(), simulation::EntityId::create(fixture::kController),
      controller, context.npc_catalogue());
  REQUIRE(decoded.is_accepted());
  CHECK(context.command_sink().submit(controller, *decoded.command()) ==
        runtime::CommandSubmissionResult::kAccepted);
  const auto unknown = fixture::seat(fixture::profiled("unknown"), controller.value());
  CHECK(context.command_sink().submit(controller, unknown) ==
        runtime::CommandSubmissionResult::kRejectedNpcDeclarationUnknown);
  const auto welcome = protocol::SessionWelcome::create(
      simulation::EntityId::create(fixture::kController), controller, "profile reader", "royale",
      context.map_name(), context.accepted_command_kinds(), context.npc_catalogue(),
      context.lobby_id(), context.seat_count_maximum(),
      simulation::TerrainDefinition::solid(simulation::ArenaBounds::create(960.0, 640.0)));
  CHECK(welcome.npc_catalogue() == context.npc_catalogue());
}
