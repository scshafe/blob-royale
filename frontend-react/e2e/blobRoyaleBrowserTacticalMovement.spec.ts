import { expect, test as playwrightTest } from '@playwright/test';
import type {
  SessionComponentOfKind,
  SessionEntitySnapshot,
  SessionPhysicsBodyComponent,
  SessionVector2,
  SessionWorldSnapshot,
} from '../src/features/simulation/simulationProtocolTypes';
import { BlobRoyaleServerProcess } from './BlobRoyaleServerProcess';
import { BrowserE2EError } from './BrowserE2EError';
import {
  CONNECTED_STATUS,
  connectionStatus,
  lobbyStartButton,
  matchHudCell,
  recordSessionTraffic,
  recordedSnapshots as snapshots,
  startMatchFromLobby,
  waitForReadyServer,
  type RecordedSessionTraffic,
} from './browserFlowSupport';

const MOVEMENT_FIXTURE = Object.freeze({
  configurationFileName: 'blob-royale-browser-e2e-tactical-movement.cfg',
  scenarioFileName: null,
  seekerProfile: 'seeker',
  coastProfile: 'coaster',
  hillCenter: Object.freeze({ x: 960, y: 640 }),
  hillRadius: 150,
  bodyRadius: 20,
  minimumRunningTicks: 800,
  maximumRunningTicks: 1200,
  minimumApproach: 8,
  maximumDisplacement: 30,
  minimumBodySeparation: 400,
  stationaryTolerance: 1e-9,
  observationTimeoutMilliseconds: 40_000,
});
const SPAWN_POSITIONS = Object.freeze([
  Object.freeze({ x: 300, y: 640 }),
  Object.freeze({ x: 1155, y: 640 }),
  Object.freeze({ x: 960, y: 1100 }),
]);

const test = playwrightTest.extend<{
  readonly blobRoyaleServer: BlobRoyaleServerProcess;
}>({
  blobRoyaleServer: async ({}, use) => {
    const server =
      await BlobRoyaleServerProcess.createFromEnvironment(MOVEMENT_FIXTURE);
    try {
      await use(server);
    } finally {
      await server.cleanup();
    }
  },
});

interface ObservedBody {
  readonly entityId: number;
  readonly controllerId: number;
  readonly body: SessionPhysicsBodyComponent;
}

interface MovementFrame {
  readonly snapshot: SessionWorldSnapshot;
  readonly seeker: ObservedBody;
  readonly coaster: ObservedBody;
  readonly human: ObservedBody;
  readonly hill: SessionComponentOfKind<'hill'>;
}

function observedBody(
  entities: readonly SessionEntitySnapshot[],
  controllerId: number | null | undefined,
): ObservedBody | null {
  if (controllerId === null || controllerId === undefined) return null;
  const entity = entities.find(
    (candidate) =>
      candidate.components.controllable?.controller_id === controllerId,
  );
  const body = entity?.components.physics_body;
  if (entity === undefined || body === undefined) return null;
  return { entityId: entity.entity_id, controllerId, body };
}

/** Resolve profiles through authored seat declarations, never display names or allocation order. */
function movementFrame(snapshot: SessionWorldSnapshot): MovementFrame | null {
  const seekerSeat = snapshot.match.seats.find(
    (seat) =>
      seat.kind === 'npc' &&
      seat.npc_kind === 'tactical' &&
      seat.profile_name === MOVEMENT_FIXTURE.seekerProfile,
  );
  const coasterSeat = snapshot.match.seats.find(
    (seat) =>
      seat.kind === 'npc' &&
      seat.npc_kind === 'tactical' &&
      seat.profile_name === MOVEMENT_FIXTURE.coastProfile,
  );
  const humanSeat = snapshot.match.seats.find(
    (seat) => seat.kind === 'controller',
  );
  if (
    seekerSeat?.kind !== 'npc' ||
    coasterSeat?.kind !== 'npc' ||
    humanSeat?.kind !== 'controller'
  )
    return null;
  const seeker = observedBody(snapshot.entities, seekerSeat.controller_id);
  const coaster = observedBody(snapshot.entities, coasterSeat.controller_id);
  const human = observedBody(snapshot.entities, humanSeat.controller_id);
  const hills = snapshot.entities.filter(
    (entity) => entity.components.hill !== undefined,
  );
  const hill = hills[0]?.components.hill;
  if (
    seeker === null ||
    coaster === null ||
    human === null ||
    hills.length !== 1 ||
    hill === undefined
  )
    return null;
  return { snapshot, seeker, coaster, human, hill };
}

function requireMovementFrame(snapshot: SessionWorldSnapshot): MovementFrame {
  const frame = movementFrame(snapshot);
  if (frame === null) {
    throw new BrowserE2EError(
      'BROWSER_E2E.TACTICAL_MOVEMENT_FRAME_INCOMPLETE',
      'The isolated running fixture must publish both profiled bots, its human, and one hill.',
      { tick_sequence: snapshot.tick_sequence, phase: snapshot.match.phase },
    );
  }
  return frame;
}

function latestLobbyFrame(
  traffic: RecordedSessionTraffic,
): MovementFrame | null {
  const snapshot = snapshots(traffic)
    .filter((value) => value.match.phase === 'lobby')
    .at(-1);
  return snapshot === undefined ? null : movementFrame(snapshot);
}

/** A retained early-running publication; wall-clock polling never extends its isolation window. */
function targetSnapshot(
  traffic: RecordedSessionTraffic,
): SessionWorldSnapshot | undefined {
  return snapshots(traffic).find((snapshot) => {
    const runningTicks =
      snapshot.tick_sequence - snapshot.match.phase_started_tick;
    return (
      snapshot.match.phase === 'running' &&
      runningTicks >= MOVEMENT_FIXTURE.minimumRunningTicks &&
      runningTicks <= MOVEMENT_FIXTURE.maximumRunningTicks
    );
  });
}

function distance(left: SessionVector2, right: SessionVector2): number {
  return Math.hypot(left.x - right.x, left.y - right.y);
}

function expectStationary(
  observed: ObservedBody,
  baseline: ObservedBody,
): void {
  expect(observed.entityId).toBe(baseline.entityId);
  expect(observed.controllerId).toBe(baseline.controllerId);
  expect(
    distance(observed.body.position, baseline.body.position),
  ).toBeLessThanOrEqual(MOVEMENT_FIXTURE.stationaryTolerance);
  expect(
    Math.hypot(observed.body.velocity.x, observed.body.velocity.y),
  ).toBeLessThanOrEqual(MOVEMENT_FIXTURE.stationaryTolerance);
  expect(
    Math.hypot(observed.body.acceleration.x, observed.body.acceleration.y),
  ).toBeLessThanOrEqual(MOVEMENT_FIXTURE.stationaryTolerance);
}

test('a real tactical seeker moves toward the hill while its coast profile and browser remain still', async ({
  blobRoyaleServer,
  page,
  request,
}) => {
  // This is a bounded functional smoke, not a throughput or real-time performance assertion.
  test.setTimeout(75_000);
  const pageErrors: string[] = [];
  page.on('pageerror', (error) => pageErrors.push(error.message));
  await blobRoyaleServer.start();
  await waitForReadyServer(request, blobRoyaleServer);
  const traffic = recordSessionTraffic(page);
  const response = await page.goto('/?lobby=1', {
    waitUntil: 'domcontentloaded',
  });
  expect(response?.status()).toBe(200);
  await expect(connectionStatus(page)).toHaveText(CONNECTED_STATUS);
  await expect(matchHudCell(page, 'Phase')).toHaveText('lobby');
  await expect(page.locator('.SeatCard')).toHaveCount(3);
  await expect(page.locator('.SeatCard').nth(0)).toContainText('/ seeker');
  await expect(page.locator('.SeatCard').nth(1)).toContainText('/ coaster');
  await expect(lobbyStartButton(page)).toBeEnabled();
  await expect
    .poll(() => latestLobbyFrame(traffic) !== null, {
      message:
        'both configured tactical profiles and the third human must publish bodies in the lobby',
      timeout: MOVEMENT_FIXTURE.observationTimeoutMilliseconds,
    })
    .toBe(true);
  const baseline = latestLobbyFrame(traffic);
  if (baseline === null) {
    throw new BrowserE2EError(
      'BROWSER_E2E.TACTICAL_LOBBY_BASELINE_MISSING',
      'The completed lobby observation must remain available before Start is pressed.',
    );
  }
  expect(baseline.snapshot.match.mode).toBe('king_of_the_hill');
  expect(baseline.snapshot.match.seats[2]?.kind).toBe('controller');
  expect(baseline.hill).toEqual({
    center: MOVEMENT_FIXTURE.hillCenter,
    radius: MOVEMENT_FIXTURE.hillRadius,
  });
  expect(
    [baseline.seeker, baseline.coaster, baseline.human]
      .map((participant) => participant.body.position)
      .sort((left, right) => left.x - right.x),
  ).toEqual([...SPAWN_POSITIONS].sort((left, right) => left.x - right.x));
  for (const participant of [
    baseline.seeker,
    baseline.coaster,
    baseline.human,
  ]) {
    expect(participant.body.radius).toBe(MOVEMENT_FIXTURE.bodyRadius);
    expect(participant.body.is_static).toBe(false);
    expectStationary(participant, participant);
    expect(
      distance(participant.body.position, baseline.hill.center),
    ).toBeGreaterThan(baseline.hill.radius);
  }

  // The only browser command is the real lobby Start button. No keyboard or injected bot input.
  await startMatchFromLobby(page);
  await expect(matchHudCell(page, 'Phase')).toHaveText('running', {
    timeout: MOVEMENT_FIXTURE.observationTimeoutMilliseconds,
  });
  await expect
    .poll(() => targetSnapshot(traffic) !== undefined, {
      message:
        'the real host/runtime must publish a running frame in the fixed two-to-three-second window',
      timeout: MOVEMENT_FIXTURE.observationTimeoutMilliseconds,
    })
    .toBe(true);
  const target = targetSnapshot(traffic);
  if (target === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.TACTICAL_MOVEMENT_WINDOW_MISSING',
      'The recorder must retain the isolated early-running observation.',
    );
  }
  const frame = requireMovementFrame(target);
  const approach =
    distance(baseline.seeker.body.position, baseline.hill.center) -
    distance(frame.seeker.body.position, baseline.hill.center);
  expect(approach).toBeGreaterThan(MOVEMENT_FIXTURE.minimumApproach);
  const toHill = {
    x: frame.hill.center.x - frame.seeker.body.position.x,
    y: frame.hill.center.y - frame.seeker.body.position.y,
  };
  expect(
    frame.seeker.body.velocity.x * toHill.x +
      frame.seeker.body.velocity.y * toHill.y,
  ).toBeGreaterThan(0);
  expect(
    frame.seeker.body.acceleration.x * toHill.x +
      frame.seeker.body.acceleration.y * toHill.y,
  ).toBeGreaterThan(0);

  const isolatedSnapshots = snapshots(traffic).filter(
    (snapshot) =>
      snapshot.match.phase === 'running' &&
      snapshot.match.phase_started_tick === target.match.phase_started_tick &&
      snapshot.tick_sequence <= target.tick_sequence,
  );
  expect(isolatedSnapshots.length).toBeGreaterThan(1);
  for (const snapshot of isolatedSnapshots) {
    const observed = requireMovementFrame(snapshot);
    expect(observed.hill).toEqual(baseline.hill);
    expect(observed.seeker.entityId).toBe(baseline.seeker.entityId);
    expect(observed.seeker.controllerId).toBe(baseline.seeker.controllerId);
    expectStationary(observed.coaster, baseline.coaster);
    expectStationary(observed.human, baseline.human);
    expect(
      distance(observed.seeker.body.position, baseline.seeker.body.position),
    ).toBeLessThan(MOVEMENT_FIXTURE.maximumDisplacement);
    for (const stationary of [observed.coaster, observed.human]) {
      expect(
        distance(observed.seeker.body.position, stationary.body.position),
      ).toBeGreaterThan(MOVEMENT_FIXTURE.minimumBodySeparation);
    }
  }
  expect(traffic.sentFrames.map((encoded) => JSON.parse(encoded))).toEqual([
    { kind: 'start_match', payload: {} },
  ]);
  expect(pageErrors).toEqual([]);
  await blobRoyaleServer.terminateWithSigterm();
});
