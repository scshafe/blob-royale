import {
  expect,
  test as playwrightTest,
  type Page,
  type BrowserContext,
} from '@playwright/test';

import { BlobRoyaleServerProcess } from './BlobRoyaleServerProcess';
import { BrowserE2EError } from './BrowserE2EError';
import {
  CONNECTED_STATUS,
  PRODUCTION_ORIGIN,
  delay,
  findLabel,
  installCanvasRecorder,
  matchHudCell,
  readWorldCanvasFrame as readCanvasFrame,
  requireWorldCanvasFrame as requireCanvasFrame,
  requireLabel,
  waitForReadyServer,
  type RecordedArc,
  type RecordedFrame,
  type RecordedLabel,
  lobbyStartButton,
  seatCountControl,
  startMatchFromLobby,
} from './browserFlowSupport';

/**
 * Its own configuration and its own map. See `fixtures/blob-royale-browser-e2e-royale.cfg` and
 * `fixtures/maps/e2e-royale-1920x1280/map.cfg` for why each number is the number it is; the two
 * that this file depends on by name are `lobby_seat_count=4` and the 9 wu/s speed cap.
 */
const ROYALE_FIXTURE = Object.freeze({
  configurationFileName: 'blob-royale-browser-e2e-royale.cfg',
  scenarioFileName: null,
});

/** The roster: one bot named from `[match] bots`, and one blob per browser session. */
const BOT_DISPLAY_NAME = 'wanderer 1';

const SESSION_DISPLAY_NAME_PATTERN = /^player-[1-9][0-9]*$/;

/**
 * Three blobs and the zone entity. The zone owns neither a body nor a controller, so it is an
 * entity the caption counts and not a player.
 */
const COMPLETE_TICK_PATTERN =
  /^Complete tick ([1-9][0-9]*) with 4 entities and 3 players\.$/;

/**
 * World-relative painted pixels remove each camera's observed boundary translation and DPR.
 * The helper also asserts the true 1920×1280 boundary at scale one, so it cannot hide a fit-all view.
 */
const PROJECTION_SCALE = 1;
const ARENA_CENTER_CANVAS = Object.freeze({ x: 960, y: 640 });
const ZONE_FULL_RADIUS_CANVAS = Math.hypot(960, 640) * PROJECTION_SCALE;
const ZONE_MINIMUM_RADIUS_CANVAS = 200 * PROJECTION_SCALE;

/**
 * A body accelerates at 400 wu/s² against `drag_per_second=40`, so it holds 9 wu/s. Sixteen world
 * units of travel is under two seconds of held thrust and is 16 CSS pixels, which no rounding,
 * no rendering order, and no snapshot cadence can manufacture from a body at rest.
 */
const MOTION_THRESHOLD_CANVAS_PIXELS = 16;
const SETTLED_TOLERANCE_CANVAS_PIXELS = 0.5;

/**
 * The zone contracts by `(1153.78 - 200) / 90 = 10.6` wu/s, which is 10.6 CSS pixels a second, so
 * eight pixels is under a second of running and is far above the 0.8 pixel step between two
 * consecutive published frames.
 */
const ZONE_CONTRACTION_CANVAS_PIXELS = 8;

/**
 * The wanderer is an independent command source, so "one thrust moved one blob and no other" is
 * only true while the bot cannot reach the blob that must not move. The map's minimum pairwise
 * spawn separation is 1000 wu and contact is 2r = 40 wu, so the closing distance is 960 wu; at the
 * 9 wu/s cap that is 106 s of continuous, perfectly aimed travel. This flow refuses to make the
 * claim past 90 s and says which invariant it lost rather than reporting a mystery.
 */
const BOT_ISOLATION_BUDGET_MILLISECONDS = 90_000;

const MATCH_START_TIMEOUT_MILLISECONDS = 20_000;
const MOTION_TIMEOUT_MILLISECONDS = 15_000;
const SETTLE_TIMEOUT_MILLISECONDS = 10_000;

interface RoyaleFixtures {
  readonly blobRoyaleServer: BlobRoyaleServerProcess;
}

const test = playwrightTest.extend<RoyaleFixtures>({
  blobRoyaleServer: async ({}, use) => {
    const server =
      await BlobRoyaleServerProcess.createFromEnvironment(ROYALE_FIXTURE);
    try {
      await use(server);
    } finally {
      await server.cleanup();
    }
  },
});

/** The safe zone is the one translucent circle; every body fills with an opaque colour. */
function isZoneArc(arc: RecordedArc): boolean {
  return arc.fillStyle.startsWith('rgba(');
}

function requireZoneArc(frame: RecordedFrame): RecordedArc {
  const zoneArcs = frame.arcs.filter(isZoneArc);
  const zoneArc = zoneArcs[0];
  if (zoneArcs.length !== 1 || zoneArc === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.ZONE_CIRCLE_NOT_DRAWN',
      'A royale frame must draw exactly one translucent safe-zone circle.',
      {
        drawn_fill_styles: frame.arcs.map((arc) => arc.fillStyle).join('|'),
        frame_index: frame.index,
        translucent_circle_count: zoneArcs.length,
      },
    );
  }
  return zoneArc;
}

function bodyArcs(frame: RecordedFrame): readonly RecordedArc[] {
  return frame.arcs.filter((arc) => !isZoneArc(arc));
}

function labelTexts(frame: RecordedFrame): readonly string[] {
  return frame.labels.map((label) => label.text).sort();
}

/**
 * What one frame drew, as one comparable value: safe-zone circles, body discs, and the display
 * names labelling them. Polled rather than sampled because the newest whole frame is at most one
 * presentation frame behind the caption that announced the roster.
 */
async function readFrameSignature(page: Page): Promise<string> {
  const frame = await readCanvasFrame(page);
  if (frame === null) {
    return 'no-frame';
  }
  return `zones=${frame.arcs.filter(isZoneArc).length} bodies=${bodyArcs(frame).length} labels=${labelTexts(frame).join(',')}`;
}

function expectedFrameSignature(
  sessionDisplayNames: readonly string[],
): string {
  const names = [BOT_DISPLAY_NAME, ...sessionDisplayNames].sort();
  return `zones=1 bodies=3 labels=${names.join(',')}`;
}

function separation(left: RecordedLabel, right: RecordedLabel): number {
  return Math.hypot(left.x - right.x, left.y - right.y);
}

async function readLabel(page: Page, text: string): Promise<RecordedLabel> {
  return requireLabel(await requireCanvasFrame(page), text);
}

/** Inverting two independent translated paints can differ by binary64 roundoff, not by motion. */
function expectSameWorldLabel(
  actual: RecordedLabel,
  expected: RecordedLabel,
): void {
  expect(actual.text).toBe(expected.text);
  expect(actual.drawOrder).toBe(expected.drawOrder);
  expect(actual.x).toBeCloseTo(expected.x, 9);
  expect(actual.y).toBeCloseTo(expected.y, 9);
}

/**
 * The two poll subjects, reported as `NaN` when the frame or the blob is momentarily absent. A
 * poll that threw there would report "no frame yet" as the failure it was waiting to disprove;
 * `NaN` fails every comparison, so the poll keeps waiting and its own message survives to the
 * timeout.
 */
async function readLabelCanvasX(page: Page, text: string): Promise<number> {
  const frame = await readCanvasFrame(page);
  const label = frame === null ? null : findLabel(frame, text);
  return label === null ? Number.NaN : label.x;
}

async function readZoneCanvasRadius(page: Page): Promise<number> {
  const frame = await readCanvasFrame(page);
  if (frame === null) {
    return Number.NaN;
  }
  const zoneArcs = frame.arcs.filter(isZoneArc);
  return zoneArcs.length === 1 ? requireZoneArc(frame).radius : Number.NaN;
}

/**
 * Waits until one blob has been drawn twice at exactly the same place on two different frames.
 *
 * After a thrust is released the stored acceleration is zero and the remaining velocity is scaled
 * by `1 - drag * dt = 0.9` every 2.5 ms tick, so within about 250 ticks the per-tick displacement
 * falls below the last bit of a coordinate near a thousand and the drawn value stops changing
 * outright. That termination is arithmetic, so this polls for the observable fact rather than
 * sleeping for a duration nobody can defend.
 */
async function waitForSettledBlob(
  page: Page,
  displayName: string,
): Promise<RecordedLabel> {
  const deadline = Date.now() + SETTLE_TIMEOUT_MILLISECONDS;
  let previousFrameIndex = -1;
  let previousLabel: RecordedLabel | null = null;

  while (Date.now() < deadline) {
    const frame = await readCanvasFrame(page);
    const label = frame === null ? null : findLabel(frame, displayName);
    if (
      frame !== null &&
      label !== null &&
      frame.index !== previousFrameIndex
    ) {
      if (
        previousLabel !== null &&
        previousLabel.x === label.x &&
        previousLabel.y === label.y
      ) {
        return label;
      }
      previousFrameIndex = frame.index;
      previousLabel = label;
    }
    await delay(50);
  }

  throw new BrowserE2EError(
    'BROWSER_E2E.BLOB_DID_NOT_COME_TO_REST',
    'A blob with no thrust never repeated one drawn position on two frames.',
    {
      display_name: displayName,
      last_drawn_x: previousLabel?.x ?? null,
      last_drawn_y: previousLabel?.y ?? null,
      timeout_milliseconds: SETTLE_TIMEOUT_MILLISECONDS,
    },
  );
}

function requireBotIsolationBudget(serverStartedAtMilliseconds: number): void {
  const elapsedMilliseconds = Date.now() - serverStartedAtMilliseconds;
  if (elapsedMilliseconds <= BOT_ISOLATION_BUDGET_MILLISECONDS) {
    return;
  }
  throw new BrowserE2EError(
    'BROWSER_E2E.BOT_ISOLATION_BUDGET_EXCEEDED',
    'The bot has had enough time to reach another spawn point, so a stationary blob is no longer a property this fixture guarantees.',
    {
      budget_milliseconds: BOT_ISOLATION_BUDGET_MILLISECONDS,
      elapsed_milliseconds: elapsedMilliseconds,
    },
  );
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
  // Room 1 by its URL: the directory in front of it is Step 15's to drive through the UI.
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

test('two browsers and a bot play one royale match', async ({
  blobRoyaleServer,
  browser,
  request,
}) => {
  // Two contexts, a countdown, a held thrust, a coast to rest, a second held thrust, and a
  // shrinking zone do not fit the default per-test budget.
  test.setTimeout(150_000);

  const pageErrors: string[] = [];
  const contexts: BrowserContext[] = [];

  try {
    await blobRoyaleServer.start();
    const serverStartedAtMilliseconds = Date.now();
    await waitForReadyServer(request, blobRoyaleServer);

    // ---------------------------------------------------------------- waiting for a match
    // Since protocol 2.3 a match starts only when every seat in the lobby is filled and somebody
    // presses Start, and nothing in this flow has done either yet. Everything this block asserts
    // therefore holds until the flow itself fills the lobby below: it is an invariant of the
    // fixture, not a window.
    const contextA = await browser.newContext({ baseURL: PRODUCTION_ORIGIN });
    contexts.push(contextA);
    const pageA = await openSessionPage(contextA, pageErrors);

    await expect(pageA.getByRole('status')).toHaveText(CONNECTED_STATUS);
    await expect(matchHudCell(pageA, 'Phase')).toHaveText('lobby');
    await expect(pageA.locator('.MatchOverlayTitle')).toHaveText(
      'Waiting for players',
    );
    await expect(pageA.locator('.MatchOverlayDetail')).toHaveText(
      'The match starts when every seat is filled and somebody presses Start.',
    );
    await expect(matchHudCell(pageA, 'Alive')).toHaveText('2');
    const displayNameA = await readOwnDisplayName(pageA);
    // Four seats, two of them held -- the bot's and this browser's -- so Start is disabled and says
    // why. This is the rule most likely to regress silently, so it is asserted before it changes.
    await expect(lobbyStartButton(pageA)).toBeDisabled();
    await expect(
      pageA.getByText('Waiting for 2 empty seats to be filled.'),
    ).toBeVisible();

    // ---------------------------------------------------------------- the match starts
    const contextB = await browser.newContext({ baseURL: PRODUCTION_ORIGIN });
    contexts.push(contextB);
    const pageB = await openSessionPage(contextB, pageErrors);

    // Neither browser is ever a mid-match joiner, so neither is ever deferred: a deferred session
    // would report the awaiting-match label here instead of a live connection.
    await expect(pageB.getByRole('status')).toHaveText(CONNECTED_STATUS);
    const displayNameB = await readOwnDisplayName(pageB);
    expect(displayNameB).not.toBe(displayNameA);

    // **The lobby is operated through the client.** Three of the four seats are held -- the bot
    // and the two browsers -- so Start is still disabled. Browser A shrinks the lobby to the three
    // seats that are taken, which is exactly where the control floors it, and presses Start once
    // the server has published the smaller roster. Nothing else ever enters the field, so the
    // counts below describe the bot and the two browsers.
    await expect(
      pageA.getByText('Waiting for 1 empty seat to be filled.'),
    ).toBeVisible();
    await seatCountControl(pageA).fill('3');
    await startMatchFromLobby(pageA);

    await expect(matchHudCell(pageA, 'Phase')).toHaveText('running', {
      timeout: MATCH_START_TIMEOUT_MILLISECONDS,
    });
    await expect(matchHudCell(pageB, 'Phase')).toHaveText('running', {
      timeout: MATCH_START_TIMEOUT_MILLISECONDS,
    });
    await expect(matchHudCell(pageA, 'Alive')).toHaveText('3');
    await expect(matchHudCell(pageA, 'Placement')).toHaveText('In play');
    // The lobby and countdown overlays are gone once a running player owns a body.
    await expect(pageA.locator('.MatchOverlayCard')).toHaveCount(0);

    // ---------------------------------------------------------------- all three blobs render
    for (const page of [pageA, pageB]) {
      await expect(page.locator('.SimulationCanvas figcaption')).toHaveText(
        COMPLETE_TICK_PATTERN,
      );
    }
    const expectedSignature = expectedFrameSignature([
      displayNameA,
      displayNameB,
    ]);
    for (const page of [pageA, pageB]) {
      await expect
        .poll(async () => readFrameSignature(page), {
          message:
            'both browsers must draw all three blobs, each labelled with its display name, inside the safe zone',
          timeout: MOTION_TIMEOUT_MILLISECONDS,
        })
        .toBe(expectedSignature);
    }
    const openingFrame = await requireCanvasFrame(pageA);

    // ---------------------------------------------------------------- the zone shrinks
    const openingZone = requireZoneArc(openingFrame);
    expect(openingZone.x).toBeCloseTo(ARENA_CENTER_CANVAS.x, 6);
    expect(openingZone.y).toBeCloseTo(ARENA_CENTER_CANVAS.y, 6);
    expect(openingZone.radius).toBeGreaterThan(ZONE_MINIMUM_RADIUS_CANVAS);
    expect(openingZone.radius).toBeLessThanOrEqual(ZONE_FULL_RADIUS_CANVAS);
    await expect
      .poll(async () => readZoneCanvasRadius(pageA), {
        message: 'the safe zone must contract while the match is running',
        timeout: MOTION_TIMEOUT_MILLISECONDS,
      })
      .toBeLessThan(openingZone.radius - ZONE_CONTRACTION_CANVAS_PIXELS);

    // ---------------------------------------------------------------- one thrust, one blob
    const beforeThrustOwnA = await readLabel(pageA, displayNameA);
    const beforeThrustPeerB = await readLabel(pageA, displayNameB);
    expectSameWorldLabel(
      await readLabel(pageB, displayNameB),
      beforeThrustPeerB,
    );

    await pageA.bringToFront();
    await pageA.keyboard.down('KeyD');
    await expect(matchHudCell(pageA, 'Thrust')).toHaveText('1.00, 0.00');
    await expect(matchHudCell(pageB, 'Thrust')).toHaveText('idle');
    await expect
      .poll(async () => readLabelCanvasX(pageA, displayNameA), {
        message: "the thrusting session's own blob must move along +x",
        timeout: MOTION_TIMEOUT_MILLISECONDS,
      })
      .toBeGreaterThan(beforeThrustOwnA.x + MOTION_THRESHOLD_CANVAS_PIXELS);
    await pageA.keyboard.up('KeyD');
    await expect(matchHudCell(pageA, 'Thrust')).toHaveText('idle');

    // The other session pressed nothing, its blob was seated at rest, and nothing has touched it:
    // its world position is identical apart from inverse-projection roundoff. Both clients are asked, because a
    // blob that moved for one viewer and not the other would be a rendering bug wearing this
    // assertion's clothes.
    requireBotIsolationBudget(serverStartedAtMilliseconds);
    expectSameWorldLabel(
      await readLabel(pageA, displayNameB),
      beforeThrustPeerB,
    );
    expectSameWorldLabel(
      await readLabel(pageB, displayNameB),
      beforeThrustPeerB,
    );

    // ---------------------------------------------------------------- and it is not one blob
    // Thrusting the other way from the other browser: whichever entity the first thrust reached,
    // it was this session's own, not a fixed one.
    const restingA = await waitForSettledBlob(pageA, displayNameA);
    const beforeThrustOwnB = await readLabel(pageB, displayNameB);

    await pageB.bringToFront();
    await pageB.keyboard.down('KeyA');
    await expect(matchHudCell(pageB, 'Thrust')).toHaveText('-1.00, 0.00');
    await expect(matchHudCell(pageA, 'Thrust')).toHaveText('idle');
    await expect
      .poll(async () => readLabelCanvasX(pageB, displayNameB), {
        message: "the second session's own blob must move along -x",
        timeout: MOTION_TIMEOUT_MILLISECONDS,
      })
      .toBeLessThan(beforeThrustOwnB.x - MOTION_THRESHOLD_CANVAS_PIXELS);
    await pageB.keyboard.up('KeyA');

    // A released blob decays geometrically rather than stopping, so this one is bounded instead of
    // exact: it was drawn at rest and must still be within half a pixel of there.
    requireBotIsolationBudget(serverStartedAtMilliseconds);
    expect(
      separation(await readLabel(pageB, displayNameA), restingA),
    ).toBeLessThan(SETTLED_TOLERANCE_CANVAS_PIXELS);

    // ---------------------------------------------------------------- still one match, still three
    expect(await readFrameSignature(pageB)).toBe(expectedSignature);
    await expect(matchHudCell(pageB, 'Phase')).toHaveText('running');

    await blobRoyaleServer.terminateWithSigterm();
  } finally {
    for (const context of contexts) {
      await context.close();
    }
  }

  expect(pageErrors).toEqual([]);
});
