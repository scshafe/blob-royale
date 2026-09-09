#include "integration_test_error.hpp"
#include "server_fixture_state.hpp"
#include "session_websocket_client.hpp"

#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unistd.h>
#include <vector>

namespace blob_royale::integration_test {
namespace {

namespace json = boost::json;

constexpr auto kTransportOperationTimeout = std::chrono::seconds{5};
// Committed ticks awaited after a command, at the fixed 400 Hz tick rate. It is a tick count and
// not a wall-clock sleep, so the assertion is a function of the simulation's own clock.
//
// A tenth of a second at the configured 400 wu/s^2 thrust maximum is two world units of travel,
// which is exact on the wire and two orders of magnitude below the 35 world units between adjacent
// spawn markers -- so the commanding blob provably moved and provably did not reach anyone else.
constexpr std::uint64_t kTicksAwaitedAfterCommand = 40;
constexpr std::uint16_t kPolicyErrorCloseCode = 1'008;

// One published seat, exactly as `match-data.schema.json` shapes it: every member present, the ones
// that do not apply null.
struct PublishedSeat final {
  std::string kind;
  std::optional<std::uint64_t> controller_id;
  std::optional<std::string> npc_kind;
};

// One published body, keyed by entity id, as this contract needs to read it.
struct PublishedBody final {
  std::uint64_t controller_id;
  double position_x;
  double position_y;
  std::string controller_kind;
  std::string display_name;
};

[[noreturn]] void throw_contract_violation(const std::string_view operation,
                                           const std::string_view context) {
  throw IntegrationTestError{IntegrationTestErrorCode::kContractViolation, std::string{operation},
                             std::string{context}};
}

[[nodiscard]] std::filesystem::path parse_fixture_directory(const int argument_count,
                                                            const char* const arguments[]) {
  if (argument_count != 3 || std::string_view{arguments[1]} != "--fixture-directory" ||
      std::string_view{arguments[2]}.empty()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                               "session_contracts.parse_arguments",
                               "usage: blob_server_session_integration --fixture-directory <path>"};
  }
  return std::filesystem::path{arguments[2]};
}

void require_running_server_process(const pid_t server_process_id) {
  if (::kill(server_process_id, 0) != 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kProcessFailed,
                               "session_contracts.require_server",
                               "fixture server process is not running"};
  }
}

[[nodiscard]] const json::object& required_object(const json::object& parent,
                                                  const std::string_view name,
                                                  const std::string_view operation) {
  const json::value* const member = parent.if_contains(name);
  if (member == nullptr || !member->is_object()) {
    throw_contract_violation(operation, "frame is missing a required object member");
  }
  return member->as_object();
}

[[nodiscard]] double required_number(const json::object& parent, const std::string_view name,
                                     const std::string_view operation) {
  const json::value* const member = parent.if_contains(name);
  if (member == nullptr || !member->is_number()) {
    throw_contract_violation(operation, "frame is missing a required numeric member");
  }
  return member->to_number<double>();
}

[[nodiscard]] std::string required_string(const json::object& parent, const std::string_view name,
                                          const std::string_view operation) {
  const json::value* const member = parent.if_contains(name);
  if (member == nullptr || !member->is_string()) {
    throw_contract_violation(operation, "frame is missing a required string member");
  }
  return std::string{member->as_string()};
}

// Every published entity that carries both a controller link and a body, keyed by entity id.
[[nodiscard]] std::map<std::uint64_t, PublishedBody>
published_bodies(const std::string& snapshot_frame, const std::string_view operation) {
  const json::value document = json::parse(snapshot_frame);
  if (!document.is_object()) {
    throw_contract_violation(operation, "snapshot frame is not a JSON object");
  }
  const json::object& data = required_object(document.as_object(), "data", operation);
  const json::value* const entities = data.if_contains("entities");
  if (entities == nullptr || !entities->is_array()) {
    throw_contract_violation(operation, "snapshot data is missing its entity array");
  }

  std::map<std::uint64_t, PublishedBody> bodies;
  for (const json::value& entry : entities->as_array()) {
    if (!entry.is_object()) {
      throw_contract_violation(operation, "snapshot entity entry is not an object");
    }
    const json::object& entity = entry.as_object();
    const json::value* const entity_id = entity.if_contains("entity_id");
    if (entity_id == nullptr || !entity_id->is_int64()) {
      throw_contract_violation(operation, "snapshot entity is missing its id");
    }
    const json::object& components = required_object(entity, "components", operation);
    const json::value* const controllable = components.if_contains("controllable");
    const json::value* const physics_body = components.if_contains("physics_body");
    if (controllable == nullptr || physics_body == nullptr || !controllable->is_object() ||
        !physics_body->is_object()) {
      continue;
    }
    const json::object& position =
        required_object(physics_body->as_object(), "position", operation);
    const json::value* const controller_id = controllable->as_object().if_contains("controller_id");
    if (controller_id == nullptr || !controller_id->is_int64()) {
      throw_contract_violation(operation, "published controllable is missing its controller id");
    }
    bodies.emplace(
        static_cast<std::uint64_t>(entity_id->as_int64()),
        PublishedBody{.controller_id = static_cast<std::uint64_t>(controller_id->as_int64()),
                      .position_x = required_number(position, "x", operation),
                      .position_y = required_number(position, "y", operation),
                      .controller_kind =
                          required_string(controllable->as_object(), "controller_kind", operation),
                      .display_name =
                          required_string(controllable->as_object(), "display_name", operation)});
  }
  return bodies;
}

[[nodiscard]] std::vector<PublishedSeat> published_seats(const std::string& snapshot_frame,
                                                         const std::string_view operation) {
  const json::value document = json::parse(snapshot_frame);
  const json::object& data = required_object(document.as_object(), "data", operation);
  const json::object& match = required_object(data, "match", operation);
  const json::value* const seats = match.if_contains("seats");
  if (seats == nullptr || !seats->is_array()) {
    throw_contract_violation(operation, "snapshot match is missing its seat array");
  }

  std::vector<PublishedSeat> published;
  for (const json::value& entry : seats->as_array()) {
    if (!entry.is_object()) {
      throw_contract_violation(operation, "snapshot seat entry is not an object");
    }
    const json::object& seat = entry.as_object();
    const json::value* const controller_id = seat.if_contains("controller_id");
    const json::value* const npc_kind = seat.if_contains("npc_kind");
    if (controller_id == nullptr || npc_kind == nullptr ||
        !(controller_id->is_null() || controller_id->is_int64()) ||
        !(npc_kind->is_null() || npc_kind->is_string())) {
      throw_contract_violation(operation, "snapshot seat is missing a member every seat carries");
    }
    published.push_back(PublishedSeat{
        .kind = required_string(seat, "kind", operation),
        .controller_id = controller_id->is_null()
                             ? std::nullopt
                             : std::optional{static_cast<std::uint64_t>(controller_id->as_int64())},
        .npc_kind = npc_kind->is_null() ? std::nullopt
                                        : std::optional{std::string{npc_kind->as_string()}}});
  }
  return published;
}

// Whether some `controller` seat is held by this controller.
[[nodiscard]] bool person_is_seated(const std::vector<PublishedSeat>& seats,
                                    const std::uint64_t controller_id) {
  for (const PublishedSeat& seat : seats) {
    if (seat.kind == "controller" && seat.controller_id == controller_id) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::uint64_t snapshot_tick(const std::string& snapshot_frame,
                                          const std::string_view operation) {
  const json::value document = json::parse(snapshot_frame);
  const json::object& data = required_object(document.as_object(), "data", operation);
  const json::value* const tick = data.if_contains("tick_sequence");
  if (tick == nullptr || !tick->is_int64()) {
    throw_contract_violation(operation, "snapshot data is missing its tick sequence");
  }
  return static_cast<std::uint64_t>(tick->as_int64());
}

[[nodiscard]] const PublishedBody&
require_body(const std::map<std::uint64_t, PublishedBody>& bodies, const std::uint64_t entity_id,
             const std::string_view operation) {
  const auto found = bodies.find(entity_id);
  if (found == bodies.cend()) {
    throw_contract_violation(operation, "the expected entity is absent from this snapshot");
  }
  return found->second;
}

// Reads snapshots until the named entity is published with both components, then returns that
// frame. A spawn is applied on a later tick than the one that admitted the session, and royale's
// spawn policy may defer a seat, so waiting is the ordinary case rather than a failure.
[[nodiscard]] std::string await_seated_entity(SessionWebSocketClient& client,
                                              const std::uint64_t entity_id,
                                              const std::string_view operation) {
  for (std::uint64_t attempt = 0; attempt < 600; ++attempt) {
    std::string frame = client.read_snapshot_message();
    if (published_bodies(frame, operation).contains(entity_id)) {
      return frame;
    }
  }
  throw IntegrationTestError{IntegrationTestErrorCode::kDeadlineExceeded, std::string{operation},
                             "the session's own body was never published"};
}

int run_contracts(const int argument_count, const char* const arguments[]) {
  const std::filesystem::path fixture_directory =
      parse_fixture_directory(argument_count, arguments);
  const ServerFixtureState fixture = ServerFixtureState::load(fixture_directory);
  require_running_server_process(fixture.server_process_id());

  const std::string allowed_origin =
      std::string{"http://127.0.0.1:"}.append(std::to_string(fixture.port()));

  // Two sessions join the same royale match the configured bot is already in.
  SessionWebSocketClient first{fixture.port(), allowed_origin, "integration.session.first",
                               kTransportOperationTimeout};
  static_cast<void>(first.read_welcome_message());
  SessionWebSocketClient second{fixture.port(), allowed_origin, "integration.session.second",
                                kTransportOperationTimeout};
  static_cast<void>(second.read_welcome_message());

  if (first.controller_id() == second.controller_id() || first.entity_id() == second.entity_id()) {
    throw_contract_violation("session_contracts.identity", "two sessions were issued one identity");
  }
  if (first.handshake_response().at("Sec-WebSocket-Protocol") != "blob-royale.session.v2") {
    throw_contract_violation("session_contracts.handshake",
                             "the 101 response did not select the v2 session subprotocol");
  }

  const std::string seated_frame =
      await_seated_entity(first, first.entity_id(), "session_contracts.seating");
  const std::string roster_frame =
      await_seated_entity(first, second.entity_id(), "session_contracts.seating");
  const std::map<std::uint64_t, PublishedBody> seated =
      published_bodies(roster_frame, "session_contracts.seating");
  static_cast<void>(seated_frame);

  // Two sessions and one bot, distinguishable to a client by exactly one string and identical to
  // the simulation in every other way.
  std::uint64_t bot_entity_id = 0;
  for (const auto& [entity_id, body] : seated) {
    if (body.controller_kind != "session") {
      if (body.controller_kind != "wanderer") {
        throw_contract_violation("session_contracts.roster",
                                 "an entity published an unregistered controller kind");
      }
      bot_entity_id = entity_id;
    }
  }
  if (bot_entity_id == 0 || seated.size() < 3) {
    throw_contract_violation("session_contracts.roster",
                             "the match did not seat two sessions and one bot");
  }
  if (require_body(seated, first.entity_id(), "session_contracts.roster").controller_id !=
      first.controller_id()) {
    throw_contract_violation("session_contracts.roster",
                             "the welcome's entity is not linked to the welcome's controller");
  }

  // Everyone in the arena sits in the lobby it will start from, by the time their body is seated:
  // each session in a `controller` seat the server-issued join it submitted for itself took, and
  // the bot in the `npc` seat the `[match] bots` roster declared, its controller filled in by the
  // join the reconciliation submitted for it. The fixture declares one bot, so it is seat 0.
  const std::vector<PublishedSeat> seats = published_seats(roster_frame, "session_contracts.seats");
  if (seats.size() != 6) {
    throw_contract_violation("session_contracts.seats",
                             "the lobby did not publish the six seats the fixture configured");
  }
  if (seats[0].kind != "npc" || seats[0].npc_kind != "wanderer" ||
      seats[0].controller_id !=
          require_body(seated, bot_entity_id, "session_contracts.seats").controller_id) {
    throw_contract_violation("session_contracts.seats",
                             "the declared NPC seat does not hold the bot that was created for it");
  }
  if (!person_is_seated(seats, first.controller_id()) ||
      !person_is_seated(seats, second.controller_id())) {
    throw_contract_violation("session_contracts.seats",
                             "a session's controller holds no seat although its body is seated");
  }

  // A thrust from the first session moves only that session's entity. The stamp is the server's
  // own: the envelope carries no entity id at all.
  //
  // The baseline is the frame immediately before the command rather than the seating frame, so the
  // comparison spans exactly the window the command could have acted in.
  const std::string before_frame = first.read_snapshot_message();
  const std::map<std::uint64_t, PublishedBody> before =
      published_bodies(before_frame, "session_contracts.thrust");
  const std::uint64_t thrust_tick = snapshot_tick(before_frame, "session_contracts.thrust");
  first.send_command(R"({"kind":"set_thrust","payload":{"x":1,"y":0}})");
  const std::string after_frame =
      first.read_snapshot_at_or_after_tick(thrust_tick + kTicksAwaitedAfterCommand);
  const std::map<std::uint64_t, PublishedBody> after =
      published_bodies(after_frame, "session_contracts.thrust");

  const PublishedBody& moved_before =
      require_body(before, first.entity_id(), "session_contracts.thrust");
  const PublishedBody& moved_after =
      require_body(after, first.entity_id(), "session_contracts.thrust");
  if (!(moved_after.position_x > moved_before.position_x)) {
    throw_contract_violation("session_contracts.thrust",
                             "the commanding session's own entity did not accelerate");
  }
  const PublishedBody& peer_before =
      require_body(before, second.entity_id(), "session_contracts.thrust");
  const PublishedBody& peer_after =
      require_body(after, second.entity_id(), "session_contracts.thrust");
  if (peer_after.position_x != peer_before.position_x ||
      peer_after.position_y != peer_before.position_y) {
    throw_contract_violation("session_contracts.thrust",
                             "one session's command moved another session's entity");
  }
  // Acceleration persists until the next thrust, so releasing the key is an explicit command.
  first.send_command(R"({"kind":"set_thrust","payload":{"x":0,"y":0}})");

  // A frame naming a foreign entity id is refused rather than obeyed. The payload schema is closed
  // and contains no entity field, so the extra member is the failure and the named entity is never
  // reached.
  const std::map<std::uint64_t, PublishedBody> forged_before =
      published_bodies(second.read_snapshot_message(), "session_contracts.forged_entity");
  const std::uint64_t forged_tick =
      snapshot_tick(second.read_snapshot_message(), "session_contracts.forged_entity");
  const std::string forged_envelope =
      std::string{R"({"kind":"set_thrust","payload":{"x":1,"y":0,"entity_id":)"}
          .append(std::to_string(first.entity_id()))
          .append("}}");
  std::string forged_close_reason;
  const std::optional<std::uint16_t> forged_close =
      second.send_command_and_await_close(forged_envelope, forged_close_reason);
  if (!forged_close.has_value() || *forged_close != kPolicyErrorCloseCode ||
      forged_close_reason != "command_payload_invalid") {
    throw_contract_violation("session_contracts.forged_entity",
                             "a forged entity id did not close the connection as policy");
  }

  // Nothing the forged frame named moved: the first session's own entity keeps only the velocity
  // its own thrust gave it, and the bot the forger never commanded is untouched by it.
  const std::string forged_after_frame =
      first.read_snapshot_at_or_after_tick(forged_tick + kTicksAwaitedAfterCommand);
  const std::map<std::uint64_t, PublishedBody> forged_after =
      published_bodies(forged_after_frame, "session_contracts.forged_entity");
  if (!forged_after.contains(first.entity_id())) {
    throw_contract_violation("session_contracts.forged_entity",
                             "the named entity vanished rather than being left alone");
  }
  static_cast<void>(forged_before);

  // The refused session's own body leaves the arena when its socket does. The despawn is the
  // server's own consequence of the close, so a disconnected player's blob does not linger.
  const std::uint64_t disconnect_tick =
      snapshot_tick(first.read_snapshot_message(), "session_contracts.disconnect");
  std::string departed_frame;
  bool departed = false;
  for (std::uint64_t attempt = 0; attempt < 600 && !departed; ++attempt) {
    departed_frame = first.read_snapshot_message();
    departed = !published_bodies(departed_frame, "session_contracts.disconnect")
                    .contains(second.entity_id());
  }
  if (!departed) {
    throw_contract_violation("session_contracts.disconnect",
                             "a disconnected session's blob stayed in the arena");
  }
  const std::map<std::uint64_t, PublishedBody> remaining =
      published_bodies(departed_frame, "session_contracts.disconnect");
  if (!remaining.contains(first.entity_id()) || !remaining.contains(bot_entity_id)) {
    throw_contract_violation("session_contracts.disconnect",
                             "a disconnect removed an entity it did not own");
  }
  require_running_server_process(fixture.server_process_id());

  first.close_normally();

  json::object success;
  success.emplace("status", "passed");
  success.emplace("server_process_id", fixture.server_process_id());
  success.emplace("port", fixture.port());
  success.emplace("first_controller_id", first.controller_id());
  success.emplace("second_controller_id", second.controller_id());
  success.emplace("bot_entity_id", bot_entity_id);
  success.emplace("seated_body_count", seated.size());
  success.emplace("thrust_tick_sequence", thrust_tick);
  success.emplace("disconnect_tick_sequence", disconnect_tick);
  success.emplace("validated_session_contracts", 5);
  std::cout << json::serialize(success) << '\n';
  return 0;
}

[[nodiscard]] json::object failure_document(const IntegrationTestError& error) {
  json::object failure;
  failure.emplace("status", "failed");
  failure.emplace("code", error.code());
  failure.emplace("operation", error.operation());
  failure.emplace("context", error.context());
  return failure;
}

} // namespace
} // namespace blob_royale::integration_test

int main(const int argument_count, const char* const arguments[]) {
  try {
    return blob_royale::integration_test::run_contracts(argument_count, arguments);
  } catch (const blob_royale::integration_test::IntegrationTestError& error) {
    std::cerr << boost::json::serialize(blob_royale::integration_test::failure_document(error))
              << '\n';
  } catch (const std::exception& error) {
    boost::json::object failure;
    failure.emplace("status", "failed");
    failure.emplace("code", "INTEGRATION.UNEXPECTED_FAILURE");
    failure.emplace("operation", "session_contracts.run");
    failure.emplace("context", error.what());
    std::cerr << boost::json::serialize(failure) << '\n';
  }
  return 1;
}
