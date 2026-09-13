import { expect, test as playwrightTest, type Page } from '@playwright/test';
import { raceModeState } from '../src/features/simulation/sessionSelectors';
import type {
  SessionEntitySnapshot,
  SessionWorldSnapshot,
} from '../src/features/simulation/simulationProtocolTypes';
import { HILL_FILL } from '../src/features/simulation/rendering/hillRenderer';
import { TERRAIN_CLIFF_RIM } from '../src/features/simulation/rendering/terrainRenderer';
import { BlobRoyaleServerProcess } from './BlobRoyaleServerProcess';
import { BrowserE2EError } from './BrowserE2EError';
import {
  CONNECTED_STATUS,
  aimFromPaintedBody,
  connectionStatus,
  entityForController,
  findLabel,
  focusSimulationCanvas,
  installCanvasRecorder,
  matchHudCell,
  recordedCommands,
  recordedSnapshots,
  recordedWelcome,
  recordSessionTraffic,
  requireCanvasFrame,
  requireWorldCanvasFrame,
  startMatchFromLobby,
  waitForReadyServer,
  type RecordedSessionTraffic,
} from './browserFlowSupport';

/** The derivation and geometry are authored beside the four dynamic configuration files. */
const DYNAMIC = Object.freeze({
  bodyRadius: 2,
  start: Object.freeze({ x: 300, y: 640 }),
  target: Object.freeze({ x: 310, y: 640 }),
  firstGate: Object.freeze({ x: 304, y: 640 }),
  finishEntryX: 315,
  freeDisplacement: 18.65625,
  hillVoid: Object.freeze({ x: 310, y: 800, radius: 80 }),
  observationTimeoutMilliseconds: 20_000,
});

type DynamicFixtureName = 'hill' | 'collision' | 'race-fall' | 'race-finish';
const test = playwrightTest.extend<{
  readonly dynamicFixtureName: DynamicFixtureName;
  readonly blobRoyaleServer: BlobRoyaleServerProcess;
}>({
  dynamicFixtureName: ['hill', { option: true }],
  blobRoyaleServer: async ({ dynamicFixtureName, request }, use) => {
    const server = await BlobRoyaleServerProcess.createFromEnvironment({
      configurationFileName: `blob-royale-browser-e2e-dynamic-${dynamicFixtureName}.cfg`,
      scenarioFileName: null,
    });
    try {
      await server.start();
      await waitForReadyServer(request, server);
      await use(server);
    } finally {
      await server.cleanup();
    }
  },
});

interface DynamicSession {
  readonly page: Page;
  readonly traffic: RecordedSessionTraffic;
  readonly controllerId: number;
  readonly displayName: string;
}

async function openSession(page: Page): Promise<DynamicSession> {
  const traffic = recordSessionTraffic(page);
  await installCanvasRecorder(page);
  expect(
    (
      await page.goto('/?lobby=1', {
        waitUntil: 'domcontentloaded',
      })
    )?.status(),
  ).toBe(200);
  await expect(connectionStatus(page)).toHaveText(CONNECTED_STATUS);
  await expect.poll(() => recordedWelcome(traffic) !== undefined).toBe(true);
  const welcome = recordedWelcome(traffic);
  if (welcome === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.DYNAMIC_WELCOME_ABSENT',
      'The real session must publish its welcome before geometry is observed.',
    );
  }
  return {
    page,
    traffic,
    controllerId: welcome.data.controller_id,
    displayName: welcome.data.display_name,
  };
}

/** Retain a qualifying publication even when the current DOM has already advanced beyond it. */
async function awaitSnapshot(
  session: DynamicSession,
  predicate: (snapshot: SessionWorldSnapshot) => boolean,
  message: string,
): Promise<SessionWorldSnapshot> {
  await expect
    .poll(() => recordedSnapshots(session.traffic).some(predicate), {
      message,
      timeout: DYNAMIC.observationTimeoutMilliseconds,
    })
    .toBe(true);
  const snapshot = recordedSnapshots(session.traffic).find(predicate);
  if (snapshot === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.DYNAMIC_PUBLICATION_ABSENT',
      message,
      { controller_id: session.controllerId },
    );
  }
  return snapshot;
}

function requireOwnEntity(
  session: DynamicSession,
  snapshot: SessionWorldSnapshot,
): SessionEntitySnapshot {
  const entity = entityForController(snapshot, session.controllerId);
  if (entity === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.DYNAMIC_ENTITY_ABSENT',
      'The returning or racing participant must retain its published controller identity.',
      {
        controller_id: session.controllerId,
        tick_sequence: snapshot.tick_sequence,
      },
    );
  }
  return entity;
}

async function startAndObserve(
  session: DynamicSession,
): Promise<SessionWorldSnapshot> {
  await startMatchFromLobby(session.page);
  await expect(matchHudCell(session.page, 'Phase')).toHaveText('running');
  return awaitSnapshot(
    session,
    (snapshot) => snapshot.match.phase === 'running',
    'the configured match must publish its running phase',
  );
}

/** No steering acceleration exists in these fixtures: the body stays at its authored start. */
async function aimCharge(
  session: DynamicSession,
  holdSpace: boolean,
): Promise<void> {
  await focusSimulationCanvas(session.page);
  await aimFromPaintedBody(
    session.page,
    session.displayName,
    { x: 100, y: 0 },
    DYNAMIC.bodyRadius,
  );
  if (holdSpace) {
    await session.page.keyboard.down('Space');
    await expect(matchHudCell(session.page, 'Thrust')).toHaveText('1.00, 0.00');
  }
  await session.page.keyboard.press('KeyD');
}

function expectSingleCharge(session: DynamicSession): void {
  const charges = recordedCommands(session.traffic).filter(
    (command) => command.kind === 'charge',
  );
  expect(charges).toHaveLength(1);
  expect(charges[0]?.payload).toMatchObject({ x: 1, y: 0 });
}

function expectClearedBodyState(entity: SessionEntitySnapshot): void {
  expect(entity.components.charge).toBeUndefined();
  expect(entity.components.shield).toBeUndefined();
  expect(entity.components.stun).toBeUndefined();
}

test.describe('random roaming and hole support', () => {
  test.use({ dynamicFixtureName: 'hill' });
  test('a roaming hill crosses dangerous ground while a charged player falls through its small hole and returns', async ({
    blobRoyaleServer,
    page,
    browser,
  }) => {
    test.setTimeout(60_000);
    const errors: string[] = [];
    page.on('pageerror', (error) => errors.push(error.message));
    const session = await openSession(page);
    const peerContext = await browser.newContext({
      baseURL: 'http://127.0.0.1:5173',
    });
    try {
      const peerPage = await peerContext.newPage();
      peerPage.on('pageerror', (error) => errors.push(error.message));
      const peer = await openSession(peerPage);
      const terrain = recordedWelcome(session.traffic)?.data.terrain;
      expect(terrain?.bounds).toEqual({
        width_world_units: 1920,
        height_world_units: 1280,
      });
      expect(terrain?.holes).toEqual([
        { name: 'charge_gap', center: DYNAMIC.target, radius: 2 },
        { name: 'hill_void', center: { x: 310, y: 800 }, radius: 80 },
      ]);
      const baseline = await startAndObserve(session);
      // Hill attrition ends a running round when its only player loses a body. The distant
      // untouched peer keeps this a return witness rather than a round-restart witness.
      const peerBody = requireOwnEntity(peer, baseline).components.physics_body;
      expect(peerBody?.position).toEqual({ x: 1600, y: 640 });
      expect(peerBody?.velocity).toEqual({ x: 0, y: 0 });
      expect(
        requireOwnEntity(session, baseline).components.physics_body?.position,
      ).toEqual(DYNAMIC.start);
      const later = await awaitSnapshot(
        session,
        (snapshot) =>
          snapshot.match.phase === 'running' &&
          snapshot.tick_sequence >= baseline.tick_sequence + 400,
        'the hill must continue retargeting across a full second of committed running ticks',
      );
      expect(later.random_draw_counts.hill).toBeGreaterThanOrEqual(
        baseline.random_draw_counts.hill + 8,
      );
      const hills = recordedSnapshots(session.traffic)
        .filter(
          (snapshot) =>
            snapshot.match.phase === 'running' &&
            snapshot.tick_sequence <= later.tick_sequence,
        )
        .flatMap((snapshot) =>
          snapshot.entities.filter(
            (entity) => entity.components.hill !== undefined,
          ),
        );
      expect(hills.length).toBeGreaterThan(2);
      expect(
        new Set(
          hills.map((entity) => JSON.stringify(entity.components.hill?.center)),
        ).size,
      ).toBeGreaterThan(1);
      expect(
        new Set(
          hills.map((entity) =>
            JSON.stringify(entity.components.hill_motion?.velocity),
          ),
        ).size,
      ).toBeGreaterThan(1);
      for (const entity of hills) {
        const hill = entity.components.hill;
        expect(hill).toBeDefined();
        expect(entity.components.hill_motion).toBeDefined();
        if (hill !== undefined) {
          expect(
            Math.hypot(
              hill.center.x - DYNAMIC.hillVoid.x,
              hill.center.y - DYNAMIC.hillVoid.y,
            ),
          ).toBeLessThan(DYNAMIC.hillVoid.radius);
        }
      }
      // The renderer must consume roaming centers, not leave a correct-radius disc at its marker.
      // Match a camera-normalized paint to the recent real wire history; the recorder and browser
      // paint clocks differ, so the newest WebSocket frame need not be the frame just painted.
      async function observedPaintedHill() {
        const frame = await requireWorldCanvasFrame(page);
        const arc = frame.arcs.find((value) => value.fillStyle === HILL_FILL);
        if (arc === undefined) {
          throw new BrowserE2EError(
            'BROWSER_E2E.DYNAMIC_HILL_PAINT_MISSING',
            'A roaming hill must paint its published center and radius.',
            { frame_index: frame.index },
          );
        }
        expect(arc.radius).toBe(250);
        const snapshots = recordedSnapshots(session.traffic);
        const latestTick = snapshots.at(-1)!.tick_sequence;
        const matchesRecentCenter = snapshots.some(
          (snapshot) =>
            snapshot.tick_sequence >= latestTick - 400 &&
            snapshot.entities.some((entity) => {
              const hill = entity.components.hill;
              return (
                hill !== undefined &&
                Math.abs(arc.x - hill.center.x) < 1e-6 &&
                Math.abs(arc.y - hill.center.y) < 1e-6
              );
            }),
        );
        expect(matchesRecentCenter).toBe(true);
        return { frame, center: { x: arc.x, y: arc.y }, latestTick };
      }
      const firstPaint = await observedPaintedHill();
      await awaitSnapshot(
        session,
        (snapshot) => snapshot.tick_sequence >= firstPaint.latestTick + 400,
        'later published roaming must also advance the actual painted hill center',
      );
      const secondPaint = await observedPaintedHill();
      expect(
        Math.hypot(
          secondPaint.center.x - firstPaint.center.x,
          secondPaint.center.y - firstPaint.center.y,
        ),
      ).toBeGreaterThan(0.1);
      const painted = secondPaint.frame;
      expect(
        painted.strokedArcs
          .filter((arc) => arc.strokeStyle === TERRAIN_CLIFF_RIM)
          .map(({ x, y, radius }) => ({ x, y, radius })),
      ).toEqual([
        { x: 310, y: 640, radius: 2 },
        { x: 310, y: 800, radius: 80 },
      ]);
      const raw = await requireCanvasFrame(page);
      expect(raw.worldBoundary?.width).toBeGreaterThan(raw.width);
      expect(raw.worldBoundary?.height).toBeGreaterThan(raw.height);

      // Center support is lost even while the scoring hill covers the small hole. A charge
      // component is body-bound and disappears in this very quantum, so the bodyless publication,
      // sole movement pulse, and independently authored endpoint are the browser's evidence.
      const beforeChargeTick =
        recordedSnapshots(session.traffic).at(-1)?.tick_sequence ?? 0;
      expect(DYNAMIC.start.x + DYNAMIC.freeDisplacement).toBeGreaterThan(
        DYNAMIC.target.x + 2,
      );
      try {
        await aimCharge(session, true);
        const fallen = await awaitSnapshot(
          session,
          (snapshot) =>
            snapshot.tick_sequence > beforeChargeTick &&
            entityForController(snapshot, session.controllerId)?.components
              .respawn_timer !== undefined,
          'a charge must terminate at the hole despite a supported free endpoint beyond it',
        );
        const absent = requireOwnEntity(session, fallen);
        expect(absent.components.physics_body).toBeUndefined();
        expectClearedBodyState(absent);
        const coveringHill = fallen.entities.find(
          (entity) => entity.components.hill !== undefined,
        )?.components.hill;
        expect(coveringHill).toBeDefined();
        if (coveringHill !== undefined) {
          expect(
            Math.hypot(
              coveringHill.center.x - DYNAMIC.target.x,
              coveringHill.center.y - DYNAMIC.target.y,
            ),
          ).toBeLessThan(coveringHill.radius);
        }
        await expect(matchHudCell(page, 'Hill')).toHaveText(
          /^Back in [0-9]+\.[0-9] s$/,
        );
        expect(
          findLabel(await requireWorldCanvasFrame(page), session.displayName),
        ).toBeNull();
        await expect(matchHudCell(page, 'Thrust')).toHaveText('idle');
        const returned = await awaitSnapshot(
          session,
          (snapshot) =>
            snapshot.tick_sequence > fallen.tick_sequence &&
            entityForController(snapshot, session.controllerId)?.components
              .physics_body !== undefined,
          'the same player must return at its safe authored spawn',
        );
        expect(returned.match.phase).toBe('running');
        expect(
          requireOwnEntity(peer, returned).components.physics_body,
        ).toEqual(peerBody);
        expect(recordedCommands(peer.traffic)).toEqual([]);
        const own = requireOwnEntity(session, returned);
        expect(own.entity_id).toBe(absent.entity_id);
        expect(own.components.physics_body?.position).toEqual(DYNAMIC.start);
        expect(own.components.physics_body?.velocity).toEqual({ x: 0, y: 0 });
        expect(own.components.physics_body?.acceleration).toEqual({
          x: 0,
          y: 0,
        });
        expectClearedBodyState(own);
        const commandCount = recordedCommands(session.traffic).length;
        const repeatTick = recordedSnapshots(session.traffic).at(
          -1,
        )!.tick_sequence;
        await page.keyboard.down('Space');
        await awaitSnapshot(
          session,
          (snapshot) => snapshot.tick_sequence >= repeatTick + 80,
          'a held-key repeat must remain invalidated across later publications',
        );
        await expect(matchHudCell(page, 'Thrust')).toHaveText('idle');
        expect(recordedCommands(session.traffic)).toHaveLength(commandCount);
        expectSingleCharge(session);
      } finally {
        await page.keyboard.up('Space');
      }
      await blobRoyaleServer.terminateWithSigterm();
      expect(errors).toEqual([]);
    } finally {
      await peerContext.close();
    }
  });
});

test.describe('swept body contact', () => {
  test.use({ dynamicFixtureName: 'collision' });
  test('a charge transfers momentum to a body its free first-tick endpoint would miss', async ({
    blobRoyaleServer,
    page,
    browser,
  }) => {
    const errors: string[] = [];
    page.on('pageerror', (error) => errors.push(error.message));
    const session = await openSession(page);
    const peerContext = await browser.newContext({
      baseURL: 'http://127.0.0.1:5173',
    });
    try {
      const peerPage = await peerContext.newPage();
      peerPage.on('pageerror', (error) => errors.push(error.message));
      const peer = await openSession(peerPage);
      const baseline = await startAndObserve(session);
      expect(
        requireOwnEntity(session, baseline).components.physics_body?.position,
      ).toEqual(DYNAMIC.start);
      expect(
        entityForController(baseline, peer.controllerId)?.components
          .physics_body?.position,
      ).toEqual(DYNAMIC.target);
      expect(DYNAMIC.start.x + DYNAMIC.freeDisplacement).toBeGreaterThan(
        DYNAMIC.target.x + 2 * DYNAMIC.bodyRadius,
      );
      await aimCharge(session, false);
      const collided = await awaitSnapshot(
        session,
        (snapshot) =>
          snapshot.tick_sequence > baseline.tick_sequence &&
          (entityForController(snapshot, peer.controllerId)?.components
            .physics_body?.velocity.x ?? 0) > 1000,
        'the stationary peer must acquire positive momentum from the swept charge contact',
      );
      const charger = requireOwnEntity(session, collided);
      const target = entityForController(collided, peer.controllerId);
      expect(charger.components.charge).toBeDefined();
      expect(charger.components.physics_body).toBeDefined();
      expect(target?.components.physics_body?.position.x).toBeGreaterThan(
        DYNAMIC.target.x,
      );
      expect(target?.components.charge).toBeUndefined();
      expect(recordedCommands(peer.traffic)).toEqual([]);
      expectSingleCharge(session);
      await blobRoyaleServer.terminateWithSigterm();
    } finally {
      await peerContext.close();
    }
    expect(errors).toEqual([]);
  });
});

test.describe('chronological race support loss', () => {
  test.use({ dynamicFixtureName: 'race-fall' });
  test('a charged racer retains the earlier gate, falls before the finish, and returns to that gate', async ({
    blobRoyaleServer,
    page,
  }) => {
    const errors: string[] = [];
    page.on('pageerror', (error) => errors.push(error.message));
    const session = await openSession(page);
    const baseline = await startAndObserve(session);
    expect(
      requireOwnEntity(session, baseline).components.race_progress
        ?.next_checkpoint,
    ).toBe(0);
    const painted = await requireWorldCanvasFrame(page);
    expect(
      painted.arcs
        .filter((arc) => arc.radius === 1)
        .map(({ x, y }) => ({ x, y })),
    ).toEqual([
      { x: 304, y: 640 },
      { x: 316, y: 640 },
    ]);
    await aimCharge(session, false);
    const fallen = await awaitSnapshot(
      session,
      (snapshot) =>
        snapshot.tick_sequence > baseline.tick_sequence &&
        entityForController(snapshot, session.controllerId)?.components
          .respawn_timer !== undefined,
      'the early gate must commit while later unsupported motion cannot finish',
    );
    const absent = requireOwnEntity(session, fallen);
    expect(absent.components.physics_body).toBeUndefined();
    expect(absent.components.race_progress?.next_checkpoint).toBe(1);
    expect(raceModeState(fallen.match)?.standings).toEqual([]);
    expect(fallen.match.phase).toBe('running');
    expectClearedBodyState(absent);
    const returned = await awaitSnapshot(
      session,
      (snapshot) =>
        snapshot.tick_sequence > fallen.tick_sequence &&
        entityForController(snapshot, session.controllerId)?.components
          .physics_body !== undefined,
      'the body must return to the earlier gate without being credited with the later finish',
    );
    const own = requireOwnEntity(session, returned);
    expect(own.entity_id).toBe(absent.entity_id);
    expect(own.components.physics_body?.position).toEqual(DYNAMIC.firstGate);
    expect(own.components.physics_body?.velocity).toEqual({ x: 0, y: 0 });
    expect(own.components.race_progress?.next_checkpoint).toBe(1);
    expect(raceModeState(returned.match)?.standings).toEqual([]);
    for (const snapshot of recordedSnapshots(session.traffic).filter(
      (value) =>
        value.tick_sequence >= fallen.tick_sequence &&
        value.tick_sequence <= returned.tick_sequence,
    )) {
      expect(
        requireOwnEntity(session, snapshot).components.race_progress
          ?.next_checkpoint,
      ).toBe(1);
      expect(raceModeState(snapshot.match)?.standings).toEqual([]);
    }
    expectSingleCharge(session);
    await blobRoyaleServer.terminateWithSigterm();
    expect(errors).toEqual([]);
  });
});

test.describe('supported swept race finish', () => {
  test.use({ dynamicFixtureName: 'race-finish' });
  test('one real charge crosses three ordered gates and stops at its published fractional finish', async ({
    blobRoyaleServer,
    page,
  }) => {
    const errors: string[] = [];
    page.on('pageerror', (error) => errors.push(error.message));
    const session = await openSession(page);
    const running = await startAndObserve(session);
    // The transition to running is committed before the next lifecycle pass initializes
    // RaceProgress. A 20 Hz frame may expose that valid transition tick; wait for the actual
    // pre-charge progress value, keeping the zero-gates assertion below exact.
    const baseline = await awaitSnapshot(
      session,
      (snapshot) =>
        snapshot.tick_sequence >= running.tick_sequence &&
        entityForController(snapshot, session.controllerId)?.components
          .race_progress !== undefined,
      'the running race must initialize progress before the charge witness begins',
    );
    expect(
      requireOwnEntity(session, baseline).components.physics_body?.position,
    ).toEqual(DYNAMIC.start);
    expect(
      requireOwnEntity(session, baseline).components.race_progress
        ?.next_checkpoint,
    ).toBe(0);
    await aimCharge(session, false);
    const finished = await awaitSnapshot(
      session,
      (snapshot) =>
        (raceModeState(snapshot.match)?.standings.length ?? 0) === 1,
      'the fully supported trajectory must commit all gates and its normalized finish time',
    );
    const own = requireOwnEntity(session, finished);
    const standing = raceModeState(finished.match)?.standings[0];
    expect(own.components.race_progress?.next_checkpoint).toBe(3);
    expect(own.components.physics_body?.position.x).toBeCloseTo(
      DYNAMIC.finishEntryX,
      6,
    );
    expect(own.components.physics_body?.position.y).toBe(640);
    expect(own.components.physics_body?.velocity).toEqual({ x: 0, y: 0 });
    expect(own.components.physics_body?.acceleration).toEqual({ x: 0, y: 0 });
    expect(standing?.controller_id).toBe(session.controllerId);
    expect(standing?.placement).toBe(1);
    expect(own.components.charge).toBeDefined();
    expect(standing?.finished_tick).toBe(
      own.components.charge?.activation_tick,
    );
    // This published durable fraction is an independently derived oracle. Intermediate
    // gate snapshots are not required: twenty-Hz publication cannot expose every quantum.
    expect(standing?.finished_tick_offset).toBeCloseTo(
      (DYNAMIC.finishEntryX - DYNAMIC.start.x) / DYNAMIC.freeDisplacement,
      6,
    );
    await expect(page.getByRole('table', { name: 'Standings' })).toContainText(
      '#1',
    );
    const later = await awaitSnapshot(
      session,
      (snapshot) => snapshot.tick_sequence >= finished.tick_sequence + 80,
      'the finished body must remain stopped across subsequent committed publications',
    );
    expect(requireOwnEntity(session, later).components.physics_body).toEqual(
      own.components.physics_body,
    );
    expect(raceModeState(later.match)?.standings).toEqual(
      raceModeState(finished.match)?.standings,
    );
    expectSingleCharge(session);
    await blobRoyaleServer.terminateWithSigterm();
    expect(errors).toEqual([]);
  });
});
