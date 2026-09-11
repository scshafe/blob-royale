#ifndef BLOB_ROYALE_PROTOCOL_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENT_ENCODING_HPP

#include "controller_directory_view.hpp"
#include "entity_id.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

namespace blob_royale::protocol {

// canonical: component_object_sink -- the members one component publishes, without naming a parser.
//
// A component encoder writes members in the order `docs/protocol/v3.md` § "Object member order"
// declares, and the sink turns each into one JSON member. It is an interface rather than a
// `boost::json::object&` for the reason `src/protocol/README.md` already states: Boost.JSON is a
// private implementation dependency of this domain's encoding source and no header exposes a Boost
// type. That rule is what lets every per-kind encoder below be a header a test can include
// directly.
//
// It also puts **number canonicalization in one place**. `set_number` is the only path a physical
// component value takes to the wire, so the round-trip-safe integral form and the refusal of `-0`
// are properties of the sink rather than rules seven encoders each remember.
// related: protocol_v3_json_encoding.cpp -- the one Boost.JSON-backed implementation.
class ComponentObjectSink {
public:
  ComponentObjectSink() = default;
  ComponentObjectSink(const ComponentObjectSink&) = delete;
  ComponentObjectSink(ComponentObjectSink&&) = delete;
  ComponentObjectSink& operator=(const ComponentObjectSink&) = delete;
  ComponentObjectSink& operator=(ComponentObjectSink&&) = delete;
  virtual ~ComponentObjectSink() = default;

  // A finite binary64 physical value. Integral magnitudes below 2^53 are written as integers, which
  // is the round-trip-safe decimal representation the schemas' examples use, and `-0` is written as
  // `0` because `docs/protocol/v3.md` § "Object member order" forbids it on the wire.
  virtual void set_number(std::string_view member_name, double value) = 0;
  virtual void set_unsigned(std::string_view member_name, std::uint64_t value) = 0;
  virtual void set_signed(std::string_view member_name, std::int64_t value) = 0;
  virtual void set_boolean(std::string_view member_name, bool value) = 0;
  virtual void set_string(std::string_view member_name, std::string_view value) = 0;
  // One `common.schema.json#/$defs/vector2` member, whose own members are `x` then `y`.
  virtual void set_vector(std::string_view member_name, double x, double y) = 0;

  // An array of objects in declared order. Calls encode_entry synchronously once per index,
  // using a nested sink with the same member-order and number rules. Zero entries emits [].
  // The caller owns schema-specific array bounds; errors from an entry propagate unchanged.
  virtual void
  set_object_array(std::string_view member_name, std::size_t count,
                   const std::function<void(std::size_t, ComponentObjectSink&)>& encode_entry) = 0;
};

// Everything a component encoder may read besides the component value itself.
//
// It is exactly two things, and both exist for one kind: `controllable` publishes the controller
// kind and display name joined from the directory, and falls back to `player-<entity_id>` when the
// directory no longer names the controller. No other kind reads either, which is the evidence that
// the context is the whole join and not an ambient bag.
struct ComponentEncodingContext final {
  simulation::EntityId entity;
  const ControllerDirectoryView* directory;
};

// canonical: component_wire_encoding -- what one component kind publishes on the protocol v3 wire.
// @extension-point snapshot_component_kind
//
// Declared beside the kind's own encoder header, exactly as `ComponentKindName` and
// `ComponentPublication` are declared beside the component's own struct in `blob_simulation`. The
// primary template is declared and never defined, so a kind registered in `ComponentRegistry` with
// no wire encoding fails to compile at the snapshot encoder's use site rather than silently
// vanishing from a published entity.
//
// Adding a component kind end to end:
//
//   new  src/simulation/components/<kind>_component.hpp        the value struct and its kind name
//   edit src/simulation/component_registry.hpp                 one type in the list
//   new  src/protocol/components/<kind>_component_encoding.hpp this specialization
//   edit src/protocol/component_encoding_registry.hpp          one include
//   new  docs/protocol/schema/v3/<kind>-component.schema.json  the closed wire schema
//   edit docs/protocol/schema/v3/entity-snapshot.schema.json   one property
//   edit docs/protocol/schema/v3/common.schema.json            one component_kind enum member
//   edit src/protocol/protocol_v3_constants.hpp                one name in the closed vocabulary
//
// which is a protocol minor version (`docs/protocol/v3.md` § "Versioning and fail-closed
// decoding"). Nothing else changes: the snapshot encoder, the entity walk, the ordering, and the
// bounds are all generated from `ComponentRegistry` and the kind's own declared name.
// related: src/simulation/component_kind_name.hpp -- the wire name each kind declares.
// related: component_encoding_registry.hpp -- the one place the specializations are gathered.
template <typename Component> struct ComponentWireEncoding;

} // namespace blob_royale::protocol

#endif
