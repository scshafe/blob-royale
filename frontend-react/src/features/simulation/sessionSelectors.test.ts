import { raceTerrain, solidTerrain } from './fixtures/terrainFrames';
import { describe, expect, it, vi } from 'vitest';
import { cameraSessionIdentity } from './fixtures/simulationCameraFrames';
import {
  STUN_INPUT_NEXT_GENERATION,
  STUN_INPUT_SNAPSHOT_TICK,
  STUN_INPUT_WINDOW,
  stunInputConnection,
} from './fixtures/stunInputFrames';

import {
  legacyNpcCatalogue,
  hillSnapshotDocument,
  raceScenarioDocument,
  type RaceSnapshotScenario,
  snapshotDocument,
} from './fixtures/sessionFrames';
import { validateSessionSnapshotMessage } from './sessionProtocolValidation';
import {
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
      },
      {
        controllerId: 4,
        displayName: 'wanderer-1',
        entityId: 8,
        isOwn: false,
        placement: 1,
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
        'The first finishers crossed on the same tick. The next lobby opens shortly.',
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
