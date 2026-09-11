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
  connectionStatus,
  PRODUCTION_ORIGIN,
  findLabel,
  installCanvasRecorder,
  lobbyStartButton,
  matchHudCell,
  readWorldCanvasFrame as readCanvasFrame,
  requireWorldCanvasFrame as requireCanvasFrame,
  requireLabel,
  startMatchFromLobby,
  waitForReadyServer,
  type RecordedArc,
  type RecordedFrame,
} from './browserFlowSupport';

/**
 * Its own configuration and its own map. See `fixtures/blob-royale-browser-e2e-hill.cfg` and
 * `fixtures/maps/e2e-hill-1920x1280/map.cfg` for why each number is the number it is; the ones
 * this file depends on by name are `lobby_seat_count=2`, `points_to_win=3`,
 * `hill_radius_world_units=150`, the hill at the arena's centre, and the 9 wu/s speed cap.
 */
const HILL_FIXTURE = Object.freeze({
  configurationFileName: 'blob-royale-browser-e2e-hill.cfg',
  scenarioFileName: null,
});

/** The bot the flow seats from the menu: the third registered kind, named by the server's rule. */
const BOT_KIND = 'hill_seeker';
const BOT_DISPLAY_NAME = 'hill_seeker 1';
const POINTS_TO_WIN = 3;

const SESSION_DISPLAY_NAME_PATTERN = /^player-[1-9][0-9]*$/;
const SECONDS_PATTERN = /^[0-9]+\.[0-9] s$/;

/**
 * The exact fill `rendering/hillRenderer.ts` paints the hill with, and the only translucent fill in
 * a hill frame: the mode has no zone, and every body fills with an opaque colour.
 */
const HILL_FILL = 'rgba(251, 191, 36, 0.22)';

/**
 * The recorder removes each frame's observed map-boundary translation and backing-buffer ratio.
 * At one CSS pixel per world unit, the hill and seeker retain their authored geometry even while
 * the local window follows a player far from the hill; these are world-relative painted pixels.
 */
const HILL_CENTER_CANVAS = Object.freeze({ x: 960, y: 640 });
const HILL_RADIUS_CANVAS = 150;
const SEEKER_SPAWN_CANVAS_X = 1155;

/**
 * A body accelerates at 400 wu/s² against `drag_per_second=40`, so it holds 9 wu/s. Sixteen world
 * units of travel is under two seconds of held thrust and is 16 CSS pixels, which no rounding,
 * no rendering order, and no snapshot cadence can manufacture from a body at rest.
 */
const MOTION_THRESHOLD_CANVAS_PIXELS = 16;

const MATCH_START_TIMEOUT_MILLISECONDS = 20_000;
const MOTION_TIMEOUT_MILLISECONDS = 15_000;
/**
 * Forty-five world units of approach at 9 wu/s, then three one-second intervals on the hill: the
 * decision lands about eight seconds after the seeker is seated with the match running. Forty
 * seconds is headroom for a slow emulated toolchain, not a hope.
 */
const DECISION_TIMEOUT_MILLISECONDS = 40_000;

interface HillFixtures {
  readonly blobRoyaleServer: BlobRoyaleServerProcess;
}

const test = playwrightTest.extend<HillFixtures>({
  blobRoyaleServer: async ({}, use) => {
    const server =
      await BlobRoyaleServerProcess.createFromEnvironment(HILL_FIXTURE);
    try {
      await use(server);
    } finally {
      await server.cleanup();
    }
  },
});

function isHillArc(arc: RecordedArc): boolean {
  return arc.fillStyle === HILL_FILL;
}

function requireHillArc(frame: RecordedFrame): RecordedArc {
  const hillArcs = frame.arcs.filter(isHillArc);
  const hillArc = hillArcs[0];
  if (hillArcs.length !== 1 || hillArc === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.HILL_DISC_NOT_DRAWN',
      'A hill frame must draw exactly one translucent hill disc.',
      {
        drawn_fill_styles: frame.arcs.map((arc) => arc.fillStyle).join('|'),
        frame_index: frame.index,
        hill_disc_count: hillArcs.length,
      },
    );
  }
  return hillArc;
}

/** How many hill discs the newest frame drew, or `NaN` before there is a frame. */
async function readHillArcCount(page: Page): Promise<number> {
  const frame = await readCanvasFrame(page);
  return frame === null ? Number.NaN : frame.arcs.filter(isHillArc).length;
}

/**
 * The seeker's drawn x, reported as `NaN` when the frame or the blob is momentarily absent so the
 * poll keeps waiting and its own message survives to the timeout.
 */
async function readLabelCanvasX(page: Page, text: string): Promise<number> {
  const frame = await readCanvasFrame(page);
  const label = frame === null ? null : findLabel(frame, text);
  return label === null ? Number.NaN : label.x;
}

/** The value cell of one participant's line on the scoreboard, addressed by its display name. */
function scoreboardCell(page: Page, displayName: string): Locator {
  return page
    .getByRole('table', { name: 'Scoreboard' })
    .getByRole('row')
    .filter({
      has: page.getByRole('rowheader', { exact: true, name: displayName }),
    })
    .getByRole('cell');
}

async function openSessionPage(
  context: BrowserContext,
  pageErrors: string[],
): Promise<Page> {
  const page = await context.newPage();
  page.on('pageerror', (error) => {
    pageErrors.push(error.message);
  });
  await installCanvasRecorder(page);
  const navigationResponse = await page.goto('/?lobby=1', {
    waitUntil: 'domcontentloaded',
  });
  expect(navigationResponse?.status()).toBe(200);
  await expect(page).toHaveTitle('Blob Royale');
  return page;
}

async function readOwnDisplayName(page: Page): Promise<string> {
  const displayNameCell = matchHudCell(page, 'Player');
  await expect(displayNameCell).toHaveText(SESSION_DISPLAY_NAME_PATTERN);
  return (await displayNameCell.textContent())?.trim() ?? '';
}

test('a browser seats a hill seeker that takes the hill and wins', async ({
  blobRoyaleServer,
  browser,
  request,
}) => {
  // A seating, a countdown, a five-second approach, and three scored intervals do not fit the
  // default per-test budget.
  test.setTimeout(120_000);

  const pageErrors: string[] = [];
  const contexts: BrowserContext[] = [];

  try {
    await blobRoyaleServer.start();
    await waitForReadyServer(request, blobRoyaleServer);

    // ---------------------------------------------------------------- the hill is there in the lobby
    const context = await browser.newContext({ baseURL: PRODUCTION_ORIGIN });
    contexts.push(context);
    const page = await openSessionPage(context, pageErrors);

    await expect(connectionStatus(page)).toHaveText(CONNECTED_STATUS);
    await expect(matchHudCell(page, 'Phase')).toHaveText('lobby');
    await expect(page.locator('.MatchOverlayTitle')).toHaveText(
      'Waiting for players',
    );
    const displayName = await readOwnDisplayName(page);

    // The HUD is the hill's from the first frame: the block arrived with the lobby, the own blob is
    // seated 510 wu clear of the hill, and royale's placement row is nobody's here.
    await expect(matchHudCell(page, 'Score')).toHaveText(
      `0 of ${String(POINTS_TO_WIN)}`,
    );
    await expect(matchHudCell(page, 'Hill')).toHaveText('Off the hill');
    await expect(
      page
        .getByRole('table', { name: 'Match status' })
        .getByRole('rowheader', { exact: true, name: 'Placement' }),
    ).toHaveCount(0);
    await expect(scoreboardCell(page, displayName)).toHaveText('0');

    // `hill_movement` creates the hill on the first tick of every phase, so the lobby draws it at
    // the marker with the configured radius, projected exactly.
    await expect
      .poll(async () => readHillArcCount(page), {
        message: 'the lobby must draw the hill as one translucent disc',
        timeout: MOTION_TIMEOUT_MILLISECONDS,
      })
      .toBe(1);
    const hillArc = requireHillArc(await requireCanvasFrame(page));
    expect(hillArc.x).toBeCloseTo(HILL_CENTER_CANVAS.x, 6);
    expect(hillArc.y).toBeCloseTo(HILL_CENTER_CANVAS.y, 6);
    expect(hillArc.radius).toBeCloseTo(HILL_RADIUS_CANVAS, 6);

    // ---------------------------------------------------------------- a seeker from the menu
    // Two seats, one held, so Start is disabled and says why. The empty seat is filled with the
    // third registered bot through the lobby's own menu, which lists whatever the welcome
    // published: the bot is reachable with no client change.
    await expect(lobbyStartButton(page)).toBeDisabled();
    await expect(
      page.getByText('Waiting for 1 empty seat to be filled.'),
    ).toBeVisible();
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
    await expect(scoreboardCell(page, BOT_DISPLAY_NAME)).toHaveText('0');

    // The seeker reads the hill from the same frames the browser draws and heads for its centre
    // from the moment it has a body: 45 wu to the edge, leftward, at the 9 wu/s cap.
    await expect
      .poll(async () => readLabelCanvasX(page, BOT_DISPLAY_NAME), {
        message: 'the seeker must be drawn moving toward the hill along -x',
        timeout: MOTION_TIMEOUT_MILLISECONDS,
      })
      .toBeLessThan(SEEKER_SPAWN_CANVAS_X - MOTION_THRESHOLD_CANVAS_PIXELS);

    // ---------------------------------------------------------------- the match starts
    await startMatchFromLobby(page);
    await expect(matchHudCell(page, 'Phase')).toHaveText('running', {
      timeout: MATCH_START_TIMEOUT_MILLISECONDS,
    });
    await expect(page.locator('.MatchOverlayCard')).toHaveCount(0);
    // Time left is the limit minus the running ticks elapsed, and exists only while running.
    await expect(matchHudCell(page, 'Time left')).toHaveText(SECONDS_PATTERN);
    await expect(matchHudCell(page, 'Alive')).toHaveText('2');

    // ---------------------------------------------------------------- the seeker scores
    // A point a second once the seeker is holding the hill; the browser's blob is 510 wu clear of
    // it and presses nothing, so nothing contests and its own score stays at zero.
    await expect(scoreboardCell(page, BOT_DISPLAY_NAME)).toHaveText('1', {
      timeout: DECISION_TIMEOUT_MILLISECONDS,
    });
    await expect(matchHudCell(page, 'Score')).toHaveText(
      `0 of ${String(POINTS_TO_WIN)}`,
    );

    // ---------------------------------------------------------------- and takes the match
    await expect(matchHudCell(page, 'Phase')).toHaveText('ended', {
      timeout: DECISION_TIMEOUT_MILLISECONDS,
    });
    await expect(scoreboardCell(page, BOT_DISPLAY_NAME)).toHaveText(
      String(POINTS_TO_WIN),
    );
    await expect(page.locator('.MatchOverlayTitle')).toHaveText('Winner');
    await expect(page.locator('.MatchOverlayDetail')).toHaveText(
      `${BOT_DISPLAY_NAME} held the hill with ${String(POINTS_TO_WIN)} points.`,
    );
    // The winner is on the hill and the hill is still drawn: `ended` freezes it, and the wipe is
    // the lobby's.
    const endedFrame = await requireCanvasFrame(page);
    const seekerLabel = requireLabel(endedFrame, BOT_DISPLAY_NAME);
    const endedHill = requireHillArc(endedFrame);
    expect(
      Math.hypot(seekerLabel.x - endedHill.x, seekerLabel.y - endedHill.y),
    ).toBeLessThan(HILL_RADIUS_CANVAS);

    await blobRoyaleServer.terminateWithSigterm();
  } finally {
    for (const context of contexts) {
      await context.close();
    }
  }

  expect(pageErrors).toEqual([]);
});
