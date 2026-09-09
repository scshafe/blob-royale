import type { SessionLobbyListing } from './simulationProtocolTypes';

/**
 * Why the directory says a join to a room would be refused at the door. The rule is admission's
 * (`docs/protocol/v2.md` § "The lobby directory"): a room that is not serving answers `503`, a
 * room whose sessions already number its seats answers `409`, and an id the directory does not
 * list is `404`. A room with no lobby publishes an empty roster and is never full.
 */
export type LobbyJoinRefusal =
  'room_full' | 'room_missing' | 'room_unavailable';

/**
 * Every way a room can turn a joiner away: the three the directory predicts, and the one only the
 * tick can decide -- `1013 lobby_full`, the last-seat race lost after admission.
 */
export type RoomRefusal = LobbyJoinRefusal | 'lobby_full';

export function findLobbyListing(
  listings: readonly SessionLobbyListing[],
  lobbyId: number,
): SessionLobbyListing | null {
  for (const listing of listings) {
    if (listing.lobby_id === lobbyId) {
      return listing;
    }
  }
  return null;
}

/**
 * The refusal the directory predicts for a join to this room, or `null` when nothing in the listing
 * would refuse it. The directory is advice read at one instant and admission is the decision, so
 * `null` means "worth asking", never "admitted"; and a browser never sees the HTTP response a
 * declined upgrade was refused with, so after a socket that closed before it opened this is also
 * the only way the client can say which of the three refusals it met.
 */
export function lobbyJoinRefusal(
  listing: SessionLobbyListing | null,
): LobbyJoinRefusal | null {
  if (listing === null) {
    return 'room_missing';
  }
  if (!listing.healthy) {
    return 'room_unavailable';
  }
  if (listing.seat_count > 0 && listing.session_count >= listing.seat_count) {
    return 'room_full';
  }
  return null;
}

/** The one sentence a player is told about a refusal, on the directory they are sent back to. */
export function describeRoomRefusal(
  refusal: RoomRefusal,
  lobbyId: number,
): string {
  switch (refusal) {
    case 'lobby_full':
      return `Room ${lobbyId} filled its last seat before your join was seated. Choose another room.`;
    case 'room_full':
      return `Room ${lobbyId} is full. Choose another room, or try again once somebody leaves.`;
    case 'room_missing':
      return `Room ${lobbyId} does not exist on this server.`;
    case 'room_unavailable':
      return `Room ${lobbyId} is not serving right now. Choose another room, or try again later.`;
  }
}

const phaseLabels: Readonly<Record<SessionLobbyListing['phase'], string>> =
  Object.freeze({
    countdown: 'Match starting',
    ended: 'Match over',
    lobby: 'In the lobby',
    running: 'Match running',
  });

function pluralize(count: number, singular: string, plural: string): string {
  return `${count} ${count === 1 ? singular : plural}`;
}

/** One room as the directory shows it, every string decided here so the view stays presentational. */
export interface LobbyListingDescription {
  readonly joinable: boolean;
  readonly lobbyId: number;
  readonly modeAndMap: string;
  readonly occupancyLabel: string;
  readonly phaseLabel: string;
  readonly refusal: LobbyJoinRefusal | null;
  readonly seatsLabel: string;
  readonly title: string;
}

/**
 * Describes one listing. `session_count` counts people and `npc_seat_count` counts declared bots,
 * which are different populations -- a bot holds a seat but is not a session -- so the occupancy
 * line names both rather than adding them.
 */
export function describeLobbyListing(
  listing: SessionLobbyListing,
): LobbyListingDescription {
  const refusal = lobbyJoinRefusal(listing);
  return Object.freeze({
    joinable: refusal === null,
    lobbyId: listing.lobby_id,
    modeAndMap: `${listing.mode} on ${listing.map}`,
    occupancyLabel: `${pluralize(listing.session_count, 'player', 'players')}, ${pluralize(listing.npc_seat_count, 'bot', 'bots')}`,
    phaseLabel: phaseLabels[listing.phase],
    refusal,
    seatsLabel:
      listing.seat_count === 0
        ? 'No lobby'
        : `${listing.filled_seat_count} of ${listing.seat_count} seats filled`,
    title: `Room ${listing.lobby_id}`,
  });
}
