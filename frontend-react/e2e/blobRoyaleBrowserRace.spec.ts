import {
  expect,
  test as playwrightTest,
  type BrowserContext,
  type Locator,
  type Page,
} from '@playwright/test';

import { BlobRoyaleServerProcess } from './BlobRoyaleServerProcess';
import { BrowserE2EError } from './BrowserE2EError';
import {
  CONNECTED_STATUS,
  PRODUCTION_ORIGIN,
  findLabel,
  installCanvasRecorder,
  lobbyStartButton,
  matchHudCell,
  readCanvasFrame,
  requireCanvasFrame,
  requireLabel,
  startMatchFromLobby,
  waitForReadyServer,
  type RecordedFrame,
  type RecordedPath,
} from './browserFlowSupport';

const RACE_FIXTURE = Object.freeze({
  configurationFileName: 'blob-royale-browser-e2e-race.cfg',
  scenarioFileName: null,
});
const BOT_KIND = 'racer';
const BOT_DISPLAY_NAME = 'racer 1';
const COURSE_STROKE = '#dbeafe';
const FINISH_FILL = '#bbf7d0';
const SESSION_DISPLAY_NAME_PATTERN = /^player-[1-9][0-9]*$/;
const SECONDS_PATTERN = /^[0-9]+\.[0-9] s$/;
const RETURN_PATTERN = /^Back on the road in [0-9]+\.[0-9] s$/;
const MOTION_TIMEOUT_MILLISECONDS = 15_000;
const FINISH_TIMEOUT_MILLISECONDS = 60_000;

interface RaceFixtures {
  readonly blobRoyaleServer: BlobRoyaleServerProcess;
}

const test = playwrightTest.extend<RaceFixtures>({
  blobRoyaleServer: async ({}, use) => {
    const server =
      await BlobRoyaleServerProcess.createFromEnvironment(RACE_FIXTURE);
    try {
      await use(server);
    } finally {
      await server.cleanup();
    }
  },
});

/** The road is one declared path, irrespective of the number of racers in the frame. */
function requireCoursePath(frame: RecordedFrame): RecordedPath {
  const paths = frame.paths.filter(
    (path) => path.strokeStyle === COURSE_STROKE,
  );
  const path = paths[0];
  if (paths.length !== 1 || path === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.RACE_CORRIDOR_NOT_DRAWN',
      'A race frame must draw exactly one course corridor.',
      { course_path_count: paths.length, frame_index: frame.index },
    );
  }
  return path;
}

/** Assert actual painted geometry and its order, including the renderer's saved world transform. */
function expectCourse(frame: RecordedFrame): void {
  const path = requireCoursePath(frame);
  expect(path.points).toEqual([
    { kind: 'move', x: 150, y: 320 },
    { kind: 'line', x: 450, y: 320 },
  ]);
  expect(path.lineWidth).toBe(70);
  expect(path.lineCap).toBe('round');
  expect(path.lineJoin).toBe('round');
  expect(path.closed).toBe(false);

  const gates = frame.arcs.filter((arc) => arc.radius === 30);
  expect(gates.map(({ x, y }) => ({ x, y }))).toEqual([
    { x: 200, y: 320 },
    { x: 320, y: 320 },
    { x: 400, y: 320 },
  ]);
  expect(gates.at(-1)?.fillStyle).toBe(FINISH_FILL);
  expect(
    gates.slice(0, -1).every((gate) => gate.fillStyle !== FINISH_FILL),
  ).toBe(true);
  expect(requireLabel(frame, 'Finish')).toMatchObject({ x: 400, y: 320 });
  const bodies = frame.arcs.filter((arc) => arc.radius === 10);
  expect(bodies.length).toBeGreaterThan(0);
  for (const gate of gates) {
    expect(path.drawOrder).toBeLessThan(gate.drawOrder);
    for (const body of bodies) {
      expect(gate.drawOrder).toBeLessThan(body.drawOrder);
    }
  }
}

function standingCell(page: Page, displayName: string): Locator {
  return page
    .getByRole('table', { name: 'Standings' })
    .getByRole('row')
    .filter({
      has: page.getByRole('rowheader', { exact: true, name: displayName }),
    })
    .getByRole('cell');
}

async function readOwnDisplayName(page: Page): Promise<string> {
  const cell = matchHudCell(page, 'Player');
  await expect(cell).toHaveText(SESSION_DISPLAY_NAME_PATTERN);
  return (await cell.textContent())?.trim() ?? '';
}

test('a racer finishes while a browser leaves the road and returns to its checkpoint', async ({
  blobRoyaleServer,
  browser,
  request,
}) => {
  test.setTimeout(120_000);
  const pageErrors: string[] = [];
  let context: BrowserContext | null = null;
  try {
    await blobRoyaleServer.start();
    await waitForReadyServer(request, blobRoyaleServer);
    context = await browser.newContext({ baseURL: PRODUCTION_ORIGIN });
    const page = await context.newPage();
    page.on('pageerror', (error) => pageErrors.push(error.message));
    await installCanvasRecorder(page);
    const response = await page.goto('/?lobby=1', {
      waitUntil: 'domcontentloaded',
    });
    expect(response?.status()).toBe(200);
    await expect(page.getByRole('status')).toHaveText(CONNECTED_STATUS);
    await expect(matchHudCell(page, 'Phase')).toHaveText('lobby');
    const displayName = await readOwnDisplayName(page);

    await expect
      .poll(
        async () => {
          const frame = await readCanvasFrame(page);
          return (
            frame?.paths.filter((path) => path.strokeStyle === COURSE_STROKE)
              .length ?? 0
          );
        },
        { message: 'the lobby must paint its declared corridor exactly once' },
      )
      .toBe(1);
    const lobbyFrame = await requireCanvasFrame(page);
    expectCourse(lobbyFrame);
    expect(requireLabel(lobbyFrame, displayName)).toMatchObject({
      x: 200,
      y: 349,
    });
    await expect(page.getByRole('table', { name: 'Standings' })).toContainText(
      'No finishers yet',
    );
    await expect(lobbyStartButton(page)).toBeDisabled();

    await page
      .getByRole('button', { name: 'Seat 2 Empty' })
      .click({ button: 'right' });
    const menu = page.getByRole('menu', { name: 'Bots for seat 2' });
    await expect(menu).toBeVisible();
    await menu.getByRole('menuitem', { name: BOT_KIND }).click();
    await expect(menu).toHaveCount(0);
    await expect(
      page.locator('.SeatGrid').getByRole('listitem').nth(1),
    ).toContainText(BOT_DISPLAY_NAME);
    await expect(matchHudCell(page, 'Alive')).toHaveText('2');
    await startMatchFromLobby(page);
    await expect(matchHudCell(page, 'Phase')).toHaveText('running');
    await expect(matchHudCell(page, 'Gate')).toHaveText('1 of 3');
    await expect(matchHudCell(page, 'Time left')).toHaveText(SECONDS_PATTERN);

    // Both centers began inside gate 1. Only the human goes down: y=670 -> beyond710 takes about
    // 4.45 s at the accepted 9 wu/s cap. The bot is already leaving to the right, clear of contact.
    await page.keyboard.down('KeyS');
    try {
      await expect(matchHudCell(page, 'Thrust')).toHaveText('0.00, 1.00');
      await expect(matchHudCell(page, 'Return')).toHaveText(RETURN_PATTERN, {
        timeout: MOTION_TIMEOUT_MILLISECONDS,
      });
      await expect
        .poll(
          async () => {
            const frame = await readCanvasFrame(page);
            return frame !== null && findLabel(frame, displayName) === null;
          },
          {
            message:
              'the off-road player must have no body label during its return countdown',
          },
        )
        .toBe(true);
    } finally {
      await page.keyboard.up('KeyS');
    }
    await expect(matchHudCell(page, 'Thrust')).toHaveText('idle');
    await expect(matchHudCell(page, 'Gate')).toHaveText('1 of 3');

    // The name must reappear at the last gate's center, a full 15 canvas pixels above its grid
    // label. This proves checkpoint return rather than merely observing an arbitrary body rejoin.
    await expect
      .poll(
        async () => {
          const frame = await readCanvasFrame(page);
          const label = frame === null ? null : findLabel(frame, displayName);
          return label === null ? null : { x: label.x, y: label.y };
        },
        {
          message:
            'the browser must return at rest to gate 1 rather than its starting grid',
          timeout: MOTION_TIMEOUT_MILLISECONDS,
        },
      )
      .toEqual({ x: 200, y: 334 });
    await expect(matchHudCell(page, 'Return')).toHaveCount(0);
    await expect(matchHudCell(page, 'Alive')).toHaveText('2');
    expectCourse(await requireCanvasFrame(page));

    await expect(standingCell(page, BOT_DISPLAY_NAME)).toHaveText('#1', {
      timeout: FINISH_TIMEOUT_MILLISECONDS,
    });
    await expect(matchHudCell(page, 'Phase')).toHaveText('running');
    await expect(matchHudCell(page, 'Finish window')).toHaveText(
      SECONDS_PATTERN,
    );
    await expect(matchHudCell(page, 'Gate')).toHaveText('1 of 3');
    await expect(matchHudCell(page, 'Phase')).toHaveText('ended');
    await expect(page.locator('.MatchOverlayTitle')).toHaveText('Winner');
    await expect(page.locator('.MatchOverlayDetail')).toHaveText(
      `${BOT_DISPLAY_NAME} finished #1.`,
    );
    await expect(standingCell(page, BOT_DISPLAY_NAME)).toHaveText('#1');
    expectCourse(await requireCanvasFrame(page));
    await blobRoyaleServer.terminateWithSigterm();
  } finally {
    await context?.close();
  }
  expect(pageErrors).toEqual([]);
});
