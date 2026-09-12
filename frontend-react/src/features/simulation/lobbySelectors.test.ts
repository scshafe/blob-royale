import { solidTerrain } from './fixtures/terrainFrames';
import { describe, expect, it } from 'vitest';

import { legacyNpcCatalogue, snapshotDocument } from './fixtures/sessionFrames';
import {
  describeSeats,
  isSeatFilled,
  isSeatOccupied,
  npcSeatOptions,
  seatCountBounds,
  startAvailability,
} from './lobbySelectors';
import { validateSessionSnapshotMessage } from './sessionProtocolValidation';
import type {
  SessionEntitySnapshot,
  SessionMatchSection,
  SessionSeat,
} from './simulationProtocolTypes';
import {
  PROFILED_NPC_CONTROLLER_ID,
  PROFILED_NPC_DISPLAY_NAME,
  PROFILED_NPC_SEAT_INDEX,
  QUICK_NPC_PROFILE,
  STEADY_NPC_PROFILE,
  tacticalNpcCatalogue,
  tacticalSnapshot,
} from './fixtures/tacticalProfileFrames';

const snapshot = validateSessionSnapshotMessage(snapshotDocument(), {
  messageSequence: 1,
  requestId: snapshotDocument().meta.request_id,
  tickSequence: null,
  npcCatalogue: legacyNpcCatalogue,
  terrain: solidTerrain,
}).data;

const EMPTY: SessionSeat = Object.freeze({
  controller_id: null,
  kind: 'empty',
  npc_kind: null,
});
const PERSON: SessionSeat = Object.freeze({
  controller_id: 3,
  kind: 'controller',
  npc_kind: null,
});
const BUILT_BOT: SessionSeat = Object.freeze({
  controller_id: 12,
  kind: 'npc',
  npc_kind: 'wanderer',
});
const JOINING_BOT: SessionSeat = Object.freeze({
  controller_id: null,
  kind: 'npc',
  npc_kind: 'chaser',
});

function matchWithSeats(
  seats: readonly SessionSeat[],
  startRequested = false,
): SessionMatchSection {
  return { ...snapshot.match, seats, start_requested: startRequested };
}

describe('isSeatFilled and isSeatOccupied', () => {
  it("draws the server's line between a declaration and a body in the seat", () => {
    expect(isSeatFilled(PERSON)).toBe(true);
    expect(isSeatFilled(BUILT_BOT)).toBe(true);
    expect(isSeatFilled(JOINING_BOT)).toBe(false);
    expect(isSeatFilled(EMPTY)).toBe(false);
    expect(isSeatOccupied(JOINING_BOT)).toBe(true);
    expect(isSeatOccupied(EMPTY)).toBe(false);
  });
});

describe('describeSeats', () => {
  it('retains the profile on occupied seats even when an entity supplies the bot display name', () => {
    const data = tacticalSnapshot(
      STEADY_NPC_PROFILE,
      PROFILED_NPC_CONTROLLER_ID,
    ).data;
    const entity: SessionEntitySnapshot = {
      entity_id: 41,
      components: {
        controllable: {
          controller_id: PROFILED_NPC_CONTROLLER_ID,
          controller_kind: 'tactical',
          display_name: PROFILED_NPC_DISPLAY_NAME,
        },
      },
    };
    expect(
      describeSeats(data.match, [...data.entities, entity], null)[
        PROFILED_NPC_SEAT_INDEX
      ]?.label,
    ).toBe('Bot 41 / steady');
    expect(
      describeSeats(data.match, [], null)[PROFILED_NPC_SEAT_INDEX]?.label,
    ).toBe('tactical / steady');
  });

  it('names every seat from the bodies the snapshot carries and marks the own one', () => {
    // The golden roster: a person, a bot the server has built, a bot still joining, an empty seat.
    expect(describeSeats(snapshot.match, snapshot.entities, 3)).toEqual([
      {
        canClear: false,
        canSeatNpc: false,
        index: 0,
        isOwn: true,
        kind: 'controller',
        label: 'Cole Shaffer',
        title: 'Seat 1',
      },
      {
        canClear: true,
        canSeatNpc: false,
        index: 1,
        isOwn: false,
        kind: 'npc',
        label: 'wanderer',
        title: 'Seat 2',
      },
      {
        canClear: true,
        canSeatNpc: false,
        index: 2,
        isOwn: false,
        kind: 'npc',
        label: 'chaser (joining)',
        title: 'Seat 3',
      },
      {
        canClear: false,
        canSeatNpc: true,
        index: 3,
        isOwn: false,
        kind: 'empty',
        label: 'Empty',
        title: 'Seat 4',
      },
    ]);
  });

  it('marks nothing as own for a session that holds no seat', () => {
    expect(
      describeSeats(snapshot.match, snapshot.entities, 99).every(
        (seat) => !seat.isOwn,
      ),
    ).toBe(true);
    expect(
      describeSeats(snapshot.match, snapshot.entities, null).every(
        (seat) => !seat.isOwn,
      ),
    ).toBe(true);
  });

  it('falls back to an id for a person whose body is not published this frame', () => {
    const [seat] = describeSeats(
      matchWithSeats([{ ...PERSON, controller_id: 41 }]),
      snapshot.entities,
      null,
    );
    expect(seat?.label).toBe('Player 41');
  });
});

describe('npcSeatOptions', () => {
  it('expands both ordered projections into distinct complete declaration keys', () => {
    expect(npcSeatOptions(tacticalNpcCatalogue())).toEqual([
      {
        key: 'wanderer',
        label: 'wanderer',
        declaration: { npc_kind: 'wanderer' },
      },
      { key: 'chaser', label: 'chaser', declaration: { npc_kind: 'chaser' } },
      {
        key: 'tactical:steady',
        label: 'tactical / steady',
        declaration: STEADY_NPC_PROFILE,
      },
      {
        key: 'tactical:quick',
        label: 'tactical / quick',
        declaration: QUICK_NPC_PROFILE,
      },
    ]);
    expect(npcSeatOptions({ npc_controller_kinds: [] })).toEqual([]);
  });
});

describe('seatCountBounds', () => {
  it('floors one above the highest occupied seat and caps at the map', () => {
    expect(seatCountBounds(snapshot.match, 32)).toEqual({
      maximum: 32,
      minimum: 3,
    });
    // A declaration nobody has built a bot for is occupied: a resize must not drop it.
    expect(
      seatCountBounds(matchWithSeats([EMPTY, EMPTY, JOINING_BOT, EMPTY]), 6),
    ).toEqual({ maximum: 6, minimum: 3 });
    expect(seatCountBounds(matchWithSeats([EMPTY, EMPTY]), 6)).toEqual({
      maximum: 6,
      minimum: 1,
    });
  });

  it('never floors above the ceiling or below one', () => {
    expect(
      seatCountBounds(matchWithSeats([PERSON, PERSON, PERSON, PERSON]), 2),
    ).toEqual({ maximum: 2, minimum: 2 });
    expect(seatCountBounds(matchWithSeats([]), 0)).toEqual({
      maximum: 1,
      minimum: 1,
    });
  });
});

describe('startAvailability', () => {
  it('enables Start exactly when every seat is filled and every bot has joined', () => {
    expect(
      startAvailability(matchWithSeats([PERSON, BUILT_BOT, PERSON])),
    ).toEqual({ enabled: true, reason: null, startRequested: false });
  });

  it('says how many seats are empty, before it says which bots are joining', () => {
    expect(startAvailability(snapshot.match)).toEqual({
      enabled: false,
      reason: 'Waiting for 1 empty seat to be filled.',
      startRequested: true,
    });
    expect(
      startAvailability(matchWithSeats([PERSON, EMPTY, EMPTY])).reason,
    ).toBe('Waiting for 2 empty seats to be filled.');
    expect(
      startAvailability(matchWithSeats([PERSON, JOINING_BOT, JOINING_BOT]))
        .reason,
    ).toBe('Waiting for 2 bots to join.');
    expect(
      startAvailability(matchWithSeats([PERSON, JOINING_BOT])).reason,
    ).toBe('Waiting for 1 bot to join.');
  });

  it('has nothing to start in a world with no lobby', () => {
    expect(startAvailability(matchWithSeats([]))).toEqual({
      enabled: false,
      reason: 'This match has no lobby to start.',
      startRequested: false,
    });
  });
});
