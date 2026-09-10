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
  installCanvasRecorder,
  matchHudCell,
  readCanvasFrame,
  requireCanvasFrame,
  requireLabel,
  waitForReadyServer,
  type RecordedArc,
  type RecordedFrame,
} from './browserFlowSupport';

/**
 * The existing hill fixture has two spawn markers, no configured bots, and no moving obstacle.
 * Two human sessions leave it in the lobby without pressing Start, so every authoritative body
 * stays at its marker. Camera motion cannot be confused with thrust or a moving objective.
 * @see fixtures/blob-royale-browser-e2e-hill.cfg
 * @see fixtures/maps/e2e-hill-1920x1280/markers.csv
 */
const CAMERA_FIXTURE = Object.freeze({
  configurationFileName: 'blob-royale-browser-e2e-hill.cfg',
  scenarioFileName: null,
  world: Object.freeze({ width: 1920, height: 1280 }),
  firstSpawn: Object.freeze({ x: 300, y: 640 }),
  secondSpawn: Object.freeze({ x: 1155, y: 640 }),
  bodyRadius: 20,
  hill: Object.freeze({ x: 960, y: 640, radius: 150 }),
  hillFill: 'rgba(251, 191, 36, 0.22)',
  wideViewport: Object.freeze({ width: 1920, height: 1080 }),
  narrowViewport: Object.freeze({ width: 800, height: 1080 }),
  panButtonDistance: 96,
  drag: Object.freeze({ x: 80, y: 48 }),
});

interface CameraFixtures {
  readonly blobRoyaleServer: BlobRoyaleServerProcess;
}

interface CameraSession {
  readonly displayName: string;
  readonly page: Page;
  readonly sentFrames: string[];
  readonly webSocketUrls: string[];
}

interface CameraPoint {
  readonly x: number;
  readonly y: number;
}

interface CameraParticipant extends CameraPoint {
  readonly displayName: string;
}

const test = playwrightTest.extend<CameraFixtures>({
  blobRoyaleServer: async ({}, use) => {
    const server =
      await BlobRoyaleServerProcess.createFromEnvironment(CAMERA_FIXTURE);
    try {
      await use(server);
    } finally {
      await server.cleanup();
    }
  },
});

/** Observes real WebSocket sends and real canvas draws without replacing either implementation. */
async function openCameraSession(
  context: BrowserContext,
  pageErrors: string[],
): Promise<CameraSession> {
  const page = await context.newPage();
  const sentFrames: string[] = [];
  const webSocketUrls: string[] = [];
  page.on('pageerror', (error) => {
    pageErrors.push(error.message);
  });
  page.on('websocket', (socket) => {
    webSocketUrls.push(socket.url());
    socket.on('framesent', ({ payload }) => {
      sentFrames.push(
        typeof payload === 'string' ? payload : payload.toString(),
      );
    });
  });
  await installCanvasRecorder(page);
  const response = await page.goto('/?lobby=1', {
    waitUntil: 'domcontentloaded',
  });
  expect(response?.status()).toBe(200);
  await expect(page).toHaveTitle('Blob Royale');
  await expect(page.getByRole('status')).toHaveText(CONNECTED_STATUS);
  await expect(matchHudCell(page, 'Phase')).toHaveText('lobby');
  const displayNameCell = matchHudCell(page, 'Player');
  await expect(displayNameCell).toHaveText(/^player-[1-9][0-9]*$/);
  const displayName = (await displayNameCell.textContent())?.trim() ?? '';
  return { displayName, page, sentFrames, webSocketUrls };
}

/** Matches a body's actual draw to its actual name draw, not to private client state. */
function requireParticipantArc(
  frame: RecordedFrame,
  displayName: string,
): RecordedArc {
  const label = requireLabel(frame, displayName);
  const expectedRadius =
    CAMERA_FIXTURE.bodyRadius * (frame.width / frame.cssWidth);
  const bodies = frame.arcs.filter(
    (arc) =>
      Math.abs(arc.x - label.x) < 0.001 &&
      Math.abs(arc.radius - expectedRadius) < 0.001,
  );
  const body = bodies[0];
  if (bodies.length !== 1 || body === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.CAMERA_PARTICIPANT_NOT_DRAWN',
      'The camera frame must draw exactly one body beneath the participant label.',
      {
        body_count: bodies.length,
        display_name: displayName,
        frame_index: frame.index,
      },
    );
  }
  return body;
}

/**
 * Insists on several complete paints after the action has finished. A multi-step drag can paint
 * more than three times during the action, and the recorder publishes a paint only at the next
 * clear, so a pre-action index alone can still admit the penultimate drag position. The fixture
 * stays in the lobby: these later snapshots also prove manual selection persists.
 */
async function requireLaterFrame(
  page: Page,
  previousFrame: RecordedFrame,
): Promise<RecordedFrame> {
  const afterActionFrame = await requireCanvasFrame(page);
  const completedIndex = Math.max(previousFrame.index, afterActionFrame.index);
  await expect
    .poll(async () => (await readCanvasFrame(page))?.index ?? -1, {
      message: 'camera assertions require several complete post-action paints',
    })
    .toBeGreaterThan(completedIndex + 3);
  return requireCanvasFrame(page);
}

/**
 * Checks the common projection at the canvas drawing boundary, including both body/label pairs,
 * the hill, and the world edge. DPR affects only backing pixels; fixture world coordinates remain
 * unchanged. Throws a diagnostic error if the renderer omits its world-boundary draw.
 */
function expectWorldView(
  frame: RecordedFrame,
  center: CameraPoint,
  participants: readonly CameraParticipant[],
): void {
  expect(frame.cssWidth).toBeGreaterThan(0);
  expect(frame.cssHeight).toBeGreaterThan(0);
  expect(frame.cssWidth).toBeLessThanOrEqual(960);
  expect(frame.cssHeight).toBeLessThanOrEqual(640);
  expect(Math.abs(frame.cssHeight - (frame.cssWidth * 2) / 3)).toBeLessThan(1);
  const pixelRatioX = frame.width / frame.cssWidth;
  const pixelRatioY = frame.height / frame.cssHeight;
  const boundary = frame.worldBoundary;
  if (boundary === null) {
    throw new BrowserE2EError(
      'BROWSER_E2E.CAMERA_WORLD_BOUNDARY_NOT_DRAWN',
      'A camera frame must draw its projected world boundary.',
      { frame_index: frame.index },
    );
  }
  expect(boundary.width).toBeCloseTo(
    CAMERA_FIXTURE.world.width * pixelRatioX,
    6,
  );
  expect(boundary.height).toBeCloseTo(
    CAMERA_FIXTURE.world.height * pixelRatioY,
    6,
  );
  expect(boundary.x).toBeCloseTo(
    (frame.cssWidth / 2 - center.x) * pixelRatioX,
    6,
  );
  expect(boundary.y).toBeCloseTo(
    (frame.cssHeight / 2 - center.y) * pixelRatioY,
    6,
  );
  expect(CAMERA_FIXTURE.world.width).toBeGreaterThan(frame.cssWidth);
  expect(CAMERA_FIXTURE.world.height).toBeGreaterThan(frame.cssHeight);

  for (const participant of participants) {
    const body = requireParticipantArc(frame, participant.displayName);
    expect(body.x).toBeCloseTo(
      (participant.x - center.x + frame.cssWidth / 2) * pixelRatioX,
      6,
    );
    expect(body.y).toBeCloseTo(
      (participant.y - center.y + frame.cssHeight / 2) * pixelRatioY,
      6,
    );
    expect(body.radius / pixelRatioX).toBeCloseTo(CAMERA_FIXTURE.bodyRadius, 6);
    const label = requireLabel(frame, participant.displayName);
    expect(label.y - body.y).toBeCloseTo(
      (CAMERA_FIXTURE.bodyRadius + 4) * pixelRatioY,
      6,
    );
  }

  const hills = frame.arcs.filter(
    (arc) => arc.fillStyle === CAMERA_FIXTURE.hillFill,
  );
  expect(hills).toHaveLength(1);
  expect(hills[0]?.x).toBeCloseTo(
    (CAMERA_FIXTURE.hill.x - center.x + frame.cssWidth / 2) * pixelRatioX,
    6,
  );
  expect(hills[0]?.y).toBeCloseTo(
    (CAMERA_FIXTURE.hill.y - center.y + frame.cssHeight / 2) * pixelRatioY,
    6,
  );
  expect(hills[0]?.radius).toBeCloseTo(
    CAMERA_FIXTURE.hill.radius * pixelRatioX,
    6,
  );
}

test('independent cameras follow and pan a large world without gameplay commands', async ({
  blobRoyaleServer,
  browser,
  request,
}) => {
  test.setTimeout(90_000);
  const contexts: BrowserContext[] = [];
  const pageErrors: string[] = [];
  try {
    await blobRoyaleServer.start();
    await waitForReadyServer(request, blobRoyaleServer);
    const firstContext = await browser.newContext({
      baseURL: PRODUCTION_ORIGIN,
      deviceScaleFactor: 1,
      viewport: CAMERA_FIXTURE.wideViewport,
    });
    contexts.push(firstContext);
    const first = await openCameraSession(firstContext, pageErrors);
    const secondContext = await browser.newContext({
      baseURL: PRODUCTION_ORIGIN,
      deviceScaleFactor: 2,
      viewport: CAMERA_FIXTURE.wideViewport,
    });
    contexts.push(secondContext);
    const second = await openCameraSession(secondContext, pageErrors);
    expect(second.displayName).not.toBe(first.displayName);
    const participants = [
      { displayName: first.displayName, ...CAMERA_FIXTURE.firstSpawn },
      { displayName: second.displayName, ...CAMERA_FIXTURE.secondSpawn },
    ];
    for (const session of [first, second]) {
      await expect
        .poll(async () => (await readCanvasFrame(session.page))?.labels.length)
        .toBe(2);
      await expect(
        session.page.getByRole('button', {
          exact: true,
          name: 'Follow player',
        }),
      ).toHaveAttribute('aria-pressed', 'true');
      await expect(
        session.page.getByRole('button', { exact: true, name: 'Manual view' }),
      ).toHaveAttribute('aria-pressed', 'false');
    }

    let firstFrame = await requireCanvasFrame(first.page);
    const secondFrame = await requireCanvasFrame(second.page);
    expectWorldView(firstFrame, CAMERA_FIXTURE.firstSpawn, participants);
    expectWorldView(secondFrame, CAMERA_FIXTURE.secondSpawn, participants);
    expect(firstFrame.cssWidth).toBe(960);
    expect(firstFrame.cssHeight).toBe(640);
    expect(secondFrame.cssWidth).toBe(firstFrame.cssWidth);
    expect(secondFrame.cssHeight).toBe(firstFrame.cssHeight);
    expect(firstFrame.width).toBe(960);
    expect(firstFrame.height).toBe(640);
    expect(secondFrame.width).toBe(1920);
    expect(secondFrame.height).toBe(1280);
    // The first player is only 300 wu from the edge. Strict follow keeps its body centred and
    // draws the real left edge 180 CSS pixels inside the viewport, exposing outside-map space.
    expect(firstFrame.worldBoundary?.x).toBeCloseTo(180, 6);
    expect(requireParticipantArc(firstFrame, first.displayName).x).toBe(480);
    expect(requireParticipantArc(secondFrame, second.displayName).x).toBe(960);
    expect(secondFrame.worldBoundary?.x).toBeLessThan(0);

    // Keyboard activation of ordinary camera buttons must not become keyboard steering.
    const manual = first.page.getByRole('button', {
      exact: true,
      name: 'Manual view',
    });
    await manual.focus();
    await manual.press('Enter');
    await expect(manual).toHaveAttribute('aria-pressed', 'true');
    firstFrame = await requireLaterFrame(first.page, firstFrame);
    expectWorldView(firstFrame, CAMERA_FIXTURE.firstSpawn, participants);

    const panRight = first.page.getByRole('button', {
      exact: true,
      name: 'Pan right',
    });
    await panRight.focus();
    await panRight.press('Space');
    const pannedCenter = {
      x: CAMERA_FIXTURE.firstSpawn.x + CAMERA_FIXTURE.panButtonDistance,
      y: CAMERA_FIXTURE.firstSpawn.y,
    };
    firstFrame = await requireLaterFrame(first.page, firstFrame);
    expectWorldView(firstFrame, pannedCenter, participants);
    expectWorldView(
      await requireLaterFrame(second.page, secondFrame),
      CAMERA_FIXTURE.secondSpawn,
      participants,
    );

    const canvas = first.page.getByRole('img', {
      name: 'Blob Royale simulation world',
    });
    await canvas.scrollIntoViewIfNeeded();
    const canvasBox = await canvas.boundingBox();
    if (canvasBox === null) {
      throw new BrowserE2EError(
        'BROWSER_E2E.CAMERA_CANVAS_NOT_VISIBLE',
        'The manual camera must have a visible canvas to drag.',
      );
    }
    const dragStart = { x: canvasBox.x + 120, y: canvasBox.y + 100 };
    const dragEnd = {
      x: dragStart.x + CAMERA_FIXTURE.drag.x,
      y: dragStart.y + CAMERA_FIXTURE.drag.y,
    };
    await first.page.mouse.move(dragStart.x, dragStart.y);
    await first.page.mouse.down();
    await first.page.mouse.move(dragEnd.x, dragEnd.y, { steps: 4 });
    await first.page.mouse.up();
    const draggedCenter = {
      x: pannedCenter.x - CAMERA_FIXTURE.drag.x,
      y: pannedCenter.y - CAMERA_FIXTURE.drag.y,
    };
    firstFrame = await requireLaterFrame(first.page, firstFrame);
    expectWorldView(firstFrame, draggedCenter, participants);
    // A released pointer no longer owns a drag, even while the pointer remains over the canvas.
    await first.page.mouse.move(dragStart.x, dragStart.y);
    firstFrame = await requireLaterFrame(first.page, firstFrame);
    expectWorldView(firstFrame, draggedCenter, participants);

    await first.page.setViewportSize(CAMERA_FIXTURE.narrowViewport);
    firstFrame = await requireLaterFrame(first.page, firstFrame);
    expect(firstFrame.cssWidth).toBeLessThan(secondFrame.cssWidth);
    expect(firstFrame.cssHeight).toBeLessThan(secondFrame.cssHeight);
    expectWorldView(firstFrame, draggedCenter, participants);
    expect(requireParticipantArc(firstFrame, first.displayName).radius).toBe(
      20,
    );

    const follow = first.page.getByRole('button', {
      exact: true,
      name: 'Follow player',
    });
    await follow.click();
    await expect(follow).toHaveAttribute('aria-pressed', 'true');
    await expect(manual).toHaveAttribute('aria-pressed', 'false');
    firstFrame = await requireLaterFrame(first.page, firstFrame);
    expectWorldView(firstFrame, CAMERA_FIXTURE.firstSpawn, participants);
    expect(requireParticipantArc(firstFrame, first.displayName).x).toBeCloseTo(
      firstFrame.width / 2,
      6,
    );
    expect(requireParticipantArc(firstFrame, first.displayName).y).toBeCloseTo(
      firstFrame.height / 2,
      6,
    );
    expectWorldView(
      await requireLaterFrame(second.page, secondFrame),
      CAMERA_FIXTURE.secondSpawn,
      participants,
    );
    for (const session of [first, second]) {
      await expect(matchHudCell(session.page, 'Phase')).toHaveText('lobby');
      expect(session.webSocketUrls).toHaveLength(1);
      expect(session.sentFrames).toEqual([]);
    }
    await blobRoyaleServer.terminateWithSigterm();
  } finally {
    for (const context of contexts) {
      await context.close();
    }
  }
  expect(pageErrors).toEqual([]);
});
