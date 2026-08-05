# Protocol

This domain owns the accepted Blob Royale v1 wire representation. It is the only
production location that may translate validated simulation values into JSON.

Its public interface consists of the validated `RequestId`, `PublicConfiguration`,
and `HttpError` values plus the concrete functions in `protocol_json_encoding.hpp`.
Those headers expose no Boost types. Boost.JSON remains a private implementation
dependency of the encoding source file, and simulation values never serialize
themselves.

The domain depends on `blob_simulation` for immutable snapshot values and on
Boost.JSON for the v1 JSON implementation. It does not depend on the runtime,
server, application composition root, logging, or network sessions.

`protocol_json_encoding.hpp` is the `@extension-point snapshot_encoding`: another
negotiated wire version can add its own named encoder without changing simulation
or publication values. Protocol v1 itself is closed and must continue matching the
accepted schemas under `docs/protocol/schema/v1`.
