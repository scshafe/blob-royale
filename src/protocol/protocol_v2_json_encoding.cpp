#include "protocol_v2_json_encoding.hpp"

#include "bounded_json_serialization.hpp"
#include "command_wire_kind.hpp"
#include "component_encoding.hpp"
#include "component_encoding_registry.hpp"
#include "http_error.hpp"
#include "mode_state_wire_encoding.hpp"
#include "protocol_encoding_error.hpp"
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
#include <utility>
#include <vector>

namespace blob_royale::protocol {
namespace {

namespace json = boost::json;
namespace simulation = blob_royale::simulation;

static_assert(kSnapshotFrameV2MaximumByteCount == kSnapshotFrameMaximumByteCount,
              "protocol v2 inherits v1's unchanged 2 MiB frame ceiling");
static_assert(kSnapshotEntityLimit <= simulation::kMaximumEntityCount,
              "a published entity is a world seat, so the wire bound cannot exceed the seat count");

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

// The declared kinds' positions sorted by kind name, because `docs/protocol/v2.md`
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
  metadata.emplace("protocol_version", kProtocolV2Version);
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
                                    " exceeds the accepted protocol v2 limit " +
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

[[nodiscard]] json::object encode_mode_state(const simulation::MatchSnapshot& match) {
  json::object value;
  JsonComponentObjectSink sink{value};
  encode_mode_state_value(match.mode_state(), sink);

  json::object encoded;
  encoded.reserve(2);
  encoded.emplace("schema_id", mode_state_wire_schema_id_of(match.mode_state()));
  encoded.emplace("value", std::move(value));
  return encoded;
}

[[nodiscard]] json::object encode_match(const simulation::MatchSnapshot& match) {
  if (!is_accepted_kind_name(match.mode_name())) {
    throw ProtocolEncodingError{ProtocolEncodingErrorCode::kMatchModeNameInvalid,
                                "snapshot_message.data.match.mode",
                                "mode name must match the accepted lower snake case kind grammar"};
  }
  // `phase_started_tick` is deliberately **not** validated as a publishable tick.
  // `match-data.schema.json` types it as `phase_start_tick`, which admits zero, because zero is the
  // truthful value for "no transition has been committed yet": a match loaded into `lobby` holds
  // `TickSequence::zero()` for the whole lobby, and every snapshot of that lobby is a legitimate
  // frame (`docs/protocol/v2.md` § "Field dictionary and invariants").

  json::object encoded;
  encoded.reserve(6);
  encoded.emplace("mode", match.mode_name());
  encoded.emplace("phase", simulation::match_phase_name(match.phase()));
  encoded.emplace("phase_started_tick", match.phase_started_tick().value());
  encoded.emplace("outcome", encode_outcome(match.outcome()));
  encoded.emplace("placements", encode_placements(match));
  encoded.emplace("mode_state", encode_mode_state(match));
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

  json::array accepted_command_kinds;
  accepted_command_kinds.reserve(kV2ClientCommandKindNames.size());
  for (const simulation::CommandKind kind : simulation::kCommandKinds) {
    const std::optional<std::string_view> wire_name = client_command_wire_name(kind);
    if (wire_name.has_value() && welcome.accepted_command_kinds().contains(kind)) {
      accepted_command_kinds.emplace_back(*wire_name);
    }
  }

  json::object data;
  data.reserve(6);
  data.emplace("entity_id", welcome.entity().value());
  data.emplace("controller_id", welcome.controller().value());
  data.emplace("display_name", welcome.display_name());
  data.emplace("mode", welcome.mode_name());
  data.emplace("map", welcome.map_name());
  data.emplace("accepted_command_kinds", std::move(accepted_command_kinds));

  json::object envelope = encode_envelope_with_data(
      json::value(std::move(data)), encode_message_metadata(kWelcomeMessageSchemaId, request_id,
                                                            kWelcomeMessageSequence, sent_at_utc));
  return serialize_bounded_json(json::value(std::move(envelope)), output_byte_limit,
                                kSnapshotFrameV2MaximumByteCount, "welcome_message.encoding");
}

std::string encode_snapshot_message_v2(const simulation::WorldSnapshot& snapshot,
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
            " exceeds the accepted protocol v2 limit " + std::to_string(kSnapshotEntityLimit)};
  }
  validate_ascending_unique_entities(entities, "snapshot_message.data.entities.entity_id");

  std::array<std::size_t, kKindCount> cursors{};
  json::array encoded_entities;
  encoded_entities.reserve(entities.size());
  for (const simulation::EntityId entity : entities) {
    encoded_entities.emplace_back(encode_entity(snapshot, entity, directory, cursors));
  }

  json::object data;
  data.reserve(3);
  data.emplace("tick_sequence", snapshot.tick_sequence().value());
  data.emplace("entities", std::move(encoded_entities));
  data.emplace("match", encode_match(snapshot.match()));

  json::object envelope = encode_envelope_with_data(
      json::value(std::move(data)), encode_message_metadata(kSnapshotMessageV2SchemaId, request_id,
                                                            message_sequence, sent_at_utc));
  return serialize_bounded_json(json::value(std::move(envelope)), output_byte_limit,
                                kSnapshotFrameV2MaximumByteCount, "snapshot_message_v2.encoding");
}

std::string encode_error_response_v2(const V2HttpError& error, const RequestId& request_id,
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

  json::object encoded_error;
  encoded_error.reserve(4);
  encoded_error.emplace("code", error.code());
  encoded_error.emplace("message", error.message());
  encoded_error.emplace("retryable", error.retryable());
  encoded_error.emplace("details", std::move(details));

  json::object metadata;
  metadata.reserve(3);
  metadata.emplace("protocol_version", kProtocolV2Version);
  metadata.emplace("schema_id", kErrorResponseV2SchemaId);
  metadata.emplace("request_id", request_id.value());

  json::object envelope;
  envelope.reserve(3);
  envelope.emplace("data", nullptr);
  envelope.emplace("error", std::move(encoded_error));
  envelope.emplace("meta", std::move(metadata));
  return serialize_bounded_json(json::value(std::move(envelope)), output_byte_limit,
                                kHttpJsonResponseMaximumByteCount, "error_response_v2.encoding");
}

} // namespace blob_royale::protocol
