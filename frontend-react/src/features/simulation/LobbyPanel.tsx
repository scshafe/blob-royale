import {
  useEffect,
  useId,
  useRef,
  useState,
  type MouseEvent as ReactMouseEvent,
} from 'react';

import {
  describeSeats,
  seatCountBounds,
  startAvailability,
  type SeatDescription,
} from './lobbySelectors';
import { SEAT_COUNT_COMMAND_DEBOUNCE_MILLISECONDS } from './simulationConstants';
import type {
  SessionEntitySnapshot,
  SessionMatchSection,
} from './simulationProtocolTypes';
import type { SimulationCommandSender } from './useSimulationConnection';

export interface LobbyPanelProps {
  readonly entities: readonly SessionEntitySnapshot[];
  readonly match: SessionMatchSection;
  /** `welcome.npc_controller_kinds`: the whole menu, read from the server's registry. */
  readonly npcControllerKinds: readonly string[];
  readonly ownControllerId: number | null;
  /** `welcome.seat_count_maximum`: the map's marker count, which caps the seat-count control. */
  readonly seatCountMaximum: number;
  readonly sendCommand: SimulationCommandSender;
}

function parseSeatCount(value: string): number | null {
  if (!/^[0-9]+$/.test(value)) {
    return null;
  }
  return Number(value);
}

/**
 * A seat count the player asked for, remembered with the count the server had published at the
 * time. It is spent -- and the control shows the server's count again -- once the published count
 * moves at all: to the ask, which is the server agreeing, or to something else, which is another
 * player's ask landing first and not one to fight over.
 */
interface PendingSeatCount {
  readonly askedAgainst: number;
  readonly count: number;
}

/**
 * The pre-match lobby: the seat grid, the seat-count control, the bot menu, and Start. Presentational
 * over the validated match section and the welcome's two facts; every rule it renders is a selector
 * in `lobbySelectors.ts`, and every press is one closed command through `sendCommand`.
 *
 * **Right-click and keyboard are the same menu.** `contextmenu` on an empty seat opens the bot menu
 * and suppresses the browser's own; the seat is also a button, so Enter or a click opens the same
 * menu for anyone without a mouse. Escape and a click outside close it. The menu is walked with
 * Tab, never the arrow keys, because the thrust hook owns the arrows and WASD for the whole window
 * and a menu that fought it would steer the blob while choosing a bot.
 *
 * **The seat-count control is debounced.** A dragged number input emits a change per step, and
 * the session's command bucket holds thirty tokens: sent per step, a drag from four to sixty-four
 * would close the socket with `command_rate_exceeded`. One `set_seat_count` goes out a quarter of
 * a second after the last change, carrying the final value, which is also the one the player meant.
 */
export function LobbyPanel({
  entities,
  match,
  npcControllerKinds,
  ownControllerId,
  seatCountMaximum,
  sendCommand,
}: LobbyPanelProps) {
  const headingId = useId();
  const seatCountId = useId();
  const panelReference = useRef<HTMLElement>(null);
  const [openedMenuSeatIndex, setOpenedMenuSeatIndex] = useState<number | null>(
    null,
  );
  const [pendingSeatCount, setPendingSeatCount] =
    useState<PendingSeatCount | null>(null);

  const seats = describeSeats(match, entities, ownControllerId);
  const bounds = seatCountBounds(match, seatCountMaximum);
  const availability = startAvailability(match);
  const publishedSeatCount = match.seats.length;
  const askedSeatCount =
    pendingSeatCount !== null &&
    pendingSeatCount.askedAgainst === publishedSeatCount &&
    pendingSeatCount.count !== publishedSeatCount
      ? pendingSeatCount.count
      : null;
  // A seat that stops being empty takes its menu with it.
  const menuSeatIndex =
    openedMenuSeatIndex !== null &&
    seats[openedMenuSeatIndex]?.canSeatNpc === true
      ? openedMenuSeatIndex
      : null;

  useEffect(() => {
    if (askedSeatCount === null) {
      return undefined;
    }
    const timer = setTimeout(() => {
      sendCommand({
        kind: 'set_seat_count',
        payload: { seat_count: askedSeatCount },
      });
    }, SEAT_COUNT_COMMAND_DEBOUNCE_MILLISECONDS);
    return () => {
      clearTimeout(timer);
    };
  }, [askedSeatCount, sendCommand]);

  // The menu closes on Escape and on a press anywhere outside the panel.
  useEffect(() => {
    if (menuSeatIndex === null) {
      return undefined;
    }
    const onKeyDown = (event: KeyboardEvent): void => {
      if (event.key === 'Escape') {
        setOpenedMenuSeatIndex(null);
      }
    };
    const onPointerDown = (event: PointerEvent): void => {
      const panel = panelReference.current;
      if (
        panel !== null &&
        event.target instanceof Node &&
        !panel.contains(event.target)
      ) {
        setOpenedMenuSeatIndex(null);
      }
    };
    document.addEventListener('keydown', onKeyDown);
    document.addEventListener('pointerdown', onPointerDown);
    return () => {
      document.removeEventListener('keydown', onKeyDown);
      document.removeEventListener('pointerdown', onPointerDown);
    };
  }, [menuSeatIndex]);

  const openMenu = (seat: SeatDescription): void => {
    setOpenedMenuSeatIndex(menuSeatIndex === seat.index ? null : seat.index);
  };

  const onSeatContextMenu = (
    event: ReactMouseEvent<HTMLElement>,
    seat: SeatDescription,
  ): void => {
    // The browser's own context menu would cover the lobby's; the lobby's is the only one an
    // empty seat has, and a seat that is not empty has none at all.
    event.preventDefault();
    if (seat.canSeatNpc) {
      setOpenedMenuSeatIndex(seat.index);
    }
  };

  const onSeatCountChange = (value: string): void => {
    const parsed = parseSeatCount(value);
    if (parsed === null) {
      return;
    }
    setPendingSeatCount({
      askedAgainst: publishedSeatCount,
      count: Math.min(bounds.maximum, Math.max(bounds.minimum, parsed)),
    });
  };

  return (
    <section
      aria-labelledby={headingId}
      className="LobbyPanel"
      ref={panelReference}
    >
      <h3 id={headingId}>Lobby</h3>
      <div className="SeatCountControl">
        <label htmlFor={seatCountId}>Seats</label>
        <input
          id={seatCountId}
          inputMode="numeric"
          max={bounds.maximum}
          min={bounds.minimum}
          onChange={(event) => {
            onSeatCountChange(event.target.value);
          }}
          type="number"
          value={askedSeatCount ?? publishedSeatCount}
        />
        <span className="SeatCountHint">
          {bounds.minimum} to {bounds.maximum}
        </span>
      </div>
      <ol className="SeatGrid">
        {seats.map((seat) => (
          <li
            className={`SeatCard SeatCard-${seat.kind}${seat.isOwn ? ' SeatCard-own' : ''}`}
            key={seat.index}
          >
            {seat.canSeatNpc ? (
              <button
                aria-expanded={menuSeatIndex === seat.index}
                aria-haspopup="menu"
                className="SeatButton"
                onClick={() => {
                  openMenu(seat);
                }}
                onContextMenu={(event) => {
                  onSeatContextMenu(event, seat);
                }}
                type="button"
              >
                <span className="SeatTitle">{seat.title}</span>{' '}
                <span className="SeatLabel">{seat.label}</span>
              </button>
            ) : (
              <div
                className="SeatButton"
                onContextMenu={(event) => {
                  onSeatContextMenu(event, seat);
                }}
              >
                <span className="SeatTitle">{seat.title}</span>{' '}
                <span className="SeatLabel">
                  {seat.label}
                  {seat.isOwn ? ' (you)' : ''}
                </span>
              </div>
            )}
            {seat.canClear ? (
              <button
                className="SeatClear"
                onClick={() => {
                  sendCommand({
                    kind: 'clear_seat',
                    payload: { seat_index: seat.index },
                  });
                }}
                type="button"
              >
                Clear {seat.title.toLowerCase()}
              </button>
            ) : null}
            {menuSeatIndex === seat.index ? (
              <ul
                aria-label={`Bots for ${seat.title.toLowerCase()}`}
                className="SeatMenu"
                role="menu"
              >
                {npcControllerKinds.length === 0 ? (
                  <li className="SeatMenuEmpty" role="none">
                    No bot kinds are registered.
                  </li>
                ) : (
                  npcControllerKinds.map((npcKind) => (
                    <li key={npcKind} role="none">
                      <button
                        onClick={() => {
                          sendCommand({
                            kind: 'seat_npc',
                            payload: {
                              npc_kind: npcKind,
                              seat_index: seat.index,
                            },
                          });
                          setOpenedMenuSeatIndex(null);
                        }}
                        role="menuitem"
                        type="button"
                      >
                        {npcKind}
                      </button>
                    </li>
                  ))
                )}
              </ul>
            ) : null}
          </li>
        ))}
      </ol>
      <div className="StartControl">
        <button
          className="StartButton"
          disabled={!availability.enabled}
          onClick={() => {
            sendCommand({ kind: 'start_match', payload: {} });
          }}
          type="button"
        >
          Start match
        </button>
        {availability.reason === null ? null : (
          <p className="StartReason">{availability.reason}</p>
        )}
        {availability.startRequested ? (
          <p className="StartRequested">
            Start is pressed. The match begins once every seat is filled.
          </p>
        ) : null}
      </div>
    </section>
  );
}
