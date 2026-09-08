import { describe, expect, it } from 'vitest';

import { snapshotDocument } from './fixtures/sessionFrames';
import { validateSessionSnapshotMessage } from './sessionProtocolValidation';
import {
  countAlivePlayers,
  describeMatchOverlay,
  findOwnEntityId,
  eliminationGraceTicks,
  findPlacementForController,
  graceSpentFraction,
  phaseElapsedSeconds,
  zoneExposureReport,
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

  it('reads the elimination grace only from a royale mode-state block that carries one', () => {
    // The golden snapshot is a royale frame, so it publishes `G` and the client can count down.
    expect(eliminationGraceTicks(snapshot.match)).toBe(1_200);
    expect(eliminationGraceTicks(null)).toBeNull();
    // The `none` block is what a mode with no non-entity-shaped state publishes -- `sandbox` does --
    // and it has no grace to read. Null here is "this mode has none", not "this frame is broken".
    expect(
      eliminationGraceTicks(
        matchWith({
          mode_state: {
            schema_id: 'blob-royale://protocol/v2/mode-state/none',
            value: {},
          },
        }),
      ),
    ).toBeNull();
    // A royale block whose value somehow lacks the member falls back rather than inventing one.
    // Schema validation makes it `required`, so this is unreachable through the real decode path
    // and is pinned anyway: the fallback is the whole reason the client cannot render a fabricated
    // countdown.
    expect(
      eliminationGraceTicks(
        matchWith({
          mode_state: {
            schema_id: 'blob-royale://protocol/v2/mode-state/royale',
            value: { previous_phase: 'running' },
          },
        }),
      ),
    ).toBeNull();
  });

  it('clamps the spent grace fraction at both ends and never divides by zero', () => {
    expect(graceSpentFraction(300, 1_200)).toBeCloseTo(0.25, 12);
    // No published grace: no denominator, and therefore no ramp rather than a guessed one.
    expect(graceSpentFraction(300, null)).toBeNull();
    // A grace of zero is a legal configuration meaning "eliminated on the first outside tick", so
    // it saturates instead of dividing.
    expect(graceSpentFraction(1, 0)).toBe(1);
    expect(Number.isFinite(graceSpentFraction(1, 0) ?? Number.NaN)).toBe(true);
    // An overshoot clamps. The server destroys the entity on the tick its counter reaches the
    // bound, so a published counter should never exceed it; the clamp is what keeps a frame that
    // says otherwise from rendering a ramp past full.
    expect(graceSpentFraction(1_800, 1_200)).toBe(1);
    expect(graceSpentFraction(0, 1_200)).toBe(0);
  });

  it('reports zone exposure only while an entity is outside the zone', () => {
    // The golden snapshot has entity 7 inside (`outside_ticks` 0), entity 8 outside for 214 ticks,
    // and entity 9 the zone itself, which carries no exposure component at all.
    expect(zoneExposureReport(snapshot.entities, 7, 400, 1_200)).toBeNull();
    expect(
      zoneExposureReport(snapshot.entities, 8, 400, 1_200)?.elapsedSeconds,
    ).toBeCloseTo(0.535, 12);
    expect(zoneExposureReport(snapshot.entities, 9, 400, 1_200)).toBeNull();
    expect(zoneExposureReport(snapshot.entities, null, 400, 1_200)).toBeNull();
    expect(zoneExposureReport(snapshot.entities, 404, 400, 1_200)).toBeNull();
    expect(zoneExposureReport(snapshot.entities, 8, 0, 1_200)).toBeNull();
  });

  it('counts the published grace down and falls back to elapsed without one', () => {
    // 214 of 1,200 ticks spent leaves 986 ticks, which is 2.465 s at the published 400 ticks/s.
    const counted = zoneExposureReport(snapshot.entities, 8, 400, 1_200);
    expect(counted?.remainingSeconds).toBeCloseTo(2.465, 12);
    expect(counted?.spentFraction).toBeCloseTo(214 / 1_200, 12);

    // No grace on the frame: elapsed is still known and the remainder is honestly absent.
    const uncounted = zoneExposureReport(snapshot.entities, 8, 400, null);
    expect(uncounted?.elapsedSeconds).toBeCloseTo(0.535, 12);
    expect(uncounted?.remainingSeconds).toBeNull();
    expect(uncounted?.spentFraction).toBeNull();

    // A counter past its bound reads as no time left rather than as a negative countdown.
    const overshot = zoneExposureReport(snapshot.entities, 8, 400, 100);
    expect(overshot?.remainingSeconds).toBe(0);
    expect(overshot?.spentFraction).toBe(1);

    // A grace of zero: no window at all, and no division by it either.
    const graceless = zoneExposureReport(snapshot.entities, 8, 400, 0);
    expect(graceless?.remainingSeconds).toBe(0);
    expect(graceless?.spentFraction).toBe(1);
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

    expect(zoneExposureReport(reentered, 8, 400, 1_200)).toBeNull();
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
