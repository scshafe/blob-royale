<!-- canonical: server_domain -- transport ownership and deployment boundary -->

# Server domain

`blob_server` owns the one bounded HTTP/1.1 and WebSocket listener for protocol v1 and protocol v3.
It reads immutable `ServerConfig` and a `LobbyDirectory`: one `LobbyEntry` per room, each a
`const SnapshotPublication` to read and a bounded capability bundle, `MatchSessionContext`:
`CommandSink::open_session`, `submit`, and `close_session`, a consuming tuning-result claim, and a read-only
presentation directory and the match identities a `welcome` announces. The v1 routes serve room 1; `GET /api/v3/lobbies` lists every room from its latest committed
snapshot and its admission count, and `GET /api/v3/lobbies/<lobby_id>/session` joins the room it
names. A session is bound at admission to its room's entry for its life and counts itself in and
out of it, so the application can tell when a room has been abandoned. It cannot start, pause, stop, step, or otherwise mutate the simulation, and it cannot
read world state through the write path or the result capability. `GameServer::run()` is a single foreground event loop. The
application owns process signals and calls the thread-safe, idempotent `GameServer::stop()`, which
closes acceptance before sessions and enforces a fixed shutdown deadline.

## Public objects

* `GameServer` owns event-loop lifecycle, retained failure, and the listener.
* `LobbyDirectory` is every room the server can route a session to, numbered `1..N`, fixed for the
  process lifetime; a `LobbyEntry` is one room's publication, session capability, and session count.
* `TcpListener` owns one acceptor and rejects peers outside loopback or the exact trusted-proxy set.
* `HttpSession` owns one bounded parser, at most eight queued responses, and at most 100 requests.
* `GameApiRouter` is the sole HTTP trust-boundary validator and exposes only the four v1 routes plus
  v3's two active target families: the lobby directory and
  `/api/v3/lobbies/<lobby_id>/session`. The one parametric segment is matched by the grammar
  `[1-9][0-9]{0,2}` between exact neighbours and resolved through `LobbyDirectory::find`; every
  other string under the prefix is `404 LOBBY.NOT_FOUND` before the method or the handshake is
  examined. Route and subprotocol are validated as a pair, never by first-acceptable offer, and any parsed
  `/api/v3/` or `/api/v2/` target's failure is rendered in the current v3 error envelope. Admission runs after every
  upgrade rule and before the WebSocket reservation: `503 LOBBY.UNAVAILABLE` for a room that is
  not serving and `409 LOBBY.FULL` when the room's admitted sessions already number its live seat
  count, both naming the room; a room with no lobby is never full this way.
* `PeerIdentity` is the one derivation of a connection's accounting principal and display name. It
  classifies the socket peer against `trusted_proxy_addresses` **before** any loopback test, so a
  configured proxy never inherits the direct-peer Origin relaxation.
* `SnapshotWebSocketSession` samples immutable publication at presentation cadence, permits one
  data write plus one replaceable pending snapshot, and accepts no application data from clients.
* `SessionWebSocketSession` is the protocol v3 half, bound for its life to the room the router
  admitted it into: it opens one `CommandSink` session, asks for a seat and a body the way the
  tick gives them, sends one `welcome` once it has a body, decodes command envelopes under the
  per-session command bucket, stamps its own current body onto every command, pushes v3 snapshots
  on v1's cadence algorithm, and retires its controller exactly once on every close path. A session
  whose join could take no seat -- the last-seat race lost, or a room past `countdown` with every
  seat held -- is closed `1013 lobby_full` before any welcome, by the same rule the tick seats with
  (`first_joinable_seat`).
* `RuntimeControllerDirectoryView` is the one production implementation of the v3 encoder's
  presentation port, and lives here because this is the only target allowed to depend on both
  `blob_protocol` and `blob_runtime`.
* `PeerTrafficPolicy` owns direct-peer connection and token-bucket accounting. Move-only TCP and
  WebSocket admission leases make capacity release structural across every handoff/failure path.
* `SnapshotEgressBudget` admits encoding on the same event loop with a full-frame reservation,
  16 MiB active encoded-payload cap, 16 MiB token capacity, 64 MiB/second refill, and a fixed fair
  waiter order. Denied slots keep only the newest immutable pending snapshot and consume no message
  sequence; capacity release never creates an out-of-cadence send.

All socket objects and peer policy run on the server event loop. The only cross-thread public
operation is `GameServer::stop()`. Hard listener, invariant, or encoding failures close traffic,
are retained without substitution, and rethrow from `run()`/`rethrow_if_failed()`.

The egress ledger accounts logical uncompressed JSON payload bytes, not transient encoder DOM
allocations, allocator capacity, WebSocket/TLS framing, kernel buffers, or total RSS. Payload string
capacity and the egress lease are released after write completion. A stalled write retains its
buffer until its cancellation handler runs, preventing use-after-free; because a close frame cannot
overtake that write, the peer may observe an abnormal transport close instead of the intended
`1013 slow_consumer` frame.

Recognized retired `/api/v2/session`, `/api/v2/lobbies`, and grammar-valid old room-session
paths return fixed `426 PROTOCOL.SESSION_VERSION_UPGRADE_REQUIRED` after global request security
checks but before method/handshake, upgrade-token, or session/controller admission. Recognition
does not require an existing room. Malformed old paths retain ordinary route errors. There is no
`/api/v3/session` alias and no general v2 encoder. See `docs/protocol/v3.md` for precise precedence.

Movement tuning uses the existing command path, mode mask, and global preparse limiter. Runtime
admission additionally bounds each controller to one unresolved exchange and a 500 ms interval;
reused ids and pipelining policy-close with fixed reasons. After egress admission the session
claims a covered result before encoding, adapts it to the protocol-owned validated value, and
retains the owned runtime result beside its active payload. Write completion clears only that
local value, never a later runtime exchange. No general acknowledgement bus or replay is exposed.

## Trust and deployment boundary

Neither protocol version has in-application authentication and snapshots are not confidential.
Direct public exposure is unsupported. Default execution binds loopback. A non-loopback bind is
accepted only for an unspecified/private address with nonempty exact Origin and trusted-proxy
allowlists.

Peer accounting retains at most 128 principals. At capacity, the oldest inactive principal is
evicted with a lexical tie-break, while a new identity is denied if all entries are active.
Transparent lookup keeps RAII teardown allocation-free.

The accounting principal is derived once per request by `derive_peer_identity`, before any route
runs. A **direct** peer's principal is its canonical socket address, with the full IPv4
`127.0.0.0/8` range and IPv6 `::1` sharing one loopback principal, and every caller-supplied
identity field -- `Forwarded`, `X-Forwarded-For`, `X-Real-IP`, and every `Tailscale-User-*` field --
is ignored entirely. A peer in `trusted_proxy_addresses` is **proxy-forwarded**: it must present
exactly one canonical `X-Forwarded-For` address, which becomes its principal for every request,
upgrade, and connection bound in both protocol versions, and an absent, repeated, listed, or
non-canonical value is `400` rather than a repaired value. Its optional `Tailscale-User-Name` is
accepted only when it matches the bounded printable-ASCII display-name grammar exactly, is never
substituted character by character, and is never logged; every other outcome publishes a generated
`player-<n>` label.

**Trusted-proxy classification precedes loopback classification**, so on the deployed configuration
-- where the proxy is the host's own loopback address -- there is no direct loopback peer and every
connection must present a valid `Origin` and a valid `X-Forwarded-For`. The residual is accepted and
severe: a loopback trusted proxy lets any local process forge both the principal and the name, which
is why this deployment is supported only on a single-operator host
(`docs/protocol/v3.md` § "Abuse cases and controls").

Before Beast parses a request, `HttpSession` scans the bounded raw header section for `CRLF`
followed by space/tab and rejects obsolete folding without field normalization. It consumes only
the first header terminator, preserving already-buffered pipeline bytes for the next preflight.

The production reverse proxy must terminate current HTTPS/WSS, keep the application listener on an
unreachable private OCI network, preserve the exact Host, Origin, target, and Upgrade headers,
select no application compression, disable snapshot-stream buffering, and use idle timeouts longer
than 25 seconds. It must apply external-client-aware header, connection, request, and upgrade-rate
limits because every external caller otherwise collapses to the proxy peer at the origin unless it
is named in `trusted_proxy_addresses`. The proxy must strip every forwarding, request-ID, and
`Tailscale-User-*` field arriving from a client and set them itself: naming it as a trusted proxy is
the single edit that turns those fields from ignored input into the accounting principal and the
published display name.

Host configuration is canonicalized with an explicit port. A request Host that omits its port uses
the configured application-listener port, so an external TLS proxy should forward its external Host
with `:443` when that is the configured authority. Serialized Origins canonicalize omitted `http`
and `https` ports to `80` and `443` for comparison, while an allowed HTTP response echoes the exact
received Origin value as required by browsers.

## Extension constraints

The versioned JSON encoding seam remains in `blob_protocol`; server sessions do not serialize
domain values themselves. Protocol v3 answered the "future command protocol" this section reserved:
it uses its own route and subprotocol, its ownership model is the socket rather than a credential,
its ordering rule is "the last command of a kind in a tick wins", and its disconnect semantics are a
server-issued `leave`, enqueued by `CommandSink::close_session` before the controller is retired,
which destroys everything the controller drove. A seat is likewise taken with a server-issued
`join` the session submits for itself, never with a client command that could name a seat. A
further command protocol must do the same
again on a new versioned route, and must not widen `MatchSessionContext` beyond the three operations
`CommandSink` exposes.
