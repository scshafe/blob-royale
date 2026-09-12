#ifndef BLOB_ROYALE_PROTOCOL_COMPONENTS_PHYSICS_BODY_COMPONENT_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_COMPONENTS_PHYSICS_BODY_COMPONENT_ENCODING_HPP

#include "component_encoding.hpp"
#include "component_wire_bound.hpp"

#include "physics_body.hpp"

namespace blob_royale::protocol {

// The `physics_body` wire object: everything the physics kernel reads or writes, in the order
// `docs/protocol/v3.md` § "Object member order" declares. Units are ADR 0003's -- position `wu`,
// velocity `wu/s`, acceleration `wu/s^2`, radius `wu` -- and the encoder converts none of them.
//
// **The wire requires a declared radius and the world does not.** `PhysicsBody::kUndeclaredRadius`
// is zero and means "defers to the configured radius", while the schema types `radius` as
// `positive_world_scalar`. A body that declares no size is therefore unpublishable, and this
// encoder says so with a named failure rather than substituting the configuration's radius: a
// substituted value is a body drawn at a size no phase used, and the encoder does not invent
// physics. Seating a body with its real radius is the fix, and it belongs in the simulation.
// related: docs/protocol/schema/v3/physics-body-component.schema.json -- the closed wire shape.
// related: component_wire_bound.hpp -- why these two bounds are checked and the others are not.
template <> struct ComponentWireEncoding<simulation::PhysicsBody> {
  static void encode(const simulation::PhysicsBody& body, const ComponentEncodingContext&,
                     ComponentObjectSink& sink) {
    sink.set_vector("position", body.position().x(), body.position().y());
    sink.set_vector("velocity", body.velocity().x(), body.velocity().y());
    sink.set_vector("acceleration", body.acceleration().x(), body.acceleration().y());
    require_positive_world_scalar(body.radius(),
                                  "snapshot.entities.components.physics_body.radius");
    require_nonnegative_world_scalar(body.mass(), "snapshot.entities.components.physics_body.mass");
    sink.set_number("radius", body.radius());
    sink.set_number("mass", body.mass());
    sink.set_unsigned("collision_layer", body.collision_layer());
    sink.set_unsigned("collision_mask", body.collision_mask());
    sink.set_boolean("is_static", body.is_static());
    sink.set_string("ground_attachment",
                    body.ground_attachment() == simulation::GroundAttachment::kGroundBound
                        ? "ground_bound"
                        : "floating");
  }
};

} // namespace blob_royale::protocol

#endif
