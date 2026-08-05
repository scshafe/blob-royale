<!-- canonical: server_domain -- transport ownership and deployment boundary -->

# Server domain

`blob_server` owns the one bounded HTTP/1.1 and WebSocket listener for protocol v1. It reads only
immutable `ServerConfig` and `const SnapshotPublication`; it cannot start, pause, stop, step, or
otherwise mutate the simulation. `GameServer::run()` is a single foreground event loop. The
application owns process signals and calls the thread-safe, idempotent `GameServer::stop()`, which
closes acceptance before sessions and enforces a fixed shutdown deadline.

## Public objects

* `GameServer` owns event-loop lifecycle, retained failure, and the listener.
* `TcpListener` owns one acceptor and rejects peers outside loopback or the exact trusted-proxy set.
* `HttpSession` owns one bounded parser, at most eight queued responses, and at most 100 requests.
* `GameApiRouter` is the sole HTTP trust-boundary validator and exposes only the four v1 routes.
* `SnapshotWebSocketSession` samples immutable publication at presentation cadence, permits one
  data write plus one replaceable pending snapshot, and accepts no application data from clients.
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

## Trust and deployment boundary

Protocol v1 has no authentication and snapshots are not confidential. Direct public exposure is
unsupported. Default execution binds loopback. A non-loopback bind is accepted only for an
unspecified/private address with nonempty exact Origin and trusted-proxy allowlists. Forwarding
headers are never used as identity or for origin rate accounting.

Peer accounting retains at most 128 principals. The full IPv4 `127.0.0.0/8` range and IPv6 `::1`
share one loopback principal; other supported peers use the canonical socket address. At capacity,
the oldest inactive principal is evicted with a lexical tie-break, while a new identity is denied if
all entries are active. Transparent lookup keeps RAII teardown allocation-free.

Before Beast parses a request, `HttpSession` scans the bounded raw header section for `CRLF`
followed by space/tab and rejects obsolete folding without field normalization. It consumes only
the first header terminator, preserving already-buffered pipeline bytes for the next preflight.

The production reverse proxy must terminate current HTTPS/WSS, keep the application listener on an
unreachable private OCI network, preserve the exact Host, Origin, target, and Upgrade headers,
select no application compression, disable snapshot-stream buffering, and use idle timeouts longer
than 25 seconds. It must apply external-client-aware header, connection, request, and upgrade-rate
limits because every external caller otherwise collapses to the proxy peer at the origin. The proxy
must strip or validate forwarding/request-ID headers; the application grants them no authority.

Host configuration is canonicalized with an explicit port. A request Host that omits its port uses
the configured application-listener port, so an external TLS proxy should forward its external Host
with `:443` when that is the configured authority. Serialized Origins canonicalize omitted `http`
and `https` ports to `80` and `443` for comparison, while an allowed HTTP response echoes the exact
received Origin value as required by browsers.

## Extension constraints

The versioned JSON encoding seam remains in `blob_protocol`; server sessions do not serialize
domain values themselves. A future command protocol must define authentication, player ownership,
authorization, tick addressing, ordering, and disconnect semantics together. It must use a new
versioned route/subprotocol and must not add mutation capability to this read-only server.
