import { mkdir } from 'node:fs/promises';
import { dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

import {
  expect,
  test as playwrightTest,
  type BrowserContext,
  type Page,
} from '@playwright/test';

import { BlobRoyaleServerProcess } from './BlobRoyaleServerProcess';
import { BrowserE2EError } from './BrowserE2EError';
import {
  CONNECTED_STATUS,
  PRODUCTION_ORIGIN,
  aimFromPaintedBody,
  connectionStatus,
  findLabel,
  focusSimulationCanvas,
  installCanvasRecorder,
  matchHudCell,
  readCanvasFrame,
  recordSessionTraffic,
  requireCanvasFrame,
  requireLabel,
  requireWorldCanvasFrame,
  waitForReadyServer,
  type RecordedSessionTraffic,
} from './browserFlowSupport';
import type {
  SessionCommand,
  SessionEntitySnapshot,
  SessionSnapshotMessage,
} from '../src/features/simulation/simulationProtocolTypes';

/** Existing two-room authoring, no bots or Start: only these real user inputs can move a body. */
const CURSOR_FIXTURE = Object.freeze({
  configurationFileName: 'blob-royale-browser-e2e-rooms.cfg',
  scenarioFileName: null,
  bodyRadius: 20,
  wideViewport: Object.freeze({ width: 1920, height: 1200 }),
  fractionalViewport: Object.freeze({ width: 1200, height: 900 }),
  near: Object.freeze({ x: 40, y: 0 }),
  far: Object.freeze({ x: 160, y: 0 }),
  diagonal: Object.freeze({ x: 60, y: 80 }),
  crossingTarget: Object.freeze({ x: 8, y: 0 }),
  // A400/drag40 approaches9wu/s: eight units is observable within a bounded short hold.
  motionDistance: 8,
});
/**
 * The retired steering keys that are still inert, which is now a strict subset of the eight Step 11a
 * removed.
 *
 * ADR 0008 reserved that whole pool "for later charge/shield bindings", and plan Step 21 spent two
 * of it on 2026-09-12: `KeyS` activates shield and `KeyD` activates charge. They are therefore no
 * longer inert and asserting that they steer nothing would now be asserting the opposite of the
 * contract. The six that remain still prove what this case exists to prove -- that no directional
 * key steers -- and the two that left are covered by their own bindings in
 * `useThrustInput.test.ts`, so nothing is untested by the move.
 */
const RETIRED_DIRECTION_KEYS = [
  'KeyW',
  'KeyA',
  'ArrowUp',
  'ArrowLeft',
  'ArrowDown',
  'ArrowRight',
] as const;

interface CursorSession {
  readonly page: Page;
  readonly displayName: string;
  readonly traffic: RecordedSessionTraffic;
}
interface CursorFixtures {
  readonly blobRoyaleServer: BlobRoyaleServerProcess;
}
const test = playwrightTest.extend<CursorFixtures>({
  blobRoyaleServer: async ({}, use) => {
    const server =
      await BlobRoyaleServerProcess.createFromEnvironment(CURSOR_FIXTURE);
    try {
      await use(server);
    } finally {
      await server.cleanup();
    }
  },
});

async function openCursorSession(
  context: BrowserContext,
  lobbyId: number,
  errors: string[],
): Promise<CursorSession> {
  const page = await context.newPage();
  page.on('pageerror', (error) => errors.push(error.message));
  const traffic = recordSessionTraffic(page);
  await installCanvasRecorder(page);
  const response = await page.goto(`/?lobby=${lobbyId}`, {
    waitUntil: 'domcontentloaded',
  });
  expect(response?.status()).toBe(200);
  await expect(connectionStatus(page)).toHaveText(CONNECTED_STATUS);
  await expect(matchHudCell(page, 'Phase')).toHaveText('lobby');
  const player = matchHudCell(page, 'Player');
  await expect(player).toHaveText(/^player-[1-9][0-9]*$/);
  const displayName = (await player.textContent())?.trim() ?? '';
  await expect
    .poll(async () => {
      const frame = await readCanvasFrame(page);
      return frame !== null && findLabel(frame, displayName) !== null;
    })
    .toBe(true);
  return { page, displayName, traffic };
}

/** Decodes observed production sends only; this helper never calls a sender. */
function thrustCommands(session: CursorSession) {
  return session.traffic.sentFrames
    .map((frame) => JSON.parse(frame) as SessionCommand)
    .filter((command) => command.kind === 'set_thrust');
}
function lastThrust(session: CursorSession) {
  return thrustCommands(session).at(-1)?.payload ?? null;
}
async function expectThrust(
  session: CursorSession,
  direction: { readonly x: number; readonly y: number },
) {
  await expect.poll(() => lastThrust(session)).toEqual(direction);
}
async function aim(
  session: CursorSession,
  offset: { readonly x: number; readonly y: number },
) {
  return aimFromPaintedBody(
    session.page,
    session.displayName,
    offset,
    CURSOR_FIXTURE.bodyRadius,
  );
}
async function laterPaints(page: Page, count = 3) {
  const before = await requireCanvasFrame(page);
  await expect
    .poll(async () => (await readCanvasFrame(page))?.index ?? -1)
    .toBeGreaterThan(before.index + count);
}
function ownPublishedBodies(session: CursorSession, afterFrame: number) {
  const bodies: NonNullable<
    SessionEntitySnapshot['components']['physics_body']
  >[] = [];
  for (const encoded of session.traffic.receivedFrames.slice(afterFrame)) {
    // Welcome has no entity array. This observer reads physical facts from the same real frames
    // delivered to the production client's schema/sequence boundary, without replacing it.
    const document = JSON.parse(encoded) as SessionSnapshotMessage;
    if (!Array.isArray(document.data.entities)) continue;
    const body = document.data.entities.find(
      (entity) =>
        entity.components.controllable?.display_name === session.displayName,
    )?.components.physics_body;
    if (body !== undefined) bodies.push(body);
  }
  return bodies;
}
async function releaseAndObserveCoast(session: CursorSession) {
  const beforeRelease = session.traffic.receivedFrames.length;
  await session.page.keyboard.up('Space');
  await expectThrust(session, { x: 0, y: 0 });
  await expect(matchHudCell(session.page, 'Thrust')).toHaveText('idle');
  await expect
    .poll(() =>
      ownPublishedBodies(session, beforeRelease).some(
        (body) =>
          body.acceleration.x === 0 &&
          body.acceleration.y === 0 &&
          Math.hypot(body.velocity.x, body.velocity.y) > 0,
      ),
    )
    .toBe(true);
}

test('cursor distance never becomes strength and held Space moves only its room', async ({
  blobRoyaleServer,
  browser,
  request,
}) => {
  test.setTimeout(60_000);
  const contexts: BrowserContext[] = [];
  const errors: string[] = [];
  try {
    await blobRoyaleServer.start();
    await waitForReadyServer(request, blobRoyaleServer);
    const firstContext = await browser.newContext({
      baseURL: PRODUCTION_ORIGIN,
      viewport: CURSOR_FIXTURE.wideViewport,
    });
    contexts.push(firstContext);
    const first = await openCursorSession(firstContext, 1, errors);
    const peerContext = await browser.newContext({
      baseURL: PRODUCTION_ORIGIN,
      viewport: CURSOR_FIXTURE.wideViewport,
    });
    contexts.push(peerContext);
    const peer = await openCursorSession(peerContext, 2, errors);
    const peerBefore = requireLabel(
      await requireWorldCanvasFrame(peer.page),
      peer.displayName,
    );
    await first.page.bringToFront();
    await focusSimulationCanvas(first.page);
    await aim(first, CURSOR_FIXTURE.near);
    for (const code of RETIRED_DIRECTION_KEYS)
      await first.page.keyboard.press(code);
    await laterPaints(first.page);
    expect(first.traffic.sentFrames).toEqual([]);
    await expect(matchHudCell(first.page, 'Thrust')).toHaveText('idle');

    await focusSimulationCanvas(first.page);
    await aim(first, CURSOR_FIXTURE.near);
    const before = requireLabel(
      await requireWorldCanvasFrame(first.page),
      first.displayName,
    );
    await first.page.keyboard.down('Space');
    await expectThrust(first, { x: 1, y: 0 });
    await expect
      .poll(
        async () =>
          requireLabel(
            await requireWorldCanvasFrame(first.page),
            first.displayName,
          ).x,
      )
      .toBeGreaterThan(before.x + CURSOR_FIXTURE.motionDistance);
    const nearCount = thrustCommands(first).length;
    await aim(first, CURSOR_FIXTURE.far);
    await laterPaints(first.page);
    expect(thrustCommands(first)).toHaveLength(nearCount);
    expect(lastThrust(first)).toEqual({ x: 1, y: 0 });
    await aim(first, CURSOR_FIXTURE.diagonal);
    await expect.poll(() => lastThrust(first)?.x).toBeCloseTo(0.6, 8);
    await expect.poll(() => lastThrust(first)?.y).toBeCloseTo(0.8, 8);
    for (const command of thrustCommands(first)) {
      expect(Math.hypot(command.payload.x, command.payload.y)).toBeCloseTo(
        1,
        12,
      );
    }
    await releaseAndObserveCoast(first);
    await laterPaints(peer.page);
    const peerAfter = requireLabel(
      await requireWorldCanvasFrame(peer.page),
      peer.displayName,
    );
    expect(peerAfter.x).toBeCloseTo(peerBefore.x, 8);
    expect(peerAfter.y).toBeCloseTo(peerBefore.y, 8);
    expect(peer.traffic.sentFrames).toEqual([]);
    await expect(matchHudCell(peer.page, 'Thrust')).toHaveText('idle');
    await blobRoyaleServer.terminateWithSigterm();
  } finally {
    for (const context of contexts) await context.close();
  }
  expect(errors).toEqual([]);
});

test('a stationary cursor follows manual-view body geometry and fractional-DPR resize', async ({
  blobRoyaleServer,
  browser,
  request,
}) => {
  test.setTimeout(60_000);
  const errors: string[] = [];
  const context = await browser.newContext({
    baseURL: PRODUCTION_ORIGIN,
    deviceScaleFactor: 1.25,
    viewport: CURSOR_FIXTURE.fractionalViewport,
  });
  try {
    await blobRoyaleServer.start();
    await waitForReadyServer(request, blobRoyaleServer);
    const session = await openCursorSession(context, 1, errors);
    const canvas = session.page.getByRole('img', {
      name: 'Blob Royale simulation world',
    });
    await focusSimulationCanvas(session.page);
    const initial = await requireCanvasFrame(session.page);
    expect(initial.width).toBe(Math.round(initial.cssWidth * 1.25));
    expect(initial.height).toBe(Math.round(initial.cssHeight * 1.25));
    await aim(session, CURSOR_FIXTURE.diagonal);
    await session.page.keyboard.down('Space');
    await expect.poll(() => lastThrust(session)?.x).toBeCloseTo(0.6, 8);
    await expect.poll(() => lastThrust(session)?.y).toBeCloseTo(0.8, 8);
    await releaseAndObserveCoast(session);
    await session.page
      .getByRole('button', { name: 'Manual view', exact: true })
      .click();
    await focusSimulationCanvas(session.page);
    const centerX = await canvas.getAttribute('data-camera-center-x');
    const centerY = await canvas.getAttribute('data-camera-center-y');
    const pointer = await aim(session, CURSOR_FIXTURE.crossingTarget);
    const firstCommand = thrustCommands(session).length;
    await session.page.keyboard.down('Space');
    // One fixed cursor eight CSS pixels away: crossing it must reverse aim without another
    // pointer event from this test. A hook that only recomputes on mousemove drives past forever.
    await expect
      .poll(() =>
        thrustCommands(session)
          .slice(firstCommand)
          .some((command) => command.payload.x < -0.5),
      )
      .toBe(true);
    expect(await canvas.getAttribute('data-camera-center-x')).toBe(centerX);
    expect(await canvas.getAttribute('data-camera-center-y')).toBe(centerY);
    const preResizeCommands = thrustCommands(session).length;
    await session.page.setViewportSize(CURSOR_FIXTURE.wideViewport);
    await expect
      .poll(async () => (await readCanvasFrame(session.page))?.cssWidth ?? 0)
      .toBeGreaterThan(initial.cssWidth);
    // No mouse.move here: the same client point is now above/left of the enlarged viewport's
    // projected body. Resizing must update aim, not multiply client coordinates by DPR.
    await expect
      .poll(() =>
        thrustCommands(session)
          .slice(preResizeCommands)
          .some(
            (command) => command.payload.x < -0.1 && command.payload.y < -0.1,
          ),
      )
      .toBe(true);
    const box = await canvas.boundingBox();
    if (box === null)
      throw new BrowserE2EError(
        'BROWSER_E2E.CURSOR_RESIZE_CANVAS_MISSING',
        'Resized arena is not visible.',
      );
    expect(pointer.x).toBeGreaterThan(box.x);
    expect(pointer.x).toBeLessThan(box.x + box.width);
    expect(pointer.y).toBeGreaterThan(box.y);
    expect(pointer.y).toBeLessThan(box.y + box.height);
    const resized = await requireCanvasFrame(session.page);
    expect(resized.width).toBe(Math.round(resized.cssWidth * 1.25));
    expect(resized.height).toBe(Math.round(resized.cssHeight * 1.25));
    expect(await canvas.getAttribute('data-camera-center-x')).toBe(centerX);
    expect(await canvas.getAttribute('data-camera-center-y')).toBe(centerY);
    await releaseAndObserveCoast(session);
    await blobRoyaleServer.terminateWithSigterm();
  } finally {
    await context.close();
  }
  expect(errors).toEqual([]);
});

test('editing, pointer departure and manual drag cancel go until fresh activation', async ({
  blobRoyaleServer,
  browser,
  request,
}, testInfo) => {
  test.setTimeout(60_000);
  const errors: string[] = [];
  const context = await browser.newContext({
    baseURL: PRODUCTION_ORIGIN,
    viewport: CURSOR_FIXTURE.wideViewport,
  });
  try {
    await blobRoyaleServer.start();
    await waitForReadyServer(request, blobRoyaleServer);
    const session = await openCursorSession(context, 1, errors);
    await session.page.bringToFront();
    await focusSimulationCanvas(session.page);
    await aim(session, CURSOR_FIXTURE.near);
    await session.page.keyboard.down('Space');
    await expectThrust(session, { x: 1, y: 0 });

    // Departing the arena clears go without changing keyboard focus. Reentry and a repeated
    // keydown cannot restore it; the next up/down pair is the new activation.
    await session.page.mouse.move(0, 0);
    await expectThrust(session, { x: 0, y: 0 });
    await aim(session, CURSOR_FIXTURE.near);
    await session.page.keyboard.down('Space');
    await laterPaints(session.page);
    expect(lastThrust(session)).toEqual({ x: 0, y: 0 });
    await session.page.keyboard.up('Space');
    await session.page.keyboard.down('Space');
    await expectThrust(session, { x: 1, y: 0 });

    const panel = session.page.getByRole('region', { name: 'Movement tuning' });
    const acceleration = panel.getByRole('spinbutton', {
      name: 'Acceleration (wu/s²)',
    });
    await acceleration.click();
    await expectThrust(session, { x: 0, y: 0 });
    await acceleration.fill('650');
    await acceleration.press('ArrowUp');
    await expect(acceleration).toHaveValue('651');
    const editingCount = thrustCommands(session).length;
    await acceleration.press('ArrowDown');
    await expect(acceleration).toHaveValue('650');
    await laterPaints(session.page);
    expect(thrustCommands(session)).toHaveLength(editingCount);
    expect(
      session.traffic.sentFrames.every(
        (frame) => (JSON.parse(frame) as SessionCommand).kind === 'set_thrust',
      ),
    ).toBe(true);

    // The tuning sidebar is taller than the arena. Grid-row stretching must not move the lobby
    // overlay below the actual canvas; inspect the card rather than its caption-spanning wrapper.
    const canvasBox = await session.page
      .getByRole('img', { name: 'Blob Royale simulation world' })
      .boundingBox();
    const overlayBox = await session.page
      .locator('.MatchOverlayCard')
      .boundingBox();
    const sidebarBox = await session.page
      .locator('.SimulationSidebar')
      .boundingBox();
    if (canvasBox === null || overlayBox === null || sidebarBox === null) {
      throw new BrowserE2EError(
        'BROWSER_E2E.OVERLAY_LAYOUT_MISSING',
        'The room must render its canvas, lobby overlay card, and tuning sidebar.',
      );
    }
    expect(sidebarBox.height).toBeGreaterThan(canvasBox.height);
    expect(overlayBox.x).toBeGreaterThanOrEqual(canvasBox.x);
    expect(overlayBox.y).toBeGreaterThanOrEqual(canvasBox.y);
    expect(overlayBox.x + overlayBox.width).toBeLessThanOrEqual(
      canvasBox.x + canvasBox.width,
    );
    expect(overlayBox.y + overlayBox.height).toBeLessThanOrEqual(
      canvasBox.y + canvasBox.height,
    );

    const qaImage = fileURLToPath(
      new URL(
        '../../out/test-results/cursor-steering-review/room-tuning.png',
        import.meta.url,
      ),
    );
    await mkdir(dirname(qaImage), { recursive: true });
    await session.page.screenshot({ path: qaImage, fullPage: true });
    await testInfo.attach('room-tuning', {
      path: qaImage,
      contentType: 'image/png',
    });

    await focusSimulationCanvas(session.page);
    await aim(session, CURSOR_FIXTURE.near);
    await session.page.keyboard.down('Space');
    await laterPaints(session.page);
    expect(lastThrust(session)).toEqual({ x: 0, y: 0 });
    await session.page.keyboard.up('Space');
    await session.page.keyboard.down('Space');
    await expectThrust(session, { x: 1, y: 0 });
    await session.page.keyboard.up('Space');
    await expectThrust(session, { x: 0, y: 0 });

    await session.page
      .getByRole('button', { name: 'Manual view', exact: true })
      .click();
    await focusSimulationCanvas(session.page);
    const pointer = await aim(session, CURSOR_FIXTURE.near);
    await session.page.keyboard.down('Space');
    await expect.poll(() => lastThrust(session)?.x ?? 0).toBeGreaterThan(0.9);
    await session.page.mouse.down({ button: 'left' });
    await session.page.mouse.move(pointer.x + 40, pointer.y + 24, { steps: 3 });
    await session.page.mouse.up({ button: 'left' });
    await expectThrust(session, { x: 0, y: 0 });
    await aim(session, CURSOR_FIXTURE.near);
    await session.page.keyboard.down('Space');
    await laterPaints(session.page);
    expect(lastThrust(session)).toEqual({ x: 0, y: 0 });
    await session.page.keyboard.up('Space');
    await session.page.keyboard.down('Space');
    await expect.poll(() => lastThrust(session)?.x ?? 0).toBeGreaterThan(0.9);
    await session.page.keyboard.up('Space');
    await expectThrust(session, { x: 0, y: 0 });
    await blobRoyaleServer.terminateWithSigterm();
  } finally {
    await context.close();
  }
  expect(errors).toEqual([]);
});
