#include "protocol_v3_json_encoding.hpp"

#include "bounded_json_serialization.hpp"
#include "command_wire_kind.hpp"
#include "component_encoding.hpp"
#include "component_encoding_registry.hpp"
#include "http_error.hpp"
#include "mode_state_wire_encoding.hpp"
#include "protocol_encoding_error.hpp"
#include "random_draw_counts_encoding.hpp"
#include "utc_timestamp.hpp"

#include "component_join.hpp"
#include "component_kind_name.hpp"
#include "component_registry.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "entity_id.hpp"
#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "match_snapshot.hpp"
#include "physics_body.hpp"
#include "seat_roster.hpp"
#include "simulation_limits.hpp"
#include "team_id.hpp"
#include "tick_sequence.hpp"
#include "world_snapshot.hpp"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::protocol {
namespace {

namespace json = boost::json;
namespace simulation = blob_royale::simulation;

static_assert(kSnapshotFrameV3MaximumByteCount == kSnapshotFrameMaximumByteCount,
              "protocol v3 inherits v1's unchanged 2 MiB frame ceiling");
static_assert(kSnapshotEntityLimit <= simulation::kMaximumEntityCount,
              "a published entity is a world entity slot, so the wire bound cannot exceed the "
              "entity slot count");

// The Boost.JSON realization of the sink every per-kind component encoder writes through. It is the
// only place a component value meets a JSON type, which is what keeps `ComponentWireEncoding` free
// of Boost and therefore includable from a test.
class JsonComponentObjectSink final : public ComponentObjectSink {
public:
  explicit JsonComponentObjectSink(json::object& target) noexcept : target_(&target) {}

  void set_number(const std::string_view member_name, const double value) override {
    target_->emplace(member_name, encode_json_number(value));
  }
  void set_unsigned(const std::string_view member_name, const std::uint64_t value) override {
    target_->emplace(member_name, value);
  }
  void set_signed(const std::string_view member_name, const std::int64_t value) override {
    target_->emplace(member_name, value);
  }
  void set_boolean(const std::string_view member_name, const bool value) override {
    target_->emplace(member_name, value);
  }
  void set_string(const std::string_view member_name, const std::string_view value) override {
    target_->emplace(member_name, value);
  }
  void set_vector(const std::string_view member_name, const double x, const double y) override {
    json::object vector;
    vector.reserve(2);
    vector.emplace("x", encode_json_number(x));
    vector.emplace("y", encode_json_number(y));
    target_->emplace(member_name, std::move(vector));
  }

  void set_object_array(
      const std::string_view member_name, const std::size_t count,
      const std::function<void(std::size_t, ComponentObjectSink&)>& encode_entry) override {
    json::array entries;
    entries.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      json::object entry;
      JsonComponentObjectSink entry_sink{entry};
      encode_entry(index, entry_sink);
      entries.emplace_back(std::move(entry));
    }
    target_->emplace(member_name, std::move(entries));
  }

private:
  json::object* target_;
};

// One registered kind's contribution to one entity, driven by an ascending cursor into that kind's
// published span.
//
// The cursor is what makes the whole entity walk one forward pass per store rather than a binary
// search per (entity, kind) pair: entities are visited in ascending order and each store is
// ascending, so a kind's cursor only ever moves forward across the whole snapshot.
using KindEmitter = void (*)(const simulation::WorldSnapshot&, std::size_t&, simulation::EntityId,
                             const ComponentEncodingContext&, json::object&);

template <typename Component>
void emit_component_of_kind(const simulation::WorldSnapshot& snapshot, std::size_t& cursor,
                            const simulation::EntityId entity,
                            const ComponentEncodingContext& context, json::object& components) {
  const std::span<const typename simulation::ComponentStore<Component>::Entry> entries =
      snapshot.components<Component>();
  while (cursor < entries.size() && entries[cursor].entity < entity) {
    ++cursor;
  }
  if (cursor >= entries.size() || entries[cursor].entity != entity) {
    return;
  }

  json::object encoded;
  JsonComponentObjectSink sink{encoded};
  ComponentWireEncoding<Component>::encode(entries[cursor].value, context, sink);
  components.emplace(simulation::component_kind_name<Component>, std::move(encoded));
  ++cursor;
}

inline constexpr std::size_t kKindCount = simulation::ComponentRegistry::kKindCount;

// The registry's kinds as a name and an emitter each, in the registry's declared order.
struct DeclaredKindTable final {
  std::array<std::string_view, kKindCount> names{};
  std::array<KindEmitter, kKindCount> emitters{};
  std::size_t next = 0;

  template <typename Component> constexpr void operator()() noexcept {
    names[next] = simulation::component_kind_name<Component>;
    emitters[next] = &emit_component_of_kind<Component>;
    ++next;
  }
};

[[nodiscard]] constexpr DeclaredKindTable declared_kinds() noexcept {
  DeclaredKindTable table;
  simulation::ComponentRegistry::for_each_kind(table);
  return table;
}

inline constexpr DeclaredKindTable kDeclaredKinds = declared_kinds();

// The declared kinds' positions sorted by kind name, because `docs/protocol/v3.md`
// § "Object member order" encodes component keys in ascending kind-name order while
// `ComponentRegistry` is ordered by declaration. Deriving the permutation from the declared names
// means a kind added anywhere in the registry lands in the right place on the wire with no second
// list to maintain.
[[nodiscard]] constexpr std::array<std::size_t, kKindCount> kind_name_order() noexcept {
  std::array<std::size_t, kKindCount> order{};
  for (std::size_t position = 0; position < kKindCount; ++position) {
    order[position] = position;
  }
  for (std::size_t outer = 1; outer < kKindCount; ++outer) {
    const std::size_t candidate = order[outer];
    std::size_t inner = outer;
    while (inner > 0 && kDeclaredKinds.names[order[inner - 1]] > kDeclaredKinds.names[candidate]) {
      order[inner] = order[inner - 1];
      --inner;
    }
    order[inner] = candidate;
  }
  return order;
}

inline constexpr std::array<std::size_t, kKindCount> kKindNameOrder = kind_name_order();

// The wire name of one committed outcome. The world calls the undecided arm `undecided` because
// that is what it means to a match; the wire calls it `none` because that is what it means to a
// client reading a result that is not yet a result. Mapping here keeps both names accurate, exactly
// as the mode-state schema ids are mapped rather than renamed.
[[nodiscard]] constexpr std::string_view
outcome_wire_kind_name(const simulation::MatchOutcomeKind kind) noexcept {
  switch (kind) {
  case simulation::MatchOutcomeKind::kUndecided:
    return "none";
  case simulation::MatchOutcomeKind::kWonByEntity:
    return "won_by_entity";
  case simulation::MatchOutcomeKind::kWonByTeam:
    return "won_by_team";
  case simulation::MatchOutcomeKind::kDrawn:
    return "drawn";
  }
  return "outcome_kind_invalid";
}

void validate_message_sequence(const std::uint64_t message_sequence, const std::uint64_t minimum,
                               const std::string_view context) {
  if (message_sequence < minimum || message_sequence > kMaximumSafeInteger) {
    throw ProtocolEncodingError{ProtocolEncodingErrorCode::kMessageSequenceOutOfRange,
                                std::string{context},
                                "message sequence must be in the inclusive range " +
                                    std::to_string(minimum) + " to 2^53-1"};
  }
}

void validate_timestamp(const std::string_view sent_at_utc, const std::string_view context) {
  if (!is_accepted_utc_timestamp(sent_at_utc)) {
    throw ProtocolEncodingError{ProtocolEncodingErrorCode::kTimestampInvalid, std::string{context},
                                "timestamp must be a bounded RFC 3339 UTC value ending in Z"};
  }
}

void validate_publishable_tick(const simulation::TickSequence tick,
                               const std::string_view context) {
  if (tick.value() == 0) {
    throw ProtocolEncodingError{ProtocolEncodingErrorCode::kSnapshotTickOutOfRange,
                                std::string{context},
                                "tick sequence must be in the inclusive range 1 to 2^53-1"};
  }
}

[[nodiscard]] json::object encode_message_metadata(const std::string_view schema_id,
                                                   const RequestId& request_id,
                                                   const std::uint64_t message_sequence,
                                                   const std::string_view sent_at_utc) {
  json::object metadata;
  metadata.reserve(5);
  metadata.emplace("protocol_version", kProtocolV3Version);
  metadata.emplace("schema_id", schema_id);
  metadata.emplace("request_id", request_id.value());
  metadata.emplace("message_sequence", message_sequence);
  metadata.emplace("sent_at_utc", sent_at_utc);
  return metadata;
}

[[nodiscard]] json::object encode_envelope_with_data(json::value data, json::object metadata) {
  json::object envelope;
  envelope.reserve(3);
  envelope.emplace("data", std::move(data));
  envelope.emplace("error", nullptr);
  envelope.emplace("meta", std::move(metadata));
  return envelope;
}

// canonical: terrain_wire_encoding -- publishes authored geometry in declared order, once per
// welcome. TerrainDefinition has already validated every scalar, name, and aggregate bound.
[[nodiscard]] json::object encode_terrain(const simulation::TerrainDefinition& terrain) {
  json::object bounds;
  bounds.emplace("width_world_units", encode_json_number(terrain.bounds().width()));
  bounds.emplace("height_world_units", encode_json_number(terrain.bounds().height()));
  const auto encode_point = [](const simulation::Vector2& point) {
    json::object encoded;
    encoded.emplace("x", encode_json_number(point.x()));
    encoded.emplace("y", encode_json_number(point.y()));
    return encoded;
  };
  json::array corridors;
  corridors.reserve(terrain.corridors().size());
  for (const simulation::TerrainCorridor& corridor : terrain.corridors()) {
    json::array points;
    points.reserve(corridor.points().size());
    for (const simulation::Vector2& point : corridor.points()) {
      points.emplace_back(encode_point(point));
    }
    json::object encoded;
    encoded.emplace("name", corridor.name());
    encoded.emplace("half_width", encode_json_number(corridor.half_width()));
    encoded.emplace("points", std::move(points));
    corridors.emplace_back(std::move(encoded));
  }
  json::array holes;
  holes.reserve(terrain.holes().size());
  for (const simulation::TerrainHole& hole : terrain.holes()) {
    json::object encoded;
    encoded.emplace("name", hole.name());
    encoded.emplace("center", encode_point(hole.center()));
    encoded.emplace("radius", encode_json_number(hole.radius()));
    holes.emplace_back(std::move(encoded));
  }
  json::object encoded;
  encoded.emplace("bounds", std::move(bounds));
  encoded.emplace("ground",
                  terrain.ground() == simulation::TerrainGround::kSolid ? "solid" : "corridors");
  encoded.emplace("corridors", std::move(corridors));
  encoded.emplace("holes", std::move(holes));
  return encoded;
}

[[nodiscard]] json::object encode_entity(const simulation::WorldSnapshot& snapshot,
                                         const simulation::EntityId entity,
                                         const ControllerDirectoryView& directory,
                                         std::array<std::size_t, kKindCount>& cursors) {
  const ComponentEncodingContext context{entity, &directory};

  json::object components;
  components.reserve(kKindCount);
  for (const std::size_t declared_position : kKindNameOrder) {
    kDeclaredKinds.emitters[declared_position](snapshot, cursors[declared_position], entity,
                                               context, components);
  }

  json::object encoded;
  encoded.reserve(2);
  encoded.emplace("entity_id", entity.value());
  encoded.emplace("components", std::move(components));
  return encoded;
}

[[nodiscard]] json::object encode_outcome(const simulation::MatchOutcome& outcome) {
  json::object encoded;
  encoded.reserve(3);
  encoded.emplace("kind", outcome_wire_kind_name(outcome.kind()));
  if (const std::optional<simulation::EntityId> winner = outcome.winning_entity();
      winner.has_value()) {
    encoded.emplace("winner_entity_id", winner->value());
  } else {
    encoded.emplace("winner_entity_id", nullptr);
  }
  if (const std::optional<simulation::TeamId> winner = outcome.winning_team(); winner.has_value()) {
    encoded.emplace("winner_team_id", winner->value());
  } else {
    encoded.emplace("winner_team_id", nullptr);
  }
  return encoded;
}

[[nodiscard]] json::array encode_placements(const simulation::MatchSnapshot& match) {
  std::vector<ModeStatePlacement> ranking;
  append_mode_state_placements(match.mode_state(), ranking);
  if (ranking.size() > kMatchPlacementLimit) {
    throw ProtocolEncodingError{ProtocolEncodingErrorCode::kPlacementLimitExceeded,
                                "snapshot_message.data.match.placements",
                                "placement count " + std::to_string(ranking.size()) +
                                    " exceeds the accepted protocol v3 limit " +
                                    std::to_string(kMatchPlacementLimit)};
  }

  json::array encoded;
  encoded.reserve(ranking.size());
  for (const ModeStatePlacement& placement : ranking) {
    validate_publishable_tick(placement.eliminated_tick,
                              "snapshot_message.data.match.placements.eliminated_tick");

    json::object entry;
    entry.reserve(4);
    entry.emplace("entity_id", placement.entity.value());
    entry.emplace("controller_id", placement.controller.value());
    entry.emplace("placement", placement.placement);
    entry.emplace("eliminated_tick", placement.eliminated_tick.value());
    encoded.emplace_back(std::move(entry));
  }
  return encoded;
}

[[nodiscard]] json::object encode_mode_state(const simulation::MatchSnapshot& match,
                                             const simulation::TerrainDefinition& terrain) {
  json::object value;
  JsonComponentObjectSink sink{value};
  encode_mode_state_value(match.mode_state(), terrain, sink);

  json::object encoded;
  encoded.reserve(2);
  encoded.emplace("schema_id", mode_state_wire_schema_id_of(match.mode_state()));
  encoded.emplace("value", std::move(value));
  return encoded;
}

// The wire name of one seat's arm. Closed, and mirrored by `common.schema.json#/$defs/seat_kind`.
[[nodiscard]] constexpr std::string_view
seat_wire_kind_name(const simulation::Seat& seat) noexcept {
  return std::visit(
      []<typename SeatType>(const SeatType&) -> std::string_view {
        if constexpr (std::is_same_v<SeatType, simulation::ControllerSeat>) {
          return "controller";
        } else if constexpr (std::is_same_v<SeatType, simulation::NpcSeat>) {
          return "npc";
        } else {
          return "empty";
        }
      },
      seat);
}

// One published seat.
//
// **Every member is always present and the ones that do not apply are null**, which is the shape
// `outcome` already uses and for the same reason: a decoder reads one object with a known member
// set rather than branching on which keys exist, and a client can never mistake an absent member
// for a meaningful one (`match-data.schema.json`, outcome).
//
// `controller_id` is non-null on a `controller` seat and on an `npc` seat **whose bot the runtime
// has already created**; it is null on an `npc` seat still waiting for one, which is the state a
// client renders as joining (`src/simulation/seat_roster.hpp`, NpcSeat).
[[nodiscard]] json::object encode_seat(const simulation::Seat& seat) {
  json::object encoded;
  encoded.reserve(3);
  encoded.emplace("kind", seat_wire_kind_name(seat));

  std::optional<simulation::ControllerId> controller;
  std::optional<std::string_view> npc_kind;
  if (const auto* held = std::get_if<simulation::ControllerSeat>(&seat); held != nullptr) {
    controller = held->controller;
  } else if (const auto* declared = std::get_if<simulation::NpcSeat>(&seat); declared != nullptr) {
    controller = declared->controller;
    npc_kind = declared->kind.value();
  }

  if (controller.has_value()) {
    encoded.emplace("controller_id", controller->value());
  } else {
    encoded.emplace("controller_id", nullptr);
  }
  if (npc_kind.has_value()) {
    encoded.emplace("npc_kind", *npc_kind);
  } else {
    encoded.emplace("npc_kind", nullptr);
  }
  return encoded;
}

// The lobby, in index order, which is the order a client renders it and the order every tie-break
// over seats resolves in (`src/simulation/seat_roster.hpp`).
//
// A world that declared no lobby publishes an empty array rather than a null, so a client has one
// code path; that is what `sandbox` publishes, and an empty roster is never full, so no client can
// read it as a startable match.
[[nodiscard]] json::array encode_seats(const simulation::SeatRoster& seats) {
  if (seats.seat_count() > kLobbySeatCountMaximum) {
    throw ProtocolEncodingError{
        ProtocolEncodingErrorCode::kSeatLimitExceeded, "snapshot_message.data.match.seats",
        "seat count " + std::to_string(seats.seat_count()) +
            " exceeds the accepted protocol v3 limit " + std::to_string(kLobbySeatCountMaximum)};
  }
  json::array encoded;
  encoded.reserve(seats.seat_count());
  for (const simulation::Seat& seat : seats.seats()) {
    encoded.emplace_back(encode_seat(seat));
  }
  return encoded;
}

[[nodiscard]] json::object encode_match(const simulation::MatchSnapshot& match,
                                        const simulation::TerrainDefinition& terrain) {
  if (!is_accepted_kind_name(match.mode_name())) {
    throw ProtocolEncodingError{ProtocolEncodingErrorCode::kMatchModeNameInvalid,
                                "snapshot_message.data.match.mode",
                                "mode name must match the accepted lower snake case kind grammar"};
  }
  // `phase_started_tick` is deliberately **not** validated as a publishable tick.
  // `match-data.schema.json` types it as `phase_start_tick`, which admits zero, because zero is the
  // truthful value for "no transition has been committed yet": a match loaded into `lobby` holds
  // `TickSequence::zero()` for the whole lobby, and every snapshot of that lobby is a legitimate
  // frame (`docs/protocol/v3.md` § "Field dictionary and invariants").

  json::object encoded;
  encoded.reserve(8);
  encoded.emplace("mode", match.mode_name());
  encoded.emplace("phase", simulation::match_phase_name(match.phase()));
  encoded.emplace("phase_started_tick", match.phase_started_tick().value());
  // The lobby sits with the lifecycle header it gates rather than at the end beside `outcome` and
  // `placements`, which describe a match that has already been played.
  encoded.emplace("seats", encode_seats(match.seats()));
  encoded.emplace("start_requested", match.seats().start_requested());
  encoded.emplace("outcome", encode_outcome(match.outcome()));
  encoded.emplace("placements", encode_placements(match));
  encoded.emplace("mode_state", encode_mode_state(match, terrain));
  return encoded;
}

} // namespace

void validate_ascending_unique_entities(const std::span<const simulation::EntityId> entities,
                                        const std::string_view context) {
  for (std::size_t position = 1; position < entities.size(); ++position) {
    if (entities[position - 1] < entities[position]) {
      continue;
    }
    throw ProtocolEncodingError{
        ProtocolEncodingErrorCode::kSnapshotEntityOrderInvalid, std::string{context},
        "entity ids must be strictly ascending and distinct; " +
            std::to_string(entities[position - 1].value()) + " is followed by " +
            std::to_string(entities[position].value())};
  }
}

std::optional<simulation::EntityId>
find_controlled_entity(const simulation::WorldSnapshot& snapshot,
                       const simulation::ControllerId controller) {
  for (const simulation::ComponentStore<simulation::Controllable>::Entry& entry :
       snapshot.components<simulation::Controllable>()) {
    if (entry.value.controller_id == controller) {
      return entry.entity;
    }
  }
  return std::nullopt;
}

std::optional<simulation::EntityId>
find_controlled_body(const simulation::WorldSnapshot& snapshot,
                     const simulation::ControllerId controller) {
  std::optional<simulation::EntityId> body;
  simulation::for_each_entity_with_both<simulation::ComponentStore<simulation::Controllable>::Entry,
                                        simulation::ComponentStore<simulation::PhysicsBody>::Entry>(
      snapshot.components<simulation::Controllable>(),
      snapshot.components<simulation::PhysicsBody>(),
      [controller, &body](const simulation::EntityId entity,
                          const simulation::Controllable& controllable,
                          const simulation::PhysicsBody&) {
        if (!body.has_value() && controllable.controller_id == controller) {
          body = entity;
        }
      });
  return body;
}

std::string encode_welcome_message(const SessionWelcome& welcome, const RequestId& request_id,
                                   const std::string_view sent_at_utc,
                                   const std::size_t output_byte_limit) {
  validate_timestamp(sent_at_utc, "welcome_message.meta.sent_at_utc");

  // Walked over the **published vocabulary** rather than the simulation's kind list, so the array a
  // client reads is in the schema enum's own order however the engine happens to declare its kinds.
  // A kind the mode does not accept is absent, and a kind with no wire name cannot appear at all
  // because it has no name to appear under (`command_wire_kind.hpp`).
  json::array accepted_command_kinds;
  accepted_command_kinds.reserve(kV3ClientCommandKindNames.size());
  for (const std::string_view wire_name : kV3ClientCommandKindNames) {
    const std::optional<simulation::CommandKind> kind = client_command_kind_of_wire_name(wire_name);
    if (kind.has_value() && welcome.accepted_command_kinds().contains(*kind)) {
      accepted_command_kinds.emplace_back(wire_name);
    }
  }

  // Read from `ControllerRegistry` by the composition root and validated by `SessionWelcome`, so
  // this loop publishes rather than decides. Registry order is preserved for the reason
  // `session_welcome.hpp` gives: it is somebody's deliberate ordering of the bots.
  json::array npc_controller_kinds;
  npc_controller_kinds.reserve(welcome.npc_controller_kinds().size());
  for (const std::string& npc_controller_kind : welcome.npc_controller_kinds()) {
    npc_controller_kinds.emplace_back(npc_controller_kind);
  }

  json::object data;
  data.reserve(10);
  data.emplace("entity_id", welcome.entity().value());
  data.emplace("controller_id", welcome.controller().value());
  data.emplace("display_name", welcome.display_name());
  data.emplace("mode", welcome.mode_name());
  data.emplace("map", welcome.map_name());
  data.emplace("accepted_command_kinds", std::move(accepted_command_kinds));
  data.emplace("npc_controller_kinds", std::move(npc_controller_kinds));
  data.emplace("lobby_id", welcome.lobby_id());
  data.emplace("seat_count_maximum", welcome.seat_count_maximum());
  data.emplace("terrain", encode_terrain(welcome.terrain()));

  json::object envelope = encode_envelope_with_data(
      json::value(std::move(data)), encode_message_metadata(kWelcomeMessageSchemaId, request_id,
                                                            kWelcomeMessageSequence, sent_at_utc));
  return serialize_bounded_json(json::value(std::move(envelope)), output_byte_limit,
                                kSnapshotFrameV3MaximumByteCount, "welcome_message.encoding");
}

std::string encode_snapshot_message_v3(const simulation::WorldSnapshot& snapshot,
                                       const ControllerDirectoryView& directory,
                                       const RequestId& request_id,
                                       const std::uint64_t message_sequence,
                                       const std::string_view sent_at_utc,
                                       const std::size_t output_byte_limit) {
  validate_message_sequence(message_sequence, kFirstSnapshotMessageSequence,
                            "snapshot_message.meta.message_sequence");
  validate_timestamp(sent_at_utc, "snapshot_message.meta.sent_at_utc");
  validate_publishable_tick(snapshot.tick_sequence(), "snapshot_message.data.tick_sequence");

  const std::span<const simulation::EntityId> entities = snapshot.entities();
  if (entities.size() > kSnapshotEntityLimit) {
    throw ProtocolEncodingError{
        ProtocolEncodingErrorCode::kSnapshotEntityLimitExceeded, "snapshot_message.data.entities",
        "published entity count " + std::to_string(entities.size()) +
            " exceeds the accepted protocol v3 limit " + std::to_string(kSnapshotEntityLimit)};
  }
  validate_ascending_unique_entities(entities, "snapshot_message.data.entities.entity_id");

  std::array<std::size_t, kKindCount> cursors{};
  json::array encoded_entities;
  encoded_entities.reserve(entities.size());
  for (const simulation::EntityId entity : entities) {
    encoded_entities.emplace_back(encode_entity(snapshot, entity, directory, cursors));
  }

  json::object random_draw_counts;
  random_draw_counts.reserve(kV3RandomStreamNames.size());
  JsonComponentObjectSink random_count_sink{random_draw_counts};
  encode_random_draw_counts(snapshot.random_draw_counts(), random_count_sink);

  json::object data;
  data.reserve(4);
  data.emplace("tick_sequence", snapshot.tick_sequence().value());
  data.emplace("random_draw_counts", std::move(random_draw_counts));
  data.emplace("entities", std::move(encoded_entities));
  data.emplace("match", encode_match(snapshot.match(), snapshot.terrain()));

  json::object envelope = encode_envelope_with_data(
      json::value(std::move(data)), encode_message_metadata(kSnapshotMessageV3SchemaId, request_id,
                                                            message_sequence, sent_at_utc));
  return serialize_bounded_json(json::value(std::move(envelope)), output_byte_limit,
                                kSnapshotFrameV3MaximumByteCount, "snapshot_message_v3.encoding");
}

std::string encode_error_response_v3(const V3HttpError& error, const RequestId& request_id,
                                     const std::size_t output_byte_limit) {
  json::object details;
  details.reserve(6);
  if (const HttpError* const shared = error.shared_error(); shared != nullptr) {
    if (!shared->allowed_methods().empty()) {
      json::array methods;
      methods.reserve(shared->allowed_methods().size());
      for (const std::string_view method : shared->allowed_methods()) {
        methods.emplace_back(method);
      }
      details.emplace("allowed_methods", std::move(methods));
    }
    if (const std::optional<std::string_view> websocket_version =
            shared->expected_websocket_version();
        websocket_version.has_value()) {
      details.emplace("expected_websocket_version", *websocket_version);
    }
  }
  if (const std::optional<ForwardedClientReason> forwarded_reason = error.forwarded_client_reason();
      forwarded_reason.has_value()) {
    details.emplace("forwarded_client_reason", forwarded_client_reason_name(*forwarded_reason));
  }
  if (const HttpError* const shared = error.shared_error(); shared != nullptr) {
    if (shared->limit().has_value()) {
      details.emplace("limit", *shared->limit());
    }
    if (const std::optional<std::string_view> detail_reason = shared->reason();
        detail_reason.has_value()) {
      details.emplace("reason", *detail_reason);
    }
    if (shared->retry_after_ms().has_value()) {
      details.emplace("retry_after_ms", *shared->retry_after_ms());
    }
  }

  // A `LOBBY.FULL` or `LOBBY.UNAVAILABLE` row names the room the request named; every other row's
  // details come off the shared error above or the forwarded-client reason.
  if (error.lobby_id().has_value()) {
    details.emplace("lobby_id", *error.lobby_id());
  }
  if (error.requires_session_version_upgrade()) {
    details.emplace("required_protocol_version", kProtocolV3Version);
  }

  json::object encoded_error;
  encoded_error.reserve(4);
  encoded_error.emplace("code", error.code());
  encoded_error.emplace("message", error.message());
  encoded_error.emplace("retryable", error.retryable());
  encoded_error.emplace("details", std::move(details));

  json::object metadata;
  metadata.reserve(3);
  metadata.emplace("protocol_version", kProtocolV3Version);
  metadata.emplace("schema_id", kErrorResponseV3SchemaId);
  metadata.emplace("request_id", request_id.value());

  json::object envelope;
  envelope.reserve(3);
  envelope.emplace("data", nullptr);
  envelope.emplace("error", std::move(encoded_error));
  envelope.emplace("meta", std::move(metadata));
  return serialize_bounded_json(json::value(std::move(envelope)), output_byte_limit,
                                kHttpJsonResponseMaximumByteCount, "error_response_v3.encoding");
}

std::string encode_lobby_directory_message(const std::span<const LobbyListing> lobbies,
                                           const RequestId& request_id,
                                           const std::size_t output_byte_limit) {
  const auto require_listing = [](const bool accepted, const std::string_view context,
                                  const std::string_view detail) {
    if (!accepted) {
      throw ProtocolEncodingError{ProtocolEncodingErrorCode::kLobbyDirectoryInvalid,
                                  std::string{context}, std::string{detail}};
    }
  };
  require_listing(
      !lobbies.empty() && lobbies.size() <= kLobbyDirectoryLimit, "lobby_directory.data.lobbies",
      "a directory lists between 1 and " + std::to_string(kLobbyDirectoryLimit) + " rooms");

  json::array encoded_lobbies;
  encoded_lobbies.reserve(lobbies.size());
  for (std::size_t index = 0; index < lobbies.size(); ++index) {
    const LobbyListing& listing = lobbies[index];
    const std::string context = "lobby_directory.data.lobbies[" + std::to_string(index) + "]";
    // Rooms are numbered by position, so the id is redundant with the index and is checked
    // against it rather than trusted: a directory that disagreed with itself would send a client
    // to the wrong room.
    require_listing(listing.lobby_id == index + 1, context + ".lobby_id",
                    "rooms are numbered 1..N in order");
    require_listing(is_accepted_kind_name(listing.mode_name), context + ".mode",
                    "mode name must match the accepted lower snake case kind grammar");
    require_listing(is_accepted_map_name(listing.map_name), context + ".map",
                    "map name must match the accepted map-name grammar");
    require_listing(listing.tick_sequence <= kMaximumSafeInteger, context + ".tick_sequence",
                    "tick sequence must be at most 2^53-1");
    require_listing(listing.phase_started_tick <= listing.tick_sequence,
                    context + ".phase_started_tick",
                    "a phase cannot have started after the tick it is read at");
    require_listing(listing.seat_count <= kLobbySeatCountMaximum, context + ".seat_count",
                    "seat count must be at most " + std::to_string(kLobbySeatCountMaximum));
    require_listing(listing.seat_count_maximum >= 1 &&
                        listing.seat_count_maximum <= kLobbySeatCountMaximum,
                    context + ".seat_count_maximum",
                    "seat count maximum must be in the inclusive range 1 to " +
                        std::to_string(kLobbySeatCountMaximum));
    require_listing(listing.seat_count <= listing.seat_count_maximum, context + ".seat_count",
                    "a room cannot seat more than its map's markers");
    require_listing(listing.filled_seat_count <= listing.seat_count, context + ".filled_seat_count",
                    "filled seats cannot outnumber seats");
    require_listing(listing.npc_seat_count <= listing.seat_count, context + ".npc_seat_count",
                    "NPC seats cannot outnumber seats");
    require_listing(listing.session_count <= kLobbySeatCountMaximum, context + ".session_count",
                    "session count must be at most " + std::to_string(kLobbySeatCountMaximum));

    json::object encoded;
    encoded.reserve(12);
    encoded.emplace("lobby_id", listing.lobby_id);
    encoded.emplace("mode", listing.mode_name);
    encoded.emplace("map", listing.map_name);
    encoded.emplace("phase", simulation::match_phase_name(listing.phase));
    encoded.emplace("phase_started_tick", listing.phase_started_tick);
    encoded.emplace("tick_sequence", listing.tick_sequence);
    encoded.emplace("seat_count", listing.seat_count);
    encoded.emplace("seat_count_maximum", listing.seat_count_maximum);
    encoded.emplace("filled_seat_count", listing.filled_seat_count);
    encoded.emplace("npc_seat_count", listing.npc_seat_count);
    encoded.emplace("session_count", listing.session_count);
    encoded.emplace("healthy", listing.healthy);
    encoded_lobbies.emplace_back(std::move(encoded));
  }

  json::object data;
  data.reserve(1);
  data.emplace("lobbies", std::move(encoded_lobbies));

  // An HTTP document, so the metadata is the error envelope's three members and not a frame's five
  // (`docs/protocol/v3.md` § "The lobby directory").
  json::object metadata;
  metadata.reserve(3);
  metadata.emplace("protocol_version", kProtocolV3Version);
  metadata.emplace("schema_id", kLobbyDirectorySchemaId);
  metadata.emplace("request_id", request_id.value());

  json::object envelope;
  envelope.reserve(3);
  envelope.emplace("data", std::move(data));
  envelope.emplace("error", nullptr);
  envelope.emplace("meta", std::move(metadata));
  return serialize_bounded_json(json::value(std::move(envelope)), output_byte_limit,
                                kHttpJsonResponseMaximumByteCount, "lobby_directory.encoding");
}

} // namespace blob_royale::protocol
