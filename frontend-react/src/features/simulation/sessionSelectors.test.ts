import { describe, expect, it } from 'vitest';

import { snapshotDocument } from './fixtures/sessionFrames';
import { validateSessionSnapshotMessage } from './sessionProtocolValidation';
import {
  countAlivePlayers,
  describeMatchOverlay,
  findOwnEntityId,
  findPlacementForController,
  phaseElapsedSeconds,
  zoneExposureSeconds,
} from './sessionSelectors';
import type {
  SessionEntitySnapshot,
  SessionMatchSection,
} from './simulationProtocolTypes';

const snapshot = validateSessionSnapshotMessage(snapshotDocument(), {
  messageSequence: 1,
  requestId: snapshotDocument().meta.request_id,
  tickSequence: null,
}).data;

function matchWith(
  overrides: Partial<SessionMatchSection>,
): SessionMatchSection {
  return { ...snapshot.match, ...overrides };
}

describe('sessionSelectors', () => {
  it('resolves the own body by controller id, never by the welcome entity id', () => {
    expect(findOwnEntityId(snapshot.entities, 3)).toBe(7);
    expect(findOwnEntityId(snapshot.entities, 99)).toBeNull();
    expect(findOwnEntityId(snapshot.entities, null)).toBeNull();
  });

  it('counts alive players as the entities carrying a body and a controller', () => {
    expect(countAlivePlayers(snapshot.entities)).toBe(2);
    expect(countAlivePlayers([])).toBe(0);
  });

  it('finds a finished rank by controller id after the entity is destroyed', () => {
    expect(findPlacementForController(snapshot.match, 6)?.placement).toBe(3);
    expect(findPlacementForController(snapshot.match, 3)).toBeNull();
    expect(findPlacementForController(null, 6)).toBeNull();
  });

  it('reports no elapsed phase time while phase_started_tick is still zero', () => {
    expect(
      phaseElapsedSeconds(matchWith({ phase_started_tick: 0 }), 400, 400),
    ).toBeNull();
    expect(
      phaseElapsedSeconds(matchWith({ phase_started_tick: 200 }), 400, 400),
    ).toBeCloseTo(0.5, 12);
    expect(phaseElapsedSeconds(snapshot.match, null, 400)).toBeNull();
  });

  it('reports elapsed zone exposure only while an entity is outside the zone', () => {
    // The golden snapshot has entity 7 inside (`outside_ticks` 0), entity 8 outside for 214 ticks,
    // and entity 9 the zone itself, which carries no exposure component at all.
    expect(zoneExposureSeconds(snapshot.entities, 7, 400)).toBeNull();
    expect(zoneExposureSeconds(snapshot.entities, 8, 400)).toBeCloseTo(
      0.535,
      12,
    );
    expect(zoneExposureSeconds(snapshot.entities, 9, 400)).toBeNull();
    expect(zoneExposureSeconds(snapshot.entities, null, 400)).toBeNull();
    expect(zoneExposureSeconds(snapshot.entities, 404, 400)).toBeNull();
    expect(zoneExposureSeconds(snapshot.entities, 8, 0)).toBeNull();
  });

  it('clears the exposure the tick the server resets the counter on re-entry', () => {
    const reentered: SessionEntitySnapshot[] = snapshot.entities.map(
      (entity) =>
        entity.entity_id === 8
          ? {
              entity_id: entity.entity_id,
              components: {
                ...entity.components,
                zone_exposure: { outside_ticks: 0 },
              },
            }
          : entity,
    );

    expect(zoneExposureSeconds(reentered, 8, 400)).toBeNull();
  });

  it('describes the lobby, countdown, elimination, win, and draw overlays', () => {
    const entities = snapshot.entities;

    expect(
      describeMatchOverlay({
        entities,
        match: matchWith({ phase: 'lobby' }),
        ownControllerId: 3,
        ownEntityId: 7,
      })?.title,
    ).toBe('Waiting for players');
    expect(
      describeMatchOverlay({
        entities,
        match: matchWith({ phase: 'countdown' }),
        ownControllerId: 3,
        ownEntityId: 7,
      })?.title,
    ).toBe('Match starting');
    expect(
      describeMatchOverlay({
        entities,
        match: snapshot.match,
        ownControllerId: 3,
        ownEntityId: 7,
      }),
    ).toBeNull();
    expect(
      describeMatchOverlay({
        entities,
        match: snapshot.match,
        ownControllerId: 6,
        ownEntityId: null,
      }),
    ).toEqual({
      detail: 'You placed #3. The next lobby opens when this match ends.',
      title: 'Eliminated',
    });
    expect(
      describeMatchOverlay({
        entities,
        match: snapshot.match,
        ownControllerId: 4,
        ownEntityId: null,
      })?.title,
    ).toBe('Waiting for the next match');
    expect(
      describeMatchOverlay({
        entities,
        match: matchWith({
          outcome: {
            kind: 'won_by_entity',
            winner_entity_id: 8,
            winner_team_id: null,
          },
          phase: 'ended',
        }),
        ownControllerId: 3,
        ownEntityId: 7,
      }),
    ).toEqual({
      detail: 'wanderer-1 was the last blob in the zone.',
      title: 'Winner',
    });
    expect(
      describeMatchOverlay({
        entities,
        match: matchWith({
          outcome: {
            kind: 'drawn',
            winner_entity_id: null,
            winner_team_id: null,
          },
          phase: 'ended',
        }),
        ownControllerId: 3,
        ownEntityId: null,
      })?.title,
    ).toBe('Draw');
  });
});
