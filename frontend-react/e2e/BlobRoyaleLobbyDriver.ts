import { BrowserE2EError } from './BrowserE2EError';

const SESSION_ROUTE = 'ws://127.0.0.1:8000/api/v2/session';
const SESSION_SUBPROTOCOL = 'blob-royale.session.v2';
const WELCOME_SCHEMA_ID = 'blob-royale://protocol/v2/welcome-message';
const SNAPSHOT_SCHEMA_ID = 'blob-royale://protocol/v2/snapshot-message';

const OPEN_TIMEOUT_MILLISECONDS = 10_000;
const CLOSE_TIMEOUT_MILLISECONDS = 10_000;
const WELCOME_TIMEOUT_MILLISECONDS = 10_000;
/**
 * The server seats a session at its first presentation slot after admission and publishes the seat
 * on the next frame, which is two frames at the fixtures' 20 Hz. Ten seconds is headroom for a slow
 * emulated toolchain, not a cadence.
 */
const SEATING_TIMEOUT_MILLISECONDS = 10_000;
/** A countdown of `[royale] countdown_seconds` and the frame that publishes `running` after it. */
const MATCH_START_TIMEOUT_MILLISECONDS = 20_000;

/**
 * The gap between two seatings from this one session, and it is a protocol property rather than a
 * guess.
 *
 * A command's addressed identity is its **sender**, so the server keeps at most one command of each
 * kind per sender per tick and the mailbox supersedes an earlier one that has not been drained yet
 * (`src/simulation/command_registry.hpp`, `src/runtime/command_mailbox.hpp`). That is the rule that
 * makes "the first of two clients to seat one seat wins" true, and its cost is that one session
 * cannot fill several seats in one burst: sending three seatings in a single event-loop turn lands
 * one of them, which is what the first version of this file did and is exactly the bug it produced.
 *
 * A person clicking seats is nowhere near this bound -- the simulation drains its mailbox every
 * 2.5 ms and a click is hundreds of ticks -- so 200 ms is eighty drains of headroom, not a sleep
 * chosen to make a race go away. Commands of *different* kinds need no gap at all, because they
 * occupy different mailbox slots and phase 0 applies them in rank order within one tick.
 */
const SEATING_INTERVAL_MILLISECONDS = 200;

type PublishedSeatKind = 'controller' | 'empty' | 'npc';

interface PublishedSeat {
  readonly kind: PublishedSeatKind;
  readonly controllerId: number | null;
}

interface PublishedLobby {
  readonly phase: string;
  readonly seats: readonly PublishedSeat[];
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null;
}

function malformedFrame(context: string): BrowserE2EError {
  return new BrowserE2EError(
    'BROWSER_E2E.LOBBY_DRIVER_FRAME_MALFORMED',
    'The game server sent the lobby driver a frame that does not have the published shape.',
    { context },
  );
}

/** The `meta.schema_id` of one server frame, which is how welcome and snapshot are told apart. */
function readSchemaId(frame: unknown): string {
  if (!isRecord(frame) || !isRecord(frame['meta'])) {
    throw malformedFrame('meta');
  }
  const schemaId = frame['meta']['schema_id'];
  if (typeof schemaId !== 'string') {
    throw malformedFrame('meta.schema_id');
  }
  return schemaId;
}

function readWelcomeControllerId(frame: unknown): number {
  if (!isRecord(frame) || !isRecord(frame['data'])) {
    throw malformedFrame('welcome.data');
  }
  const controllerId = frame['data']['controller_id'];
  if (typeof controllerId !== 'number') {
    throw malformedFrame('welcome.data.controller_id');
  }
  return controllerId;
}

function readSeat(value: unknown): PublishedSeat {
  if (!isRecord(value)) {
    throw malformedFrame('snapshot.data.match.seats[]');
  }
  const kind = value['kind'];
  const controllerId = value['controller_id'];
  if (kind !== 'controller' && kind !== 'empty' && kind !== 'npc') {
    throw malformedFrame('snapshot.data.match.seats[].kind');
  }
  if (controllerId !== null && typeof controllerId !== 'number') {
    throw malformedFrame('snapshot.data.match.seats[].controller_id');
  }
  return { kind, controllerId };
}

function readLobby(frame: unknown): PublishedLobby {
  if (
    !isRecord(frame) ||
    !isRecord(frame['data']) ||
    !isRecord(frame['data']['match'])
  ) {
    throw malformedFrame('snapshot.data.match');
  }
  const match = frame['data']['match'];
  const phase = match['phase'];
  const seats = match['seats'];
  if (typeof phase !== 'string' || !Array.isArray(seats)) {
    throw malformedFrame('snapshot.data.match.phase');
  }
  return { phase, seats: seats.map(readSeat) };
}

/**
 * Whether this session's admission has been resolved by the server: it holds a seat, or the lobby
 * is full with no bot for it to displace and it therefore holds none.
 */
function seatingResolved(lobby: PublishedLobby, controllerId: number): boolean {
  const seated = lobby.seats.some((seat) => seat.controllerId === controllerId);
  const nothingToTake = lobby.seats.every((seat) => seat.kind === 'controller');
  return seated || nothingToTake;
}

/**
 * The server's side of one session as it arrives: the welcome once, then the lobby every frame.
 * Waiting is by predicate over the newest state, so a flow asks for the fact it needs and gets the
 * close code instead when the server hangs up first.
 */
class LobbyObserver {
  private controllerId: number | null = null;
  private lobby: PublishedLobby | null = null;
  private closeCode: number | null = null;
  private readonly listeners = new Set<() => void>();

  constructor(socket: WebSocket) {
    socket.addEventListener('message', (event) => {
      this.observe(event.data);
    });
    socket.addEventListener('close', (event) => {
      this.closeCode = event.code;
      this.notify();
    });
  }

  private observe(payload: unknown): void {
    if (typeof payload !== 'string') {
      throw malformedFrame('binary frame');
    }
    const frame: unknown = JSON.parse(payload);
    const schemaId = readSchemaId(frame);
    if (schemaId === WELCOME_SCHEMA_ID) {
      this.controllerId = readWelcomeControllerId(frame);
    } else if (schemaId === SNAPSHOT_SCHEMA_ID) {
      this.lobby = readLobby(frame);
    }
    this.notify();
  }

  private notify(): void {
    for (const listener of this.listeners) {
      listener();
    }
  }

  /**
   * Resolves with the first non-null answer `read` gives over the state observed so far; rejects
   * when the server closes first or the deadline passes.
   */
  private awaitState<T>(
    read: () => T | null,
    timeoutMilliseconds: number,
    errorCode: string,
    message: string,
  ): Promise<T> {
    return new Promise<T>((resolve, reject) => {
      let settled = false;
      const finish = (): void => {
        settled = true;
        clearTimeout(timer);
        this.listeners.delete(check);
      };
      const check = (): void => {
        if (settled) {
          return;
        }
        if (this.closeCode !== null) {
          finish();
          reject(
            new BrowserE2EError(
              'BROWSER_E2E.LOBBY_DRIVER_CLOSED_EARLY',
              'The game server closed the lobby driver session before the flow was done with it, which means it refused one of its commands.',
              { close_code: this.closeCode },
            ),
          );
          return;
        }
        const value = read();
        if (value !== null) {
          finish();
          resolve(value);
        }
      };
      const timer = setTimeout(() => {
        if (!settled) {
          finish();
          reject(
            new BrowserE2EError(errorCode, message, {
              timeout_milliseconds: timeoutMilliseconds,
            }),
          );
        }
      }, timeoutMilliseconds);
      this.listeners.add(check);
      check();
    });
  }

  welcome(): Promise<number> {
    return this.awaitState(
      () => this.controllerId,
      WELCOME_TIMEOUT_MILLISECONDS,
      'BROWSER_E2E.LOBBY_DRIVER_WELCOME_TIMEOUT',
      'The lobby driver did not receive its welcome before the deadline.',
    );
  }

  /** The lobby as published once the server has decided where, if anywhere, this session sits. */
  seating(controllerId: number): Promise<PublishedLobby> {
    return this.awaitState(
      () =>
        this.lobby !== null && seatingResolved(this.lobby, controllerId)
          ? this.lobby
          : null,
      SEATING_TIMEOUT_MILLISECONDS,
      'BROWSER_E2E.LOBBY_DRIVER_SEATING_TIMEOUT',
      'The server never published a lobby in which the lobby driver was seated or could not be.',
    );
  }

  phase(phase: string): Promise<PublishedLobby> {
    return this.awaitState(
      () =>
        this.lobby !== null && this.lobby.phase === phase ? this.lobby : null,
      MATCH_START_TIMEOUT_MILLISECONDS,
      'BROWSER_E2E.LOBBY_DRIVER_MATCH_START_TIMEOUT',
      `The match did not reach phase '${phase}' before the deadline.`,
    );
  }
}

/**
 * Fills whatever seats are still empty in the running match's lobby with one NPC kind, presses
 * Start, and stays until the match is running, over the published protocol, from outside the
 * browser.
 *
 * **Why this exists, stated plainly, because it is a seam and not a feature.** Since protocol 2.3 a
 * royale match starts only when every seat in its lobby is filled and somebody sends `start_match`.
 * The commands that do that are on the wire; the *client* that would send them is the lobby UI,
 * which is a later step of the same plan. Until it lands, a browser flow that needs a running match
 * has to send those commands itself, and doing it here rather than by adding a hidden control to the
 * production client is what keeps the client honest: nothing ships a back door so a test can press
 * a button.
 *
 * It is also worth the space it takes for a second reason. It exercises the 2.3 command envelope end
 * to end -- a real upgrade, a real subprotocol, real closed payloads -- against the same server every
 * other flow talks to, which is coverage the browser flows cannot give until the UI exists.
 *
 * **It is a player while it is connected.** A session is seated on admission: the server gives it
 * the lowest empty seat, or -- before a match has started -- a bot's seat, and a session admitted
 * into a lobby that is full of people holds none. The driver reads the lobby the server publishes,
 * declares an NPC into each seat that is still empty, presses Start, and then **holds whatever it
 * holds until the match is running**: leaving during the countdown would empty its seat and send the
 * lobby back to waiting. A fixture that wants the driver out of the field the flow counts gives it
 * a seat of its own (`fixtures/blob-royale-browser-e2e-royale.cfg`); one whose lobby the browsers
 * fill entirely leaves it seatless, which is fine too.
 *
 * **It connects directly to the server rather than through the vite proxy.** The upgrade validation
 * requires an allowlisted `Origin` only from a *proxied* peer; a direct loopback peer may omit it
 * entirely, and Node's `WebSocket` sends none. Going through `:5173` would work too, but it would
 * make this depend on the preview server's proxy as well as on the game server.
 *
 * **It leaves nothing behind.** Closing the socket makes the server leave on the session's behalf:
 * its seat is vacated and whatever it drove, seated or still pending, is destroyed by the tick.
 * `fillSeatsAndStart` resolves only after that close completes, so a flow that awaits it can then
 * assert on a running match's counts without racing the teardown.
 *
 * @canonical browser_e2e_lobby_driver -- operates the pre-match lobby over the published wire.
 */
export class BlobRoyaleLobbyDriver {
  private constructor() {}

  /**
   * Opens one session, seats `npcKind` into every seat still empty once the server has seated the
   * session itself, presses Start, waits for `running`, and closes.
   *
   * `npcKind` must be one the server's `welcome.npc_controller_kinds` names; the boundary refuses
   * anything else with `command_payload_invalid` and closes, which surfaces here as an early close
   * rather than a silent no-op.
   */
  static async fillSeatsAndStart(npcKind: string): Promise<void> {
    const socket = await BlobRoyaleLobbyDriver.open();
    try {
      const observer = new LobbyObserver(socket);
      const controllerId = await observer.welcome();
      const lobby = await observer.seating(controllerId);

      let seatingsSent = 0;
      for (const [seatIndex, seat] of lobby.seats.entries()) {
        if (seat.kind !== 'empty') {
          continue;
        }
        if (seatingsSent > 0) {
          await BlobRoyaleLobbyDriver.pause(SEATING_INTERVAL_MILLISECONDS);
        }
        socket.send(
          JSON.stringify({
            kind: 'seat_npc',
            payload: { seat_index: seatIndex, npc_kind: npcKind },
          }),
        );
        seatingsSent += 1;
      }
      // No gap before this one: `start_match` is a different kind, so it takes its own mailbox slot
      // and phase 0 applies it after the seating in the same tick.
      socket.send(JSON.stringify({ kind: 'start_match', payload: {} }));
      // Every declared seat gets a bot from the server's reconciliation and the lobby starts once
      // each of them exists; `running` is the proof that the server saw every command above.
      await observer.phase('running');
    } finally {
      await BlobRoyaleLobbyDriver.close(socket);
    }
  }

  private static pause(milliseconds: number): Promise<void> {
    return new Promise((resolve) => {
      setTimeout(resolve, milliseconds);
    });
  }

  private static open(): Promise<WebSocket> {
    return new Promise((resolve, reject) => {
      const socket = new WebSocket(SESSION_ROUTE, SESSION_SUBPROTOCOL);
      const timeout = setTimeout(() => {
        reject(
          new BrowserE2EError(
            'BROWSER_E2E.LOBBY_DRIVER_UPGRADE_TIMEOUT',
            'The lobby driver did not complete its WebSocket upgrade before the deadline.',
            {
              route: SESSION_ROUTE,
              timeout_milliseconds: OPEN_TIMEOUT_MILLISECONDS,
            },
          ),
        );
      }, OPEN_TIMEOUT_MILLISECONDS);

      socket.addEventListener('open', () => {
        clearTimeout(timeout);
        resolve(socket);
      });
      socket.addEventListener('error', () => {
        clearTimeout(timeout);
        reject(
          new BrowserE2EError(
            'BROWSER_E2E.LOBBY_DRIVER_UPGRADE_REFUSED',
            'The game server refused the lobby driver session.',
            { route: SESSION_ROUTE, subprotocol: SESSION_SUBPROTOCOL },
          ),
        );
      });
    });
  }

  private static close(socket: WebSocket): Promise<void> {
    return new Promise((resolve, reject) => {
      if (socket.readyState === WebSocket.CLOSED) {
        resolve();
        return;
      }
      const timeout = setTimeout(() => {
        reject(
          new BrowserE2EError(
            'BROWSER_E2E.LOBBY_DRIVER_CLOSE_TIMEOUT',
            'The lobby driver session did not close before the deadline.',
            { timeout_milliseconds: CLOSE_TIMEOUT_MILLISECONDS },
          ),
        );
      }, CLOSE_TIMEOUT_MILLISECONDS);
      socket.addEventListener('close', () => {
        clearTimeout(timeout);
        resolve();
      });
      socket.close(1000, 'lobby driver finished');
    });
  }
}
