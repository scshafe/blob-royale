import type {
  SessionEntitySnapshot,
  SessionMatchSection,
  SessionSeat,
} from './simulationProtocolTypes';

/**
 * A seat is filled when somebody is actually in it: a person, or the bot the server has created
 * for a declared NPC seat. A declaration nobody has built a bot for is occupied but not filled,
 * which is the server's own distinction (`docs/protocol/v2.md` § "The lobby commands"): Start
 * waits for the bot, and a resize must not drop the declaration.
 */
export function isSeatFilled(seat: SessionSeat): boolean {
  return (
    seat.kind === 'controller' ||
    (seat.kind === 'npc' && seat.controller_id !== null)
  );
}

export function isSeatOccupied(seat: SessionSeat): boolean {
  return seat.kind !== 'empty';
}

/** One seat as the lobby shows it, every string decided here so the panel stays presentational. */
export interface SeatDescription {
  /** An NPC seat may be cleared; a person's seat belongs to a live session and may not. */
  readonly canClear: boolean;
  /** Only an empty seat takes a bot: seating never replaces an occupant. */
  readonly canSeatNpc: boolean;
  readonly index: number;
  readonly isOwn: boolean;
  readonly kind: SessionSeat['kind'];
  readonly label: string;
  readonly title: string;
}

function displayNamesByController(
  entities: readonly SessionEntitySnapshot[],
): ReadonlyMap<number, string> {
  const names = new Map<number, string>();
  for (const entity of entities) {
    const controllable = entity.components.controllable;
    if (controllable !== undefined) {
      names.set(controllable.controller_id, controllable.display_name);
    }
  }
  return names;
}

function seatLabel(
  seat: SessionSeat,
  names: ReadonlyMap<number, string>,
): string {
  switch (seat.kind) {
    case 'empty':
      return 'Empty';
    case 'controller':
      return (
        (seat.controller_id === null
          ? undefined
          : names.get(seat.controller_id)) ??
        `Player ${seat.controller_id ?? '?'}`
      );
    case 'npc': {
      const kind = seat.npc_kind ?? 'bot';
      if (seat.controller_id === null) {
        return `${kind} (joining)`;
      }
      return names.get(seat.controller_id) ?? kind;
    }
  }
}

/**
 * The roster in seat-index order, which is the order a client renders and the order every
 * tie-break over seats resolves in. Names come from the bodies the snapshot already carries --
 * `controllable.display_name` keyed by controller id -- so a seat and its blob can never disagree
 * about who is sitting there.
 */
export function describeSeats(
  match: SessionMatchSection,
  entities: readonly SessionEntitySnapshot[],
  ownControllerId: number | null,
): readonly SeatDescription[] {
  const names = displayNamesByController(entities);
  return match.seats.map((seat, index) =>
    Object.freeze({
      canClear: seat.kind === 'npc',
      canSeatNpc: seat.kind === 'empty',
      index,
      isOwn: ownControllerId !== null && seat.controller_id === ownControllerId,
      kind: seat.kind,
      label: seatLabel(seat, names),
      title: `Seat ${index + 1}`,
    }),
  );
}

export interface SeatCountBounds {
  readonly maximum: number;
  readonly minimum: number;
}

/**
 * What a seat-count control may ask for. The floor is one above the highest occupied seat, because
 * a lobby never shrinks past somebody who is sitting down and the server would ignore the ask; the
 * ceiling is the map's marker count, which the welcome published as `seat_count_maximum` for
 * exactly this control. A control that offered more would offer settings the tick ignores.
 */
export function seatCountBounds(
  match: SessionMatchSection,
  seatCountMaximum: number,
): SeatCountBounds {
  let highestOccupiedIndex = -1;
  match.seats.forEach((seat, index) => {
    if (isSeatOccupied(seat)) {
      highestOccupiedIndex = index;
    }
  });
  const maximum = Math.max(1, seatCountMaximum);
  return Object.freeze({
    maximum,
    minimum: Math.min(maximum, Math.max(1, highestOccupiedIndex + 1)),
  });
}

/** Whether Start may be pressed, and the one sentence that says why not. */
export interface StartAvailability {
  readonly enabled: boolean;
  readonly reason: string | null;
  /** Somebody already pressed Start; the match begins the moment the field is complete. */
  readonly startRequested: boolean;
}

function pluralize(count: number, singular: string, plural: string): string {
  return `${count} ${count === 1 ? singular : plural}`;
}

/**
 * Start is enabled exactly when every seat is filled and every NPC seat has its controller, which
 * is the server's own start condition for royale, so the button is never enabled for a press the
 * tick would remember rather than act on.
 */
export function startAvailability(
  match: SessionMatchSection,
): StartAvailability {
  const startRequested = match.start_requested;
  if (match.seats.length === 0) {
    return Object.freeze({
      enabled: false,
      reason: 'This match has no lobby to start.',
      startRequested,
    });
  }
  const emptySeatCount = match.seats.filter(
    (seat) => seat.kind === 'empty',
  ).length;
  if (emptySeatCount > 0) {
    return Object.freeze({
      enabled: false,
      reason: `Waiting for ${pluralize(emptySeatCount, 'empty seat', 'empty seats')} to be filled.`,
      startRequested,
    });
  }
  const joiningBotCount = match.seats.filter(
    (seat) => seat.kind === 'npc' && seat.controller_id === null,
  ).length;
  if (joiningBotCount > 0) {
    return Object.freeze({
      enabled: false,
      reason: `Waiting for ${pluralize(joiningBotCount, 'bot', 'bots')} to join.`,
      startRequested,
    });
  }
  return Object.freeze({ enabled: true, reason: null, startRequested });
}
