# Protocol

This domain owns the accepted Blob Royale v1 and v3 wire representations. It is the
only production location that may translate validated simulation values into JSON,
and the only one that may turn a client-chosen byte into a `simulation::Command`.

Its public interface consists of the validated `RequestId`, `PublicConfiguration`,
`HttpError`, `V3HttpError`, `SessionWelcome`, and `MovementTuningWireResult` values, the `ControllerDirectoryView`
port, and the concrete functions in `protocol_json_encoding.hpp` (v1),
`protocol_v3_json_encoding.hpp` (v3), `command_decoding.hpp` (the inbound direction),
and `protocol_v3_frame_conformance.hpp` (the invariants JSON Schema cannot express).
Those headers expose no Boost types, and simulation values never serialize themselves.

Boost.JSON is a private implementation dependency of this domain's encoding and
decoding sources. `bounded_json_serialization.hpp` is the single documented exception:
it is a `blob_protocol`-internal header, included only by this domain's own `.cpp`
files, that lets the v1 and v3 encoders share one bounded serializer rather than each
keeping a copy. No consumer of `blob_protocol` needs Boost.JSON to compile or link.

The domain depends on `blob_simulation` for immutable snapshot values and command
values, and on Boost.JSON. It does not depend on the runtime, server, application
composition root, logging, or network sessions. Protocol v3 publishes a controller
kind and display name that live in the runtime's `ControllerDirectory`; it reaches
them through the `ControllerDirectoryView` port declared here and implemented by
`blob_server`, so the dependency is inverted rather than added
(`docs/architecture/0002-simulation-architecture.md` § target table).

`protocol_json_encoding.hpp` and `protocol_v3_json_encoding.hpp` are the
`@extension-point snapshot_encoding`: another negotiated wire version adds its own
named encoders without changing simulation or publication values. Within v3,
`@extension-point snapshot_component_kind` is `component_encoding.hpp`,
`@extension-point snapshot_mode_state` is `mode_state_wire_encoding.hpp`, and
`@extension-point command_kind` is `command_wire_kind.hpp`; each declares a primary
template that is never defined, so a kind registered in the simulation without a wire
encoding fails to compile rather than vanishing from a frame. Protocol v1 itself is
closed and must continue matching the accepted schemas under
`docs/protocol/schema/v1`; v3 must match `docs/protocol/schema/v3`.

Session v3 requires complete immutable terrain in `SessionWelcome`; snapshot frames omit it.
The encoder reads the authored value retained by the map/snapshot, never a per-mode publisher
or a fallback rectangle. Historical v2 schemas/examples remain validated but have no active
encoder/decoder. Both parsed session-version URL prefixes use the current v3 error envelope;
only recognized retired routes receive the fixed application-major upgrade error. V1 stays closed.

V3 snapshots publish shared movement current/defaults/limits/revision/effective tick and require
an explicitly supplied nullable session result outside `match`. The result value enforces its
closed statuses and nullable fields without depending on runtime types; the server owns the
adapter. The encoder and frame oracle enforce snapshot coverage, while the client additionally
checks exact pending-request correlation. `set_movement_tuning` decoding uses the same closed
command path and the mode's published accepted mask.

Protocol 3.1 extends these same boundaries; the active routes and session subprotocol remain v3.
`set_thrust` accepts optional boolean `braking` (absent means false), while `rotate_velocity`
accepts one left/right pulse with the same optional generation token. Neither command accepts an
actor, velocity, or speed. Simulation owns brake stopping, exact sign/swap turns, generation
admission, and the steering → ability → rotation ordering; the decoder owns the closed payload.
The client vocabulary is nine kinds, with availability narrowed by each mode's published mask.

The single room-tuning value, request, and current/default publication each contain five fields:
acceleration, normal top speed, charge gain fraction, lethal birth rate, and nonlethal birth rate.
The existing JSON names remain canonical. Limits publish a minimum and maximum for every field;
charge maximum reflects the room's authored safety envelope and an unavailable crossing class has
rate maximum zero. Intrinsically valid requests outside those room limits receive committed
`unsupported_tuning` through the existing correlated result path. Apply and Reset each replace all
five values atomically; no second exchange or command path exists.

`Charge` encodes `activation_tick`, `cooldown_expiry_tick`, `active_expiry_tick`, then
`hit_stun_duration_ticks`. Cooldown remains strictly later than activation; active expiry may equal
activation, and a nonempty active window requires positive hit-stun duration. The two expiries are
independent. Successful first player contact removes the component and refunds cooldown while the
shared stun retains collision momentum. Shielded contact, braking, and received stun cancel only
the active attempt. The protocol publishes committed windows and velocity rather than inventing an
ability receipt. Shield still publishes its five captured fields and Stun its two absolute ticks.

The eighteenth component kind is sparse `crossing_hazard: {}`, identifying an object counted
against the shared 64-object crossing cap. Body and lifetime components own its motion and expiry.
Random class trials, weighted kind selection, and sampled speed use the existing hazards stream;
`random_draw_counts.hazards` therefore changes with running trials, not just successful births.
The registry, generated types, schema set, and encoder all retain one registered representation
per kind. See [the current contract](../../docs/protocol/v3.md) for exact member order, limits,
refusal semantics, and the historical 3.0 implementation record.
