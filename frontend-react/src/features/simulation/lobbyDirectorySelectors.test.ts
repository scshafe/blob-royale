import { describe, expect, it } from 'vitest';

import { lobbyDirectoryMessageExample } from './fixtures/protocolV2Examples';
import {
  describeLobbyListing,
  describeRoomRefusal,
  findLobbyListing,
  lobbyJoinRefusal,
} from './lobbyDirectorySelectors';
import { validateLobbyDirectoryMessage } from './sessionProtocolValidation';
import type { SessionLobbyListing } from './simulationProtocolTypes';

const goldenListings = validateLobbyDirectoryMessage(
  structuredClone(lobbyDirectoryMessageExample),
  lobbyDirectoryMessageExample.meta.request_id,
).data.lobbies;

function goldenRoom(lobbyId: number): SessionLobbyListing {
  const listing = findLobbyListing(goldenListings, lobbyId);
  if (listing === null) {
    throw new Error('TEST.GOLDEN_ROOM_MISSING');
  }
  return listing;
}

describe('findLobbyListing', () => {
  it('finds a room by id and answers null for one the directory does not list', () => {
    expect(findLobbyListing(goldenListings, 2)?.lobby_id).toBe(2);
    expect(findLobbyListing(goldenListings, 3)).toBeNull();
    expect(findLobbyListing([], 1)).toBeNull();
  });
});

describe('lobbyJoinRefusal', () => {
  it('predicts nothing for a serving room with a seat to give', () => {
    expect(lobbyJoinRefusal(goldenRoom(1))).toBeNull();
    expect(lobbyJoinRefusal(goldenRoom(2))).toBeNull();
  });

  it("predicts full once the sessions number the seats, which is admission's rule", () => {
    expect(lobbyJoinRefusal({ ...goldenRoom(1), session_count: 4 })).toBe(
      'room_full',
    );
    expect(lobbyJoinRefusal({ ...goldenRoom(1), session_count: 5 })).toBe(
      'room_full',
    );
    expect(lobbyJoinRefusal({ ...goldenRoom(1), session_count: 3 })).toBeNull();
  });

  it('never calls a room with no lobby full', () => {
    expect(
      lobbyJoinRefusal({
        ...goldenRoom(1),
        filled_seat_count: 0,
        npc_seat_count: 0,
        seat_count: 0,
        session_count: 5,
      }),
    ).toBeNull();
  });

  it('reports a room that is not serving before it reports it full', () => {
    expect(
      lobbyJoinRefusal({ ...goldenRoom(1), healthy: false, session_count: 4 }),
    ).toBe('room_unavailable');
  });

  it('reports a room the directory does not list as missing', () => {
    expect(lobbyJoinRefusal(null)).toBe('room_missing');
  });
});

describe('describeRoomRefusal', () => {
  it('names the room and what to do next, for every refusal', () => {
    expect(describeRoomRefusal('lobby_full', 2)).toBe(
      'Room 2 filled its last seat before your join was seated. Choose another room.',
    );
    expect(describeRoomRefusal('room_full', 2)).toBe(
      'Room 2 is full. Choose another room, or try again once somebody leaves.',
    );
    expect(describeRoomRefusal('room_missing', 9)).toBe(
      'Room 9 does not exist on this server.',
    );
    expect(describeRoomRefusal('room_unavailable', 1)).toBe(
      'Room 1 is not serving right now. Choose another room, or try again later.',
    );
  });
});

describe('describeLobbyListing', () => {
  it('describes the golden running room in the words a player is shown', () => {
    expect(describeLobbyListing(goldenRoom(1))).toEqual({
      joinable: true,
      lobbyId: 1,
      modeAndMap: 'royale on arena-960x640',
      occupancyLabel: '1 player, 2 bots',
      phaseLabel: 'Match running',
      refusal: null,
      seatsLabel: '2 of 4 seats filled',
      title: 'Room 1',
    });
  });

  it('counts people and bots separately, because they are different populations', () => {
    expect(describeLobbyListing(goldenRoom(2))).toMatchObject({
      occupancyLabel: '0 players, 1 bot',
      phaseLabel: 'In the lobby',
      seatsLabel: '1 of 4 seats filled',
    });
  });

  it('marks a full room unjoinable and a room with no lobby as such', () => {
    expect(
      describeLobbyListing({ ...goldenRoom(2), session_count: 4 }),
    ).toMatchObject({ joinable: false, refusal: 'room_full' });
    expect(
      describeLobbyListing({
        ...goldenRoom(2),
        filled_seat_count: 0,
        npc_seat_count: 0,
        seat_count: 0,
      }),
    ).toMatchObject({ joinable: true, seatsLabel: 'No lobby' });
  });
});
