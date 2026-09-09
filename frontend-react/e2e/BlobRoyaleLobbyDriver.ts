import { BrowserE2EError } from './BrowserE2EError';

const SESSION_ROUTE = 'ws://127.0.0.1:8000/api/v2/session';
const SESSION_SUBPROTOCOL = 'blob-royale.session.v2';

const OPEN_TIMEOUT_MILLISECONDS = 10_000;
const CLOSE_TIMEOUT_MILLISECONDS = 10_000;
const FLUSH_SETTLE_MILLISECONDS = 500;

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

/**
 * Fills every seat in the running match's lobby with one NPC kind and presses Start, over the
 * published protocol, from outside the browser.
 *
 * **Why this exists, stated plainly, because it is a seam and not a feature.** Since protocol 2.3 a
 * royale match starts only when every seat in its lobby is filled and somebody sends `start_match`.
 * The four commands that do that are on the wire; the *client* that would send them is the lobby UI,
 * which is a later step of the same plan. Until it lands, a browser flow that needs a running match
 * has to send those commands itself, and doing it here rather than by adding a hidden control to the
 * production client is what keeps the client honest: nothing ships a back door so a test can press
 * a button.
 *
 * It is also worth the space it takes for a second reason. It exercises the 2.3 command envelope end
 * to end -- a real upgrade, a real subprotocol, real closed payloads -- against the same server every
 * other flow talks to, which is coverage the browser flows cannot give until the UI exists.
 *
 * **It connects directly to the server rather than through the vite proxy.** The upgrade validation
 * requires an allowlisted `Origin` only from a *proxied* peer; a direct loopback peer may omit it
 * entirely, and Node's `WebSocket` sends none. Going through `:5173` would work too, but it would
 * make this depend on the preview server's proxy as well as on the game server.
 *
 * **It leaves nothing behind.** The server requests a body for every session, so this one takes a
 * spawn point if the ring has a free one and is deferred if it does not; either way the session's
 * entity is despawned when the socket closes. `fillSeatsAndStart` resolves only after that close
 * completes, so a flow that awaits it can then assert on an entity count without racing the
 * teardown.
 *
 * @canonical browser_e2e_lobby_driver -- operates the pre-match lobby over the published wire.
 */
export class BlobRoyaleLobbyDriver {
  private constructor() {}

  /**
   * Opens one session, seats `seatCount` NPCs of `npcKind`, presses Start, and closes.
   *
   * `npcKind` must be one the server's `welcome.npc_controller_kinds` names; the boundary refuses
   * anything else with `command_payload_invalid` and closes, which surfaces here as an unexpected
   * close code rather than a silent no-op.
   */
  static async fillSeatsAndStart(
    seatCount: number,
    npcKind: string,
  ): Promise<void> {
    const socket = await BlobRoyaleLobbyDriver.open();
    try {
      for (let seatIndex = 0; seatIndex < seatCount; seatIndex += 1) {
        if (seatIndex > 0) {
          await BlobRoyaleLobbyDriver.pause(SEATING_INTERVAL_MILLISECONDS);
        }
        socket.send(
          JSON.stringify({
            kind: 'seat_npc',
            payload: { seat_index: seatIndex, npc_kind: npcKind },
          }),
        );
      }
      // No gap before this one: `start_match` is a different kind, so it takes its own mailbox slot
      // and phase 0 applies it after the seating in the same tick.
      socket.send(JSON.stringify({ kind: 'start_match', payload: {} }));
      // The commands are queued on a socket this function is about to close. Settling before the
      // close is what makes "the server saw them" true rather than likely; a close frame racing an
      // unflushed send is exactly the flake this flow must not have.
      await BlobRoyaleLobbyDriver.settle(socket);
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

  private static settle(socket: WebSocket): Promise<void> {
    return new Promise((resolve, reject) => {
      const deadline = Date.now() + FLUSH_SETTLE_MILLISECONDS;
      const poll = (): void => {
        if (socket.readyState !== WebSocket.OPEN) {
          reject(
            new BrowserE2EError(
              'BROWSER_E2E.LOBBY_DRIVER_CLOSED_EARLY',
              'The game server closed the lobby driver session before its commands were flushed, which means it refused one of them.',
              { ready_state: socket.readyState },
            ),
          );
          return;
        }
        if (socket.bufferedAmount === 0 && Date.now() >= deadline) {
          resolve();
          return;
        }
        setTimeout(poll, 25);
      };
      poll();
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
