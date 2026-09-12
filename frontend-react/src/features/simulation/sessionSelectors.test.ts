import { raceTerrain, solidTerrain } from './fixtures/terrainFrames';
import { describe, expect, it, vi } from 'vitest';
import {
  cameraSessionIdentity,
  type CameraSnapshotScenario,
} from './fixtures/simulationCameraFrames';
import { cursorSteeringConnection } from './fixtures/cursorSteeringFrames';
import { THRUST_RIGHT, THRUST_ZERO } from './fixtures/thrustInputFrames';
import {
  STUN_INPUT_NEXT_GENERATION,
  STUN_INPUT_SNAPSHOT_TICK,
  STUN_INPUT_WINDOW,
  stunInputConnection,
  stunInputSnapshotDocument,
} from './fixtures/stunInputFrames';
import {
  CANCELLED_SHIELD_WINDOWS,
  SHIELD_ACTIVATION_TICK,
  SHIELD_COOLDOWN_EXPIRY_TICK,
  SHIELD_PARRY_STUN_DURATION_TICKS,
  SHIELD_SNAPSHOT_TICK,
  SHIELD_WINDOWS,
  shieldSnapshotDocument,
} from './fixtures/shieldFrames';
import {
  CHARGE_ACTIVATION_TICK,
  CHARGE_COOLDOWN,
  CHARGE_COOLDOWN_EXPIRY_TICK,
  chargeSnapshotDocument,
} from './fixtures/chargeFrames';

import {
  legacyNpcCatalogue,
  hillSnapshotDocument,
  type MutableSnapshotDocument,
  raceScenarioDocument,
  type RaceSnapshotScenario,
  snapshotDocument,
} from './fixtures/sessionFrames';
import { validateSessionSnapshotMessage } from './sessionProtocolValidation';
import {
  type AbilityAvailabilityReport,
  abilityAvailabilityReport,
  type AbilityStatusInput,
  abilityStatusReport,
  type AbilityUnavailableReason,
  countAlivePlayers,
  describeMatchOverlay,
  findOwnEntityId,
  eliminationGraceTicks,
  findPlacementForController,
  graceSpentFraction,
  hillHudReport,
  hillPresenceReport,
  hillProgressFraction,
  kingOfTheHillRules,
  phaseElapsedSeconds,
  raceHudReport,
  raceModeState,
  respawnCountdownSeconds,
  runningTimeRemainingSeconds,
  scoreboard,
  selectThrustInputOptions,
  zoneExposureReport,
} from './sessionSelectors';
import type {
  SessionEntitySnapshot,
  SessionMatchSection,
} from './simulationProtocolTypes';
import type { SimulationConnection } from './useSimulationConnection';
import type { ThrustDirection } from './useThrustInput';

const snapshot = validateSessionSnapshotMessage(snapshotDocument(), {
  messageSequence: 1,
  requestId: snapshotDocument().meta.request_id,
  tickSequence: null,
  npcCatalogue: legacyNpcCatalogue,
  terrain: solidTerrain,
}).data;

describe('selectThrustInputOptions', () => {
  it.each([
    [STUN_INPUT_WINDOW.activation_tick, true],
    [STUN_INPUT_SNAPSHOT_TICK, true],
    [STUN_INPUT_WINDOW.expiry_tick, false],
  ] as const)(
    'uses authoritative half-open stun containment at tick %s',
    (tick, locked) => {
      const connection = stunInputConnection(
        vi.fn(() => true),
        cameraSessionIdentity(),
        STUN_INPUT_NEXT_GENERATION,
        STUN_INPUT_WINDOW,
        tick,
      );
      const options = selectThrustInputOptions(
        connection,
        connection.session?.lobbyId ?? null,
      );
      expect(options.enabled).toBe(true);
      expect(options.inputLocked).toBe(locked);
      expect(options.inputGeneration).toBe(STUN_INPUT_NEXT_GENERATION);
    },
  );

  it('retains the authoritative generation after the entire stun component disappears', () => {
    const connection = stunInputConnection(
      vi.fn(() => true),
      cameraSessionIdentity(),
      STUN_INPUT_NEXT_GENERATION,
    );
    expect(selectThrustInputOptions(connection, 1)).toMatchObject({
      enabled: true,
      inputLocked: false,
      inputGeneration: STUN_INPUT_NEXT_GENERATION,
      ownEntityId: connection.ownEntityId,
    });
  });

  it('requires the current room, accepted thrust capability, live connection, and actual owned body', () => {
    const connection = stunInputConnection(
      vi.fn(() => true),
      cameraSessionIdentity(),
      STUN_INPUT_NEXT_GENERATION,
    );
    expect(selectThrustInputOptions(connection, null).enabled).toBe(false);
    expect(
      selectThrustInputOptions({ ...connection, ownEntityId: null }, 1).enabled,
    ).toBe(false);
    expect(
      selectThrustInputOptions({ ...connection, entities: [] }, 1).enabled,
    ).toBe(false);
    expect(
      selectThrustInputOptions({ ...connection, status: 'connecting' }, 1)
        .enabled,
    ).toBe(false);
    const session = connection.session;
    if (session === null) throw new Error('TEST.STUN_INPUT_SESSION_MISSING');
    expect(
      selectThrustInputOptions(
        { ...connection, session: { ...session, acceptedCommandKinds: [] } },
        1,
      ).enabled,
    ).toBe(false);
  });
});

function matchWith(
  overrides: Partial<SessionMatchSection>,
): SessionMatchSection {
  return { ...snapshot.match, ...overrides };
}

/** The built hill frame, accepted by the 3.0 schemas before any selector reads it. */
const hill = validateSessionSnapshotMessage(hillSnapshotDocument(), {
  messageSequence: 1,
  requestId: hillSnapshotDocument().meta.request_id,
  tickSequence: null,
  npcCatalogue: legacyNpcCatalogue,
  terrain: solidTerrain,
}).data;

function hillMatchWith(
  overrides: Partial<SessionMatchSection>,
): SessionMatchSection {
  return { ...hill.match, ...overrides };
}

function raceFrame(scenario: RaceSnapshotScenario = 'running') {
  const document = raceScenarioDocument(scenario);
  return validateSessionSnapshotMessage(document, {
    messageSequence: 1,
    requestId: document.meta.request_id,
    tickSequence: null,
    npcCatalogue: legacyNpcCatalogue,
    terrain: raceTerrain,
  }).data;
}

function raceReport(
  scenario: RaceSnapshotScenario = 'running',
  ownControllerId = 3,
) {
  const frame = raceFrame(scenario);
  return raceHudReport({
    entities: frame.entities,
    match: frame.match,
    ownControllerId,
    ownEntityId: findOwnEntityId(frame.entities, ownControllerId),
    tickSequence: frame.tick_sequence,
    ticksPerSecond: 400,
  });
}

function raceOverlay(scenario: RaceSnapshotScenario, ownControllerId = 3) {
  const frame = raceFrame(scenario);
  return describeMatchOverlay({
    entities: frame.entities,
    match: frame.match,
    ownControllerId,
    ownEntityId: findOwnEntityId(frame.entities, ownControllerId),
  });
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
            schema_id: 'blob-royale://protocol/v3/mode-state/none',
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
            schema_id: 'blob-royale://protocol/v3/mode-state/royale',
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

describe('sessionSelectors for the hill', () => {
  it('reads the three rules only from a king-of-the-hill block that carries them', () => {
    expect(kingOfTheHillRules(hill.match)).toEqual({
      pointIntervalTicks: 400,
      pointsToWin: 30,
      timeLimitTicks: 96_000,
    });
    // The royale golden frame and the `none` block are not hill frames; null is "another mode",
    // not "a broken frame", and it is what keeps the royale HUD exactly as it was.
    expect(kingOfTheHillRules(snapshot.match)).toBeNull();
    expect(kingOfTheHillRules(null)).toBeNull();
    expect(
      kingOfTheHillRules(
        matchWith({
          mode_state: {
            schema_id: 'blob-royale://protocol/v3/mode-state/none',
            value: {},
          },
        }),
      ),
    ).toBeNull();
    // A hill block missing a member, or carrying a negative one, reads as no rules rather than as
    // invented ones. Unreachable through the validated decode path and pinned anyway, because the
    // fallback is what keeps every hill row from dividing by a number nobody sent.
    expect(
      kingOfTheHillRules(
        hillMatchWith({
          mode_state: {
            schema_id: 'blob-royale://protocol/v3/mode-state/king-of-the-hill',
            value: { points_to_win: 30, point_interval_ticks: 400 },
          },
        }),
      ),
    ).toBeNull();
    expect(
      kingOfTheHillRules(
        hillMatchWith({
          mode_state: {
            schema_id: 'blob-royale://protocol/v3/mode-state/king-of-the-hill',
            value: {
              points_to_win: 30,
              point_interval_ticks: -1,
              time_limit_ticks: 96_000,
            },
          },
        }),
      ),
    ).toBeNull();
  });

  it('ranks every participant best first, ties by entity id, and a missing score as zero', () => {
    // Entity 10 is knocked out: no body, a respawn timer, and still a line on the board.
    expect(scoreboard(hill.entities)).toEqual([
      {
        controllerId: 4,
        displayName: 'wanderer-1',
        entityId: 8,
        isInPlay: true,
        points: 6,
      },
      {
        controllerId: 3,
        displayName: 'Cole Shaffer',
        entityId: 7,
        isInPlay: true,
        points: 4,
      },
      {
        controllerId: 5,
        displayName: 'chaser-2',
        entityId: 10,
        isInPlay: false,
        points: 2,
      },
    ]);
    // The royale frame's players carry no score: zero each, in entity order, and the hill and the
    // wall are not participants.
    expect(
      scoreboard(snapshot.entities).map((row) => [row.entityId, row.points]),
    ).toEqual([
      [7, 0],
      [8, 0],
    ]);
    expect(scoreboard([])).toEqual([]);
  });

  it('saturates a zero interval instead of dividing and clamps an overshoot', () => {
    expect(hillProgressFraction(120, 400)).toBeCloseTo(0.3, 12);
    expect(hillProgressFraction(1, 0)).toBe(1);
    expect(Number.isFinite(hillProgressFraction(1, 0))).toBe(true);
    expect(hillProgressFraction(900, 400)).toBe(1);
    expect(hillProgressFraction(0, 400)).toBe(0);
  });

  it('reports a presence only for an entity that carries one', () => {
    const rules = kingOfTheHillRules(hill.match);
    const own = hillPresenceReport(hill.entities, 7, 400, rules);
    // 120 of 400 ticks held leaves 280, which is 0.7 s at 400 ticks/s.
    expect(own?.heldSeconds).toBeCloseTo(0.3, 12);
    expect(own?.remainingSeconds).toBeCloseTo(0.7, 12);
    expect(own?.spentFraction).toBeCloseTo(0.3, 12);
    expect(hillPresenceReport(hill.entities, 8, 400, rules)).toBeNull();
    expect(hillPresenceReport(hill.entities, null, 400, rules)).toBeNull();
    expect(hillPresenceReport(hill.entities, 7, 0, rules)).toBeNull();
    expect(hillPresenceReport(hill.entities, 7, 400, null)).toBeNull();
    // A zero interval: the whole ring, and no time to wait.
    const instant = hillPresenceReport(hill.entities, 7, 400, {
      pointIntervalTicks: 0,
      pointsToWin: 30,
      timeLimitTicks: 96_000,
    });
    expect(instant?.remainingSeconds).toBe(0);
    expect(instant?.spentFraction).toBe(1);
  });

  it('counts the running clock down and clamps at zero', () => {
    // Running since tick 10,904 at tick 12,904 against 96,000 ticks: 94,000 ticks, 235 s.
    expect(
      runningTimeRemainingSeconds(hill.match, 12_904, 400, 96_000),
    ).toBeCloseTo(235, 12);
    expect(runningTimeRemainingSeconds(hill.match, 200_000, 400, 96_000)).toBe(
      0,
    );
    expect(
      runningTimeRemainingSeconds(hill.match, 12_904, 400, null),
    ).toBeNull();
    expect(
      runningTimeRemainingSeconds(hill.match, null, 400, 96_000),
    ).toBeNull();
    expect(
      runningTimeRemainingSeconds(hill.match, 12_904, 0, 96_000),
    ).toBeNull();
    expect(runningTimeRemainingSeconds(null, 12_904, 400, 96_000)).toBeNull();
    expect(
      runningTimeRemainingSeconds(
        hillMatchWith({ phase: 'countdown' }),
        12_904,
        400,
        96_000,
      ),
    ).toBeNull();
    expect(
      runningTimeRemainingSeconds(
        hillMatchWith({ phase_started_tick: 0 }),
        12_904,
        400,
        96_000,
      ),
    ).toBeNull();
  });

  it('counts a knocked-out entity down to its seat and nobody else', () => {
    expect(respawnCountdownSeconds(hill.entities, 10, 400)).toBeCloseTo(
      0.75,
      12,
    );
    expect(respawnCountdownSeconds(hill.entities, 7, 400)).toBeNull();
    expect(respawnCountdownSeconds(hill.entities, null, 400)).toBeNull();
    expect(respawnCountdownSeconds(hill.entities, 10, 0)).toBeNull();
  });

  it('composes the hill section for a hill frame and nothing for a royale frame', () => {
    const report = hillHudReport({
      entities: hill.entities,
      match: hill.match,
      ownEntityId: 7,
      tickSequence: 12_904,
      ticksPerSecond: 400,
    });
    expect(report?.ownPoints).toBe(4);
    expect(report?.presence?.remainingSeconds).toBeCloseTo(0.7, 12);
    expect(report?.respawnSeconds).toBeNull();
    expect(report?.rules.pointsToWin).toBe(30);
    expect(report?.scoreboard.map((row) => row.entityId)).toEqual([8, 7, 10]);
    expect(report?.timeRemainingSeconds).toBeCloseTo(235, 12);

    const knockedOut = hillHudReport({
      entities: hill.entities,
      match: hill.match,
      ownEntityId: 10,
      tickSequence: 12_904,
      ticksPerSecond: 400,
    });
    expect(knockedOut?.ownPoints).toBe(2);
    expect(knockedOut?.presence).toBeNull();
    expect(knockedOut?.respawnSeconds).toBeCloseTo(0.75, 12);

    const unseated = hillHudReport({
      entities: hill.entities,
      match: hill.match,
      ownEntityId: null,
      tickSequence: 12_904,
      ticksPerSecond: 400,
    });
    expect(unseated?.ownPoints).toBeNull();

    expect(
      hillHudReport({
        entities: snapshot.entities,
        match: snapshot.match,
        ownEntityId: 7,
        tickSequence: 12_904,
        ticksPerSecond: 400,
      }),
    ).toBeNull();
  });

  it("describes the hill's countdown, win, and draw in the hill's own words", () => {
    const entities = hill.entities;

    expect(
      describeMatchOverlay({
        entities,
        match: hillMatchWith({ phase: 'countdown' }),
        ownControllerId: 3,
        ownEntityId: 7,
      })?.detail,
    ).toBe('Get ready: hold the hill to score once the match begins.');
    // A knocked-out player keeps its entity, so there is no overlay to read while it waits.
    expect(
      describeMatchOverlay({
        entities,
        match: hill.match,
        ownControllerId: 5,
        ownEntityId: 10,
      }),
    ).toBeNull();
    expect(
      describeMatchOverlay({
        entities,
        match: hillMatchWith({
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
      detail: 'wanderer-1 held the hill with 6 points.',
      title: 'Winner',
    });
    expect(
      describeMatchOverlay({
        entities,
        match: hillMatchWith({
          outcome: {
            kind: 'won_by_entity',
            winner_entity_id: 7,
            winner_team_id: null,
          },
          phase: 'ended',
        }),
        ownControllerId: 3,
        ownEntityId: 7,
      }),
    ).toEqual({
      detail: 'You held the hill with 4 points.',
      title: 'You win',
    });
    expect(
      describeMatchOverlay({
        entities,
        match: hillMatchWith({
          outcome: {
            kind: 'drawn',
            winner_entity_id: null,
            winner_team_id: null,
          },
          phase: 'ended',
        }),
        ownControllerId: 3,
        ownEntityId: 7,
      }),
    ).toEqual({
      detail: 'Nobody took the hill outright. The next lobby opens shortly.',
      title: 'Draw',
    });
  });
});

describe('sessionSelectors for the race', () => {
  it('selects the complete course by schema id independently of the open mode name', () => {
    const frame = raceFrame();
    expect(raceModeState({ ...frame.match, mode: 'custom_race_name' })).toEqual(
      frame.match.mode_state.value,
    );
    expect(raceModeState(snapshot.match)).toBeNull();
    expect(raceModeState(hill.match)).toBeNull();
    expect(raceModeState(null)).toBeNull();
  });

  it('fails visibly if an internal caller supplies a malformed known race block', () => {
    const frame = raceFrame();
    expect(() =>
      raceModeState({
        ...frame.match,
        mode_state: { ...frame.match.mode_state, value: {} },
      }),
    ).toThrow('Cannot read the race mode state');
  });

  it('reports gates taken and tick-derived running time without inventing standings', () => {
    expect(raceReport()).toMatchObject({
      checkpointCount: 3,
      ownCheckpointCount: 1,
      ownStanding: null,
      standings: [],
      timeRemainingSeconds: 235,
      finishWindowRemainingSeconds: null,
      respawnSeconds: null,
    });
  });

  it('replaces the running clock with the first finish window and clamps its expiry', () => {
    expect(raceReport('finish_window')).toMatchObject({
      ownCheckpointCount: 3,
      ownStanding: { placement: 1 },
      timeRemainingSeconds: null,
      finishWindowRemainingSeconds: 4,
    });
    expect(
      raceReport('finish_window_expired')?.finishWindowRemainingSeconds,
    ).toBe(0);
  });

  it('keeps simultaneous finishers in recorded order and preserves their shared rank', () => {
    expect(raceReport('tied_finish')?.standings).toEqual([
      {
        controllerId: 3,
        displayName: 'Cole Shaffer',
        entityId: 7,
        isOwn: true,
        placement: 1,
        finishedTick: 12504,
        finishedTickOffset: 0.25,
      },
      {
        controllerId: 4,
        displayName: 'wanderer-1',
        entityId: 8,
        isOwn: false,
        placement: 1,
        finishedTick: 12504,
        finishedTickOffset: 0.25,
      },
    ]);
  });

  it('recognizes an own finish by its recorded controller after the entity disappears', () => {
    expect(raceReport('own_finish_after_wipe')).toMatchObject({
      ownCheckpointCount: 3,
      ownStanding: { entity_id: 7, controller_id: 3, placement: 1 },
      standings: [
        { entityId: 7, displayName: 'entity 7', isOwn: true, placement: 1 },
      ],
    });
  });

  it('reports a bodyless racer countdown and a zero-time wait until its checkpoint clears', () => {
    expect(raceReport('running', 5)).toMatchObject({
      ownCheckpointCount: 1,
      respawnSeconds: 0.75,
      awaitingReturn: null,
    });
    expect(raceReport('awaiting_checkpoint', 5)).toMatchObject({
      ownCheckpointCount: 1,
      respawnSeconds: 0,
      awaitingReturn: 'checkpoint',
    });
    expect(raceReport('awaiting_grid', 5)).toMatchObject({
      ownCheckpointCount: 0,
      respawnSeconds: 0,
      awaitingReturn: 'grid',
    });
  });

  it('shows no race timer during countdown or for a session with no participating entity', () => {
    expect(raceReport('countdown')).toMatchObject({
      timeRemainingSeconds: null,
      finishWindowRemainingSeconds: null,
    });
    expect(raceReport('running', 99)).toMatchObject({
      ownCheckpointCount: null,
      respawnSeconds: null,
      awaitingReturn: null,
    });
    expect(raceOverlay('awaiting_checkpoint', 5)).toBeNull();
  });

  it('retains both a recorded finish and the return countdown after a finisher loses its body', () => {
    expect(raceReport('finished_respawning')).toMatchObject({
      ownCheckpointCount: 3,
      ownStanding: { placement: 1 },
      respawnSeconds: 0.75,
    });
  });

  it('announces a recorded own win after a wipe and a gate leader when nobody finished', () => {
    expect(raceOverlay('own_finish_after_wipe')).toEqual({
      title: 'You win',
      detail: 'You finished #1.',
    });
    expect(raceOverlay('clock_win')).toEqual({
      title: 'Winner',
      detail: 'wanderer-1 led on gates taken when time ran out (2 of 3).',
    });
  });

  it('distinguishes a shared finish from a gate-count tie and an empty field draw', () => {
    expect(raceOverlay('tied_finish')).toEqual({
      title: 'Draw',
      detail:
        'The first finishers crossed at the same instant. The next lobby opens shortly.',
    });
    expect(raceOverlay('clock_draw')).toEqual({
      title: 'Draw',
      detail:
        'Time ran out with the lead tied on gates taken. The next lobby opens shortly.',
    });
    expect(raceOverlay('empty_draw')).toEqual({
      title: 'Draw',
      detail: 'No racers remained. The next lobby opens shortly.',
    });
    expect(raceOverlay('empty_draw_after_finish')).toEqual({
      title: 'Draw',
      detail: 'No racers remained. The next lobby opens shortly.',
    });
  });

  it('explains ordered gates during countdown', () => {
    expect(raceOverlay('countdown')).toEqual({
      title: 'Match starting',
      detail: 'Get ready: take each gate in order and stay on the road.',
    });
  });

  it('keeps a recorded finisher out of the next-match waiting overlay without a live entity', () => {
    const document = raceScenarioDocument('own_finish_after_wipe');
    document.data.match.phase = 'running';
    document.data.match.phase_started_tick = 10904;
    document.data.match.outcome = {
      kind: 'none',
      winner_entity_id: null,
      winner_team_id: null,
    };
    const frame = validateSessionSnapshotMessage(document, {
      messageSequence: 1,
      requestId: document.meta.request_id,
      tickSequence: null,
      npcCatalogue: legacyNpcCatalogue,
      terrain: raceTerrain,
    }).data;
    expect(
      describeMatchOverlay({
        entities: frame.entities,
        match: frame.match,
        ownControllerId: 3,
        ownEntityId: null,
      }),
    ).toEqual({
      title: 'Finished',
      detail:
        'You finished #1. The other racers can finish until the window closes.',
    });
  });
});

/** The cadence `config/blob-royale.cfg` authors and every published example is written against. */
const ABILITY_TICKS_PER_SECOND = 400;

/**
 * One published shield, authored here rather than borrowed from `shieldFrames`, for one reason: its
 * three spans -- 40 perfect ticks, 160 protection ticks and a 400-tick cooldown -- divide every tick
 * offset in the table below into an exact decimal, so a boundary case pins the arithmetic instead of
 * a rounding tolerance. The nesting is the wire's own, `activation <= perfect <= shield` and
 * `activation <= cooldown`, so the validator accepts it exactly as it accepts the shipped corpus.
 */
const ABILITY_SHIELD_WINDOWS = Object.freeze({
  activation_tick: 12800,
  shield_expiry_tick: 12960,
  perfect_expiry_tick: 12840,
  cooldown_expiry_tick: 13200,
  parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
});

/** A hundred-tick stun, chosen so the same offsets divide exactly against both its endpoints. */
const ABILITY_STUN_WINDOW = Object.freeze({
  activation_tick: 12800,
  expiry_tick: 12900,
});

/** A four-hundred-tick charge cooldown, for the same exact-decimal reason as the shield above. */
const ABILITY_CHARGE_COOLDOWN = Object.freeze({
  activation_tick: 12800,
  cooldown_expiry_tick: 13200,
});

/** Every member null: no tick to read against, no cadence, or no own entity on the frame. */
const NOTHING_PUBLISHED = { charge: null, shield: null, stun: null };

/**
 * Ability status read from a frame the real validator accepted. Every case goes through the
 * validator rather than a hand-built object so that a window no server could publish -- a reversed
 * stun, a charge cooldown collapsed onto its activation -- fails as a fixture instead of quietly
 * becoming a selector case that proves nothing.
 */
function abilityStatusInput(
  document: MutableSnapshotDocument,
): AbilityStatusInput {
  const frame = validateSessionSnapshotMessage(document, {
    messageSequence: 1,
    requestId: document.meta.request_id,
    tickSequence: null,
    npcCatalogue: legacyNpcCatalogue,
    terrain: solidTerrain,
  }).data;
  return {
    entities: frame.entities,
    ownEntityId: findOwnEntityId(frame.entities, 3),
    tickSequence: frame.tick_sequence,
    ticksPerSecond: ABILITY_TICKS_PER_SECOND,
  };
}

function abilityStatus(document: MutableSnapshotDocument) {
  return abilityStatusReport(abilityStatusInput(document));
}

describe('sessionSelectors for published ability windows', () => {
  it.each([
    {
      tick: 12800,
      expected: { isActive: true, remainingSeconds: 0.25, spentFraction: 0 },
    },
    {
      tick: 12850,
      expected: { isActive: true, remainingSeconds: 0.125, spentFraction: 0.5 },
    },
    {
      // The last tick the window covers. Half-open containment is what makes this the last one.
      tick: 12899,
      expected: {
        isActive: true,
        remainingSeconds: 0.0025,
        spentFraction: 0.99,
      },
    },
    {
      tick: 12900,
      expected: { isActive: false, remainingSeconds: 0, spentFraction: 1 },
    },
  ])('reads the stun window half-open at tick $tick', ({ tick, expected }) => {
    expect(
      abilityStatus(
        stunInputSnapshotDocument(undefined, ABILITY_STUN_WINDOW, tick),
      ).stun,
    ).toEqual(expected);
  });

  it('lets a stun merge grow the denominator and move the spent fraction backwards', () => {
    // The status system merges a repeated request by keeping the original activation and taking the
    // maximum expiry, so the denominator grows while the numerator's origin stays put. Both frames
    // here are read at the same tick, 80 ticks after one shared activation: those 80 ticks are 0.8
    // of the first window and 0.4 of the merged one. A fraction that moves backwards is the merged
    // window's truth, not a glitch, and no caller may assume monotonicity.
    const beforeMerge = abilityStatus(
      stunInputSnapshotDocument(undefined, ABILITY_STUN_WINDOW, 12880),
    );
    const afterMerge = abilityStatus(
      stunInputSnapshotDocument(
        undefined,
        { ...ABILITY_STUN_WINDOW, expiry_tick: 13000 },
        12880,
      ),
    );
    expect(beforeMerge.stun?.spentFraction).toBe(0.8);
    expect(afterMerge.stun?.spentFraction).toBe(0.4);
    expect(beforeMerge.stun?.remainingSeconds).toBe(0.05);
    expect(afterMerge.stun?.remainingSeconds).toBe(0.3);
  });

  it.each([
    {
      tick: 12800,
      cooldown: { isActive: true, remainingSeconds: 1, spentFraction: 0 },
      perfect: { isActive: true, remainingSeconds: 0.1, spentFraction: 0 },
      protection: { isActive: true, remainingSeconds: 0.4, spentFraction: 0 },
    },
    {
      // The last tick inside the perfect opening, and every window still running.
      tick: 12839,
      cooldown: {
        isActive: true,
        remainingSeconds: 0.9025,
        spentFraction: 0.0975,
      },
      perfect: {
        isActive: true,
        remainingSeconds: 0.0025,
        spentFraction: 0.975,
      },
      protection: {
        isActive: true,
        remainingSeconds: 0.3025,
        spentFraction: 0.24375,
      },
    },
    {
      // The opening has closed and ordinary protection continues: two states, not one.
      tick: 12840,
      cooldown: { isActive: true, remainingSeconds: 0.9, spentFraction: 0.1 },
      perfect: { isActive: false, remainingSeconds: 0, spentFraction: 1 },
      protection: {
        isActive: true,
        remainingSeconds: 0.3,
        spentFraction: 0.25,
      },
    },
    {
      // The third state, and the majority of a shield's published life by duration: no protection
      // at all, with the component still on the wire because its cooldown is still running.
      tick: 12960,
      cooldown: { isActive: true, remainingSeconds: 0.6, spentFraction: 0.4 },
      perfect: { isActive: false, remainingSeconds: 0, spentFraction: 1 },
      protection: { isActive: false, remainingSeconds: 0, spentFraction: 1 },
    },
    {
      tick: 13200,
      cooldown: { isActive: false, remainingSeconds: 0, spentFraction: 1 },
      perfect: { isActive: false, remainingSeconds: 0, spentFraction: 1 },
      protection: { isActive: false, remainingSeconds: 0, spentFraction: 1 },
    },
  ])(
    'reads all three shield windows against tick $tick',
    ({ tick, cooldown, perfect, protection }) => {
      expect(
        abilityStatus(shieldSnapshotDocument(ABILITY_SHIELD_WINDOWS, tick))
          .shield,
      ).toEqual({ cooldown, perfect, protection });
    },
  );

  it('reads a cancelled shield as no protection with a live cooldown, never as presence', () => {
    // A stun shortens still-live protection to the cancelling tick and leaves the cooldown running.
    // Both protection windows are then empty, so both report a null fraction rather than a ratio of
    // nothing, and neither is active: presence is not protection.
    const shield = abilityStatus(
      shieldSnapshotDocument(CANCELLED_SHIELD_WINDOWS, SHIELD_SNAPSHOT_TICK),
    ).shield;
    const emptyWindow = {
      isActive: false,
      remainingSeconds: 0,
      spentFraction: null,
    };
    expect(shield?.protection).toEqual(emptyWindow);
    expect(shield?.perfect).toEqual(emptyWindow);
    expect(shield?.cooldown.isActive).toBe(true);
    // A cancellation keeps the cooldown whole: 104 of its 360 ticks are spent at tick 12,904, and
    // the 256 that remain are 0.64 s at 400 ticks per second.
    expect(shield?.cooldown.remainingSeconds).toBe(0.64);
    expect(shield?.cooldown.spentFraction).toBeCloseTo(0.288889, 6);
  });

  it('guards the shield cooldown denominator the zero-cooldown frame collapses', () => {
    // `cooldown_expiry_tick === activation_tick` is authored tuning the configuration explicitly
    // permits, so this is a frame the server is required to be able to send. The span is empty, so
    // the fraction is null rather than Infinity or NaN, and an empty window is never active.
    const cooldown = abilityStatus(
      shieldSnapshotDocument(
        { ...SHIELD_WINDOWS, cooldown_expiry_tick: SHIELD_ACTIVATION_TICK },
        SHIELD_SNAPSHOT_TICK,
      ),
    ).shield?.cooldown;
    expect(cooldown).toEqual({
      isActive: false,
      remainingSeconds: 0,
      spentFraction: null,
    });

    // Charge's denominator cannot collapse the same way: its authored cooldown is validated
    // strictly positive, so the asymmetry with shield is the wire contract rather than an accident.
    const chargeFraction = abilityStatus(
      chargeSnapshotDocument(CHARGE_COOLDOWN),
    ).charge?.spentFraction;
    expect(chargeFraction).not.toBeNull();
    expect(chargeFraction).toBeGreaterThan(0);
    expect(chargeFraction).toBeLessThan(1);
  });

  it.each([
    {
      tick: 12800,
      expected: { isActive: true, remainingSeconds: 1, spentFraction: 0 },
    },
    {
      tick: 13000,
      expected: { isActive: true, remainingSeconds: 0.5, spentFraction: 0.5 },
    },
    {
      tick: 13199,
      expected: {
        isActive: true,
        remainingSeconds: 0.0025,
        spentFraction: 0.9975,
      },
    },
    {
      // The cooldown has elapsed. That is all this says: whether a charge would be admitted is the
      // server's safety envelope, which is deliberately not on the wire.
      tick: 13200,
      expected: { isActive: false, remainingSeconds: 0, spentFraction: 1 },
    },
  ])(
    'reads the charge cooldown half-open at tick $tick',
    ({ tick, expected }) => {
      expect(
        abilityStatus(chargeSnapshotDocument(ABILITY_CHARGE_COOLDOWN, tick))
          .charge,
      ).toEqual(expected);
    },
  );

  it('reports nothing for a frame that publishes no ability component at all', () => {
    expect(abilityStatus(snapshotDocument())).toEqual(NOTHING_PUBLISHED);
  });

  it('reports nothing without a tick to read against, a cadence, or an own entity', () => {
    // Each of these is a state the live client really reaches: the frames before the first snapshot
    // arrives, a configuration a caller has not resolved, and every frame of an eliminated player.
    // The empty report is what keeps this selector from dividing by a denominator it does not have.
    const input = abilityStatusInput(
      shieldSnapshotDocument(ABILITY_SHIELD_WINDOWS),
    );
    expect(abilityStatusReport(input).shield).not.toBeNull();
    expect(abilityStatusReport({ ...input, tickSequence: null })).toEqual(
      NOTHING_PUBLISHED,
    );
    expect(abilityStatusReport({ ...input, ticksPerSecond: 0 })).toEqual(
      NOTHING_PUBLISHED,
    );
    expect(abilityStatusReport({ ...input, ticksPerSecond: -1 })).toEqual(
      NOTHING_PUBLISHED,
    );
    expect(abilityStatusReport({ ...input, ownEntityId: null })).toEqual(
      NOTHING_PUBLISHED,
    );
    expect(abilityStatusReport({ ...input, ownEntityId: 4242 })).toEqual(
      NOTHING_PUBLISHED,
    );
    expect(abilityStatusReport({ ...input, entities: [] })).toEqual(
      NOTHING_PUBLISHED,
    );
  });

  it('reads the own blob only, never a peer carrying the same windows', () => {
    // Entity 8 is the bot in the golden roster. The HUD answers for the player reading it, and a
    // peer's cooldown on the player's own row would be a lie about what the player may do.
    const input = abilityStatusInput(
      shieldSnapshotDocument(ABILITY_SHIELD_WINDOWS),
    );
    expect(abilityStatusReport(input).shield).not.toBeNull();
    expect(abilityStatusReport({ ...input, ownEntityId: 8 })).toEqual(
      NOTHING_PUBLISHED,
    );
  });
});

/**
 * The frame every availability case is read from. `cursorSteeringConnection` supplies the live
 * client's own shape -- a validated welcome advertising both ability kinds, a running hill frame, an
 * owned body and a real sender -- and this adds only the ability state one case needs. Every frame
 * still goes through the real validator, for `stunInputConnection`'s reason: a window no server could
 * publish, an activation in its own frame's future or a reversed cooldown, must fail as a fixture
 * rather than quietly become an availability case that proves nothing.
 */
function abilityConnection(
  overrides: {
    readonly charge?: unknown;
    readonly scenario?: CameraSnapshotScenario;
    readonly shield?: unknown;
    readonly stun?: unknown;
    readonly tickSequence?: number;
  } = {},
): SimulationConnection {
  const session = cameraSessionIdentity();
  const connection = cursorSteeringConnection(
    vi.fn(() => true),
    session,
    overrides.scenario,
  );
  if (connection.snapshot === null) {
    throw new Error('TEST.ABILITY_AVAILABILITY_SNAPSHOT_MISSING');
  }
  const document = {
    ...connection.snapshot,
    data: {
      ...connection.snapshot.data,
      tick_sequence:
        overrides.tickSequence ?? connection.snapshot.data.tick_sequence,
      entities: connection.entities.map((entity) =>
        entity.entity_id === connection.ownEntityId
          ? {
              ...entity,
              components: {
                ...entity.components,
                ...(overrides.charge === undefined
                  ? {}
                  : { charge: overrides.charge }),
                ...(overrides.shield === undefined
                  ? {}
                  : { shield: overrides.shield }),
                ...(overrides.stun === undefined
                  ? {}
                  : { stun: overrides.stun }),
              },
            }
          : entity,
      ),
    },
  };
  const snapshot = validateSessionSnapshotMessage(document, {
    messageSequence: 1,
    requestId: document.meta.request_id,
    tickSequence: null,
    npcCatalogue: session.npcCatalogue,
    terrain: session.terrain,
  });
  return { ...connection, snapshot, entities: snapshot.data.entities };
}

function availability(
  connection: SimulationConnection,
  lastNonzeroAimDirection: ThrustDirection | null = THRUST_RIGHT,
): AbilityAvailabilityReport {
  return abilityAvailabilityReport({
    connection,
    lastNonzeroAimDirection,
    lobbyId: connection.session?.lobbyId ?? null,
  });
}

function abilityUnavailableOf(connection: SimulationConnection) {
  const lobbyId = connection.session?.lobbyId ?? null;
  return selectThrustInputOptions(connection, lobbyId).abilityUnavailable;
}

/** An open gate is the absence of a reason, never a claim that an activation would be accepted. */
function attemptable(kind: 'charge' | 'shield') {
  return { kind, canAttempt: true, reason: null, explanation: null };
}

function blocked(
  kind: 'charge' | 'shield',
  reason: AbilityUnavailableReason,
  explanation: string,
) {
  return { kind, canAttempt: false, reason, explanation };
}

describe('sessionSelectors for ability availability', () => {
  it('names no obstruction while every gate the client can see is open', () => {
    expect(availability(abilityConnection())).toEqual({
      charge: attemptable('charge'),
      shield: attemptable('shield'),
    });
  });

  it.each(['charge', 'shield'] as const)(
    'refuses %s alone when the welcome does not advertise that kind',
    (kind) => {
      // `enabled` checks `set_thrust` and nothing else, so without a per-kind read a control for an
      // unadvertised kind would look live and silently no-op: `SimulationApi.sendCommand` returns
      // false for a kind the welcome did not accept, before anything reaches the socket.
      const base = abilityConnection();
      const session = base.session;
      if (session === null) {
        throw new Error('TEST.ABILITY_AVAILABILITY_SESSION_MISSING');
      }
      const other = kind === 'charge' ? 'shield' : 'charge';
      const report = availability({
        ...base,
        session: {
          ...session,
          acceptedCommandKinds: session.acceptedCommandKinds.filter(
            (candidate) => candidate !== kind,
          ),
        },
      });
      expect(report[kind]).toEqual(
        blocked(
          kind,
          'not_advertised',
          `This room does not accept the ${kind} command.`,
        ),
      );
      expect(report[other]).toEqual(attemptable(other));
    },
  );

  it('folds every "there is no blob here" state into the one answer', () => {
    // Not connected, seated in another room, holding an entity whose body is gone, and holding no
    // entity at all are four different facts and one answer to the only question a control asks.
    // The connection's own status has a live region of its own at the top of `SimulationViewer`.
    const bodyless = availability(abilityConnection({ scenario: 'bodyless' }));
    expect(bodyless.shield).toEqual(
      blocked('shield', 'no_body', 'Your blob is not in play.'),
    );
    expect(bodyless.charge.reason).toBe('no_body');
    const missing = availability(abilityConnection({ scenario: 'missing' }));
    expect(missing.shield.reason).toBe('no_body');
    const retrying = availability({
      ...abilityConnection(),
      status: 'retrying',
    });
    expect(retrying.shield.reason).toBe('no_body');
    const otherRoom = abilityAvailabilityReport({
      connection: abilityConnection(),
      lastNonzeroAimDirection: THRUST_RIGHT,
      lobbyId: 4242,
    });
    expect(otherRoom.shield.reason).toBe('no_body');
  });

  it('refuses both abilities outside a running match', () => {
    // "Abilities are a running-match mechanic. A pulse in `lobby`, `countdown` or `ended` is refused
    // rather than held" -- `src/gameplay/shared/ability_system.cpp`. The blob still holds its body
    // on this frame, so the reason is the phase rather than the body. The same gate subsumes that
    // stage's tick-zero refusal: a match that has never run has never left `lobby`.
    const report = availability(abilityConnection({ scenario: 'lobby' }));
    expect(report.shield).toEqual(
      blocked('shield', 'not_running', 'The match is not running.'),
    );
    expect(report.charge).toEqual(
      blocked('charge', 'not_running', 'The match is not running.'),
    );
  });

  it.each([
    { tick: STUN_INPUT_WINDOW.expiry_tick - 1, stunned: true },
    { tick: STUN_INPUT_WINDOW.expiry_tick, stunned: false },
  ])('reads the input lock half-open at tick $tick', ({ tick, stunned }) => {
    // The same boolean the thrust sender obeys, read from the same `ownBodyFrame`, so a control
    // cannot say "stunned" on a frame the sender is still transmitting on.
    const report = availability(
      abilityConnection({ stun: STUN_INPUT_WINDOW, tickSequence: tick }),
    );
    expect(report.shield).toEqual(
      stunned
        ? blocked('shield', 'stunned', 'Your blob is stunned.')
        : attemptable('shield'),
    );
    expect(report.charge.reason).toBe(stunned ? 'stunned' : null);
  });

  it('lets live protection refuse both abilities, and says why for each', () => {
    // `shield_eligible` requires `!protection_active` because a re-tap would restart the perfect
    // opening, and the charge term requires it because a blob may not charge out of its own guard
    // (`src/gameplay/shared/ability_system.cpp`). The contract's § "Availability" names only the
    // charge half; this is the same term of the same expression.
    const inside = availability(
      abilityConnection({
        shield: SHIELD_WINDOWS,
        tickSequence: SHIELD_WINDOWS.shield_expiry_tick - 1,
      }),
    );
    expect(inside.shield).toEqual(
      blocked('shield', 'protection_active', 'Your shield is still up.'),
    );
    expect(inside.charge).toEqual(
      blocked(
        'charge',
        'protection_active',
        'You cannot charge out of your own shield.',
      ),
    );
    // The first tick protection does not cover; half-open containment is what makes it the first.
    // The shield falls through to its own live cooldown and the charge to nothing at all.
    const after = availability(
      abilityConnection({
        shield: SHIELD_WINDOWS,
        tickSequence: SHIELD_WINDOWS.shield_expiry_tick,
      }),
    );
    expect(after.shield.reason).toBe('cooling_down');
    expect(after.charge).toEqual(attemptable('charge'));
  });

  it.each([
    {
      kind: 'shield' as const,
      frame: (tick: number) =>
        abilityConnection({ shield: SHIELD_WINDOWS, tickSequence: tick }),
      liveTick: SHIELD_COOLDOWN_EXPIRY_TICK - 1,
      elapsedTick: SHIELD_COOLDOWN_EXPIRY_TICK,
      sentence: 'Shield is cooling down.',
    },
    {
      kind: 'charge' as const,
      frame: (tick: number) =>
        abilityConnection({ charge: CHARGE_COOLDOWN, tickSequence: tick }),
      liveTick: CHARGE_COOLDOWN_EXPIRY_TICK - 1,
      elapsedTick: CHARGE_COOLDOWN_EXPIRY_TICK,
      sentence: 'Charge is cooling down.',
    },
  ])(
    'suppresses $kind while its own published cooldown is live',
    ({ kind, frame, liveTick, elapsedTick, sentence }) => {
      // Suppression is the contract's primary rate mitigation as well as an explanation: the
      // per-session bucket is 30 burst and 20 refill per second, exhausting it closes the socket
      // with 1008 rather than refusing one command, and held thrust already runs at the refill rate.
      const live = availability(frame(liveTick));
      expect(live[kind]).toEqual(blocked(kind, 'cooling_down', sentence));
      // An elapsed cooldown is "cooldown over" and never "ready": the reason simply goes away, and
      // whether an activation would be admitted stays the server's answer to give.
      const elapsed = availability(frame(elapsedTick));
      expect(elapsed[kind]).toEqual(attemptable(kind));
    },
  );

  it('keeps each cooldown to the ability that published it', () => {
    // "The two cooldowns are separate keys on separate components and neither gates the other
    // ability" -- `src/gameplay/shared/ability_system.cpp`. One shared flag would make a spent
    // charge disable the guard a player needs in the same second.
    const report = availability(
      abilityConnection({
        charge: {
          activation_tick: CHARGE_ACTIVATION_TICK,
          cooldown_expiry_tick: 12900,
        },
        shield: SHIELD_WINDOWS,
        tickSequence: 13000,
      }),
    );
    expect(report.shield.reason).toBe('cooling_down');
    expect(report.charge).toEqual(attemptable('charge'));
  });

  it('reads the zero-cooldown shield frame without a divisor to guard', () => {
    // `cooldown_expiry_tick === activation_tick` is authored tuning the configuration permits, so it
    // is a frame the server is required to be able to send, and it is the one frame where nothing
    // but live protection refuses a re-tap. Containment answers both ticks: the empty span Step 20
    // had to return a null fraction for is simply never live, and nothing here divides at all.
    const zeroCooldown = {
      ...SHIELD_WINDOWS,
      cooldown_expiry_tick: SHIELD_ACTIVATION_TICK,
    };
    const guarded = availability(
      abilityConnection({
        shield: zeroCooldown,
        tickSequence: SHIELD_SNAPSHOT_TICK,
      }),
    );
    expect(guarded.shield.reason).toBe('protection_active');
    const afterProtection = availability(
      abilityConnection({
        shield: zeroCooldown,
        tickSequence: SHIELD_WINDOWS.shield_expiry_tick,
      }),
    );
    expect(afterProtection.shield).toEqual(attemptable('shield'));
  });

  it('states nothing from a frame with no tick, never that a window is over', () => {
    const live = abilityConnection({
      shield: SHIELD_WINDOWS,
      tickSequence: 13000,
    });
    expect(availability(live).shield.reason).toBe('cooling_down');
    // The live client cannot hold a body without a tick: entities are only ever set from a snapshot
    // (`useSimulationConnection`), so clearing one clears the other and the missing body answers.
    const disconnected = availability({
      ...live,
      snapshot: null,
      entities: [],
      ownEntityId: null,
    });
    expect(disconnected.shield.reason).toBe('no_body');
    // Forced past that into a state the reducer cannot produce, an unknown tick is silence rather
    // than a claim the cooldown elapsed: no window can contain a tick that is not there, nothing
    // divides, and nothing throws.
    const tickless = availability({ ...live, snapshot: null });
    expect(tickless.shield).toEqual(attemptable('shield'));
  });

  it.each([
    { name: 'a pointer that has never entered the arena', aim: null },
    { name: 'a remembered direction of zero magnitude', aim: THRUST_ZERO },
  ])('refuses charge alone with $name', ({ aim }) => {
    // A zero-magnitude direction "is a silent refusal that consumes no cooldown, never a scaled-down
    // burst" (`charge-command.schema.json`), so a control must not offer an attempt that vanishes.
    // Shield is unaffected in both cases: its pulse carries no direction at all.
    const report = availability(abilityConnection(), aim);
    expect(report.charge).toEqual(
      blocked(
        'charge',
        'no_aim',
        'Move the pointer over the arena to aim first.',
      ),
    );
    expect(report.shield).toEqual(attemptable('shield'));
  });

  it('names one reason at a time, and the earliest gate wins', () => {
    // Every gate is shut at once on this frame -- a stunned blob whose shield is up and whose charge
    // is cooling -- and a control shows one sentence. The order is the server's own gate order, so
    // the sentence names the gate that would really refuse the pulse first.
    const everything = {
      charge: CHARGE_COOLDOWN,
      shield: SHIELD_WINDOWS,
      stun: STUN_INPUT_WINDOW,
      tickSequence: STUN_INPUT_SNAPSHOT_TICK,
    };
    const stunned = abilityConnection(everything);
    const session = stunned.session;
    if (session === null) {
      throw new Error('TEST.ABILITY_AVAILABILITY_SESSION_MISSING');
    }
    const unseated = abilityConnection({ ...everything, scenario: 'bodyless' });
    expect(
      availability({
        ...unseated,
        session: { ...session, acceptedCommandKinds: [] },
      }).charge.reason,
    ).toBe('not_advertised');
    expect(availability(unseated).charge.reason).toBe('no_body');
    const lobby = abilityConnection({ ...everything, scenario: 'lobby' });
    expect(availability(lobby).charge.reason).toBe('not_running');
    expect(availability(stunned).charge.reason).toBe('stunned');
    const guarded = abilityConnection({
      ...everything,
      tickSequence: STUN_INPUT_WINDOW.expiry_tick,
    });
    expect(availability(guarded).charge.reason).toBe('protection_active');
    const cooling = abilityConnection({
      ...everything,
      tickSequence: SHIELD_WINDOWS.shield_expiry_tick,
    });
    expect(availability(cooling).charge.reason).toBe('cooling_down');
    const settled = abilityConnection({
      ...everything,
      tickSequence: CHARGE_COOLDOWN_EXPIRY_TICK,
    });
    expect(availability(settled, null).charge.reason).toBe('no_aim');
    expect(availability(settled).charge).toEqual(attemptable('charge'));
  });

  it('never claims readiness, in any state and in any wording', () => {
    // Charge readiness depends on the authored safety envelope, which is deliberately server-side
    // and never on the wire, and the shield's authored length is never published either. So the only
    // honest client statement is the absence of a visible obstruction, and neither "ready" nor
    // "available" may reach a player from here -- both would be a claim on exactly the frames a
    // player would act on one.
    const base = abilityConnection();
    const session = base.session;
    if (session === null) {
      throw new Error('TEST.ABILITY_AVAILABILITY_SESSION_MISSING');
    }
    const reports: AbilityAvailabilityReport[] = [
      availability(base),
      availability(base, null),
      availability({
        ...base,
        session: { ...session, acceptedCommandKinds: [] },
      }),
      availability(abilityConnection({ scenario: 'bodyless' })),
      availability(abilityConnection({ scenario: 'lobby' })),
      availability(
        abilityConnection({
          stun: STUN_INPUT_WINDOW,
          tickSequence: STUN_INPUT_SNAPSHOT_TICK,
        }),
      ),
      availability(
        abilityConnection({
          shield: SHIELD_WINDOWS,
          tickSequence: SHIELD_SNAPSHOT_TICK,
        }),
      ),
      availability(
        abilityConnection({ charge: CHARGE_COOLDOWN, tickSequence: 13000 }),
      ),
    ];
    const named = new Set<AbilityUnavailableReason>();
    for (const report of reports) {
      for (const control of [report.charge, report.shield]) {
        expect(JSON.stringify(control)).not.toMatch(/\b(ready|available)\b/i);
        if (control.canAttempt) {
          expect(control.reason).toBeNull();
          expect(control.explanation).toBeNull();
        } else {
          named.add(control.reason);
          expect(control.explanation.length).toBeGreaterThan(0);
        }
      }
    }
    // Every reason this selector can name is exercised above, so a new one cannot arrive untested.
    expect([...named].sort()).toEqual([
      'cooling_down',
      'no_aim',
      'no_body',
      'not_advertised',
      'not_running',
      'protection_active',
      'stunned',
    ]);
  });

  it('hands the input owner the published half of the same answer', () => {
    const cooling = abilityConnection({
      shield: SHIELD_WINDOWS,
      tickSequence: 13000,
    });
    expect(availability(cooling).shield.reason).toBe('cooling_down');
    expect(abilityUnavailableOf(cooling)).toEqual({
      charge: false,
      shield: true,
    });
    // A key press must never do what the control beside it says is impossible, so the suppression
    // carries the whole published composition and not the cooldown alone.
    const lobby = abilityConnection({ scenario: 'lobby' });
    expect(abilityUnavailableOf(lobby)).toEqual({ charge: true, shield: true });
  });

  it('keeps the client-local aim reason out of that suppression', () => {
    // Aim is the input owner's own state; feeding it back in through its options would be a loop,
    // and the contract has the hook resolve the direction inside its effect anyway. So the control
    // explains "no aim" while the sender's published suppression stays silent about it.
    const connection = abilityConnection();
    expect(availability(connection, null).charge.reason).toBe('no_aim');
    expect(abilityUnavailableOf(connection)).toEqual({
      charge: false,
      shield: false,
    });
  });

  it('interns the suppression so a render cannot retire the input owner', () => {
    // `SimulationFeature` rebuilds these options on every render, and the owner compares this
    // member by identity to decide whether anything changed. A fresh object here would report a
    // change on every frame -- rebuilding an input lifetime that must survive a cooldown merely
    // starting, and discarding remembered aim, which is discarded only on body loss, replacement,
    // a new welcome or disconnect.
    const first = abilityConnection({
      shield: SHIELD_WINDOWS,
      tickSequence: 13000,
    });
    const later = abilityConnection({
      shield: SHIELD_WINDOWS,
      tickSequence: 13001,
    });
    expect(abilityUnavailableOf(first)).toBe(abilityUnavailableOf(first));
    expect(abilityUnavailableOf(later)).toBe(abilityUnavailableOf(first));
    expect(abilityUnavailableOf(abilityConnection())).not.toBe(
      abilityUnavailableOf(first),
    );
  });
});
