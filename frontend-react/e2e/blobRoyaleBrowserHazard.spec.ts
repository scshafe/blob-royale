import {
  expect,
  test,
  type APIRequestContext,
  type Browser,
  type BrowserContext,
  type Page,
} from '@playwright/test';

import { BlobRoyaleLobbyDriver } from './BlobRoyaleLobbyDriver';
import {
  BlobRoyaleServerProcess,
  type BlobRoyaleServerFixture,
} from './BlobRoyaleServerProcess';
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
  type RecordedLabel,
} from './browserFlowSupport';

/**
 * Two configurations over one map, differing in exactly one key.
 *
 * `[hazard.crimson_comet] lethal_on_contact` is the only line that is not identical between the two
 * files, which is the observable form of the claim `shared/hazard_archetype.hpp` makes: a comet and
 * a boulder differ by one configuration key and one component. Anything these two flows disagree
 * about is therefore attributable to that key and to nothing else.
 *
 * They are a third and fourth fixture rather than a hazard table added to
 * `blob-royale-browser-e2e-royale.cfg`, because that flow pins `4 entities and 3 players` in its
 * caption assertion and a hazard is an entity: adding one there would make the count fluctuate and
 * that assertion would be asserting the weather. Neither existing flow is touched by this file.
 */
/**
 * The kind the lobby driver would declare into any seat still empty when it presses Start. Both
 * fixtures declare exactly the two seats the two browsers take on admission and `[match] bots` is
 * empty, so the driver is admitted into a full lobby with no bot to displace: it holds no seat,
 * declares nothing, and only presses Start. No bot session is ever created. The driver's own blob
 * sits at the map's third marker for the countdown and is gone within a frame of `running`, so the
 * two browsers are the only bodies in the arena by the time a hazard can exist, which is what keeps
 * the drawn crossing a two-body geometry.
 */
const SEATED_NPC_KIND = 'wanderer';

const LETHAL_FIXTURE: BlobRoyaleServerFixture = Object.freeze({
  configurationFileName: 'blob-royale-browser-e2e-hazard-lethal.cfg',
  scenarioFileName: null,
});
const HEAVY_FIXTURE: BlobRoyaleServerFixture = Object.freeze({
  configurationFileName: 'blob-royale-browser-e2e-hazard-heavy.cfg',
  scenarioFileName: null,
});

const SESSION_DISPLAY_NAME_PATTERN = /^player-[1-9][0-9]*$/;

/**
 * `[world] 1920x1280` against the client's `960x640` canvas maximum is an exact 0.5 projection, so
 * every recorded canvas coordinate is exactly half its world coordinate.
 */
const PROJECTION_SCALE = 0.5;
const PLAYER_RADIUS_CANVAS = 20 * PROJECTION_SCALE;
const HAZARD_RADIUS_CANVAS = 30 * PROJECTION_SCALE;
/** `controllableLabelRenderer` writes a name one body radius plus four pixels below its centre. */
const LABEL_GAP_CANVAS = 4;

/** Where the client draws the name of a blob whose body centre is at this world position. */
function labelCanvasPosition(
  worldX: number,
  worldY: number,
): { x: number; y: number } {
  return {
    x: worldX * PROJECTION_SCALE,
    y: worldY * PROJECTION_SCALE + PLAYER_RADIUS_CANVAS + LABEL_GAP_CANVAS,
  };
}

/**
 * The two `spawn` markers of `fixtures/maps/e2e-hazard-1920x1280`, which is where `SpawnSystem`
 * seats a blob exactly — it writes `PhysicsBody::create(spawn_points[chosen].position, ...)` at
 * rest, with no jitter and no offset.
 *
 * **The first sits on the path of the first hazard this seed draws and the second does not**, by
 * 503 wu against a 50 wu contact distance. The map file states how both numbers were derived. This
 * asymmetry is what the two flows below are: `hazard_spawn` is the only reader of the world's
 * seeded generator, so the first hazard of a match is a pure function of `[match] seed` and nothing
 * else — not of the tick the match started running on, and not of how long the browsers took to
 * connect. There is no waiting for a random hazard to eventually arrive anywhere in this file.
 */
const STRUCK_SPAWN_LABEL = labelCanvasPosition(640.065975168, 695.026526373);
const CLEAR_SPAWN_LABEL = labelCanvasPosition(925, 225);

/**
 * How close a drawn label must be to a projected marker to be the blob seated there.
 *
 * Half a canvas pixel is one world unit, against 549.6 wu between the two markers. It is loose
 * enough that a JSON round trip of a nine-decimal coordinate cannot fail it and tight enough that
 * mistaking one marker for the other is impossible.
 */
const SEATING_TOLERANCE_CANVAS_PIXELS = 0.5;

/**
 * A perfectly elastic head-on contact with a mass-200 body at 600 wu/s hands a unit-mass blob
 * `2 * 600 / (1/200 + 1) = 1194` wu/s, which at `drag_per_second=0` is 1.5 canvas pixels of travel
 * every 2.5 ms tick and does not decay. Four pixels is under three ticks of that and is eight world
 * units, which nothing else in this fixture can produce: the blob was seated at rest, no session
 * presses a key, there is no bot, and the zone applies no force. At the 20 Hz snapshot cadence the
 * first frame after contact already shows some thirty pixels, so there is no partly-moved state a
 * poll could mistake for the deflection.
 */
const DEFLECTION_THRESHOLD_CANVAS_PIXELS = 4;

/** The blob the hazard misses is never touched, so its drawn position must not move at all. */
const UNDISTURBED_TOLERANCE_CANVAS_PIXELS = 0.5;

/**
 * How long "the other blob has not moved" stays a property of this fixture after a deflection.
 *
 * The deflected blob keeps its 1194 wu/s and folds off the walls rather than settling, because
 * phase 1 drag has to be zero for a hazard to cross the arena at all
 * (`fixtures/blob-royale-browser-e2e-hazard-heavy.cfg` § `[simulation]`). It therefore wanders, and
 * it eventually finds the second blob: replaying this exact world offline, the second blob is bit
 * for bit where it was seated until 12.8 s after the contact and is knocked about from then on.
 * Eight seconds is that window with a third of it left over, and this flow refuses to make the claim
 * past it and says which invariant it lost rather than reporting a mystery — the same discipline
 * `blobRoyaleBrowserRoyaleMatch.spec.ts` applies to its wandering bot.
 */
const DEFLECTION_ISOLATION_BUDGET_MILLISECONDS = 8_000;

function requireDeflectionIsolationBudget(
  observedAtMilliseconds: number,
): void {
  const elapsedMilliseconds = Date.now() - observedAtMilliseconds;
  if (elapsedMilliseconds <= DEFLECTION_ISOLATION_BUDGET_MILLISECONDS) {
    return;
  }
  throw new BrowserE2EError(
    'BROWSER_E2E.DEFLECTION_ISOLATION_BUDGET_EXCEEDED',
    'The deflected blob has had enough time to reach the other spawn point, so an undisturbed second blob is no longer a property this fixture guarantees.',
    {
      budget_milliseconds: DEFLECTION_ISOLATION_BUDGET_MILLISECONDS,
      elapsed_milliseconds: elapsedMilliseconds,
    },
  );
}

/**
 * The placement an eliminated player receives when two were alive.
 *
 * `placement_recorder` records `alive_after + 1`, so the first of two players to be removed is
 * always `#2`. It is a literal rather than a pattern because the exact number is a property of the
 * roster this fixture declares.
 */
const ELIMINATED_PLACEMENT = '#2';

const MATCH_START_TIMEOUT_MILLISECONDS = 20_000;
/**
 * The first hazard is seated on the first tick whose number is a multiple of the 8 s spawn
 * interval, so up to 8 s can pass before one exists, and it then travels 706 wu at 600 wu/s — 1.18 s
 * — before its centre reaches the struck blob's. Twenty-five seconds covers both with room for the
 * 20 Hz snapshot cadence and a slow emulated toolchain, and is a bounded wait for an event whose
 * arrival is arithmetic rather than a hope.
 */
const HAZARD_ARRIVAL_TIMEOUT_MILLISECONDS = 25_000;

interface Seating {
  readonly clearName: string;
  readonly clearPage: Page;
  readonly struckName: string;
  readonly struckPage: Page;
}

/** The safe zone is the one translucent circle; every body fills with an opaque colour. */
function isZoneArc(arc: RecordedArc): boolean {
  return arc.fillStyle.startsWith('rgba(');
}

function bodyArcs(frame: RecordedFrame): readonly RecordedArc[] {
  return frame.arcs.filter((arc) => !isZoneArc(arc));
}

/**
 * The hazard discs in one frame, told apart from blobs by radius alone.
 *
 * A hazard is `radius_world_units=30` against a `player_radius_world_units=20`, and the projection
 * is exactly 0.5, so the two are 15 and 10 canvas pixels and no rounding stands between them. The
 * dashed lethal ring is a `stroke` and this recorder only observes `fill`, so this counts the
 * hazard body rather than its warning.
 */
function hazardArcs(frame: RecordedFrame): readonly RecordedArc[] {
  return bodyArcs(frame).filter(
    (arc) => Math.abs(arc.radius - HAZARD_RADIUS_CANVAS) < 0.001,
  );
}

async function countHazardArcs(page: Page): Promise<number> {
  const frame = await readCanvasFrame(page);
  return frame === null ? 0 : hazardArcs(frame).length;
}

function separation(
  left: { x: number; y: number },
  right: { x: number; y: number },
): number {
  return Math.hypot(left.x - right.x, left.y - right.y);
}

async function readLabel(page: Page, text: string): Promise<RecordedLabel> {
  return requireLabel(await requireCanvasFrame(page), text);
}

/**
 * Reports how far one blob has been drawn from where it was seated, or `NaN` when the frame or the
 * label is momentarily absent.
 *
 * `NaN` rather than a throw, for the same reason the royale flow does it: a poll that threw here
 * would report "no frame yet" as the failure it was waiting to disprove. `NaN` fails every
 * comparison, so the poll keeps waiting and its own message survives to the timeout.
 */
async function readDisplacementFromSpawn(
  page: Page,
  displayName: string,
  spawnLabel: { x: number; y: number },
): Promise<number> {
  const frame = await readCanvasFrame(page);
  if (frame === null) {
    return Number.NaN;
  }
  const label = frame.labels.find((entry) => entry.text === displayName);
  return label === undefined ? Number.NaN : separation(label, spawnLabel);
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
  const navigationResponse = await page.goto('/', {
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

/**
 * Decides which browser is which by reading where each blob was drawn, not by which connected
 * first.
 *
 * The rotating spawn policy assigns points in seat order and this flow does connect its two
 * sessions in a fixed order, so hard-coding the assignment would very probably work. It is still
 * the wrong thing to write down: the claim these flows make is about the blob the hazard's path
 * passes through, and reading that off the canvas is what makes the claim true by observation
 * instead of by an assumption about the seating counter that no assertion here would notice
 * breaking.
 *
 * The pairing is required to be one-to-one, so a frame in which both blobs somehow matched one
 * marker fails here with the positions in the message rather than silently picking the first.
 */
async function readSeating(
  pageA: Page,
  nameA: string,
  pageB: Page,
  nameB: string,
): Promise<Seating> {
  const frame = await requireCanvasFrame(pageA);
  const labelA = requireLabel(frame, nameA);
  const labelB = requireLabel(frame, nameB);
  const aIsStruck =
    separation(labelA, STRUCK_SPAWN_LABEL) < SEATING_TOLERANCE_CANVAS_PIXELS &&
    separation(labelB, CLEAR_SPAWN_LABEL) < SEATING_TOLERANCE_CANVAS_PIXELS;
  const bIsStruck =
    separation(labelB, STRUCK_SPAWN_LABEL) < SEATING_TOLERANCE_CANVAS_PIXELS &&
    separation(labelA, CLEAR_SPAWN_LABEL) < SEATING_TOLERANCE_CANVAS_PIXELS;
  if (aIsStruck === bIsStruck) {
    throw new BrowserE2EError(
      'BROWSER_E2E.HAZARD_SEATING_UNRESOLVED',
      'The two blobs were not drawn one at each of the map’s two spawn markers.',
      {
        clear_marker: `${CLEAR_SPAWN_LABEL.x},${CLEAR_SPAWN_LABEL.y}`,
        drawn_a: `${labelA.x},${labelA.y}`,
        drawn_b: `${labelB.x},${labelB.y}`,
        frame_index: frame.index,
        struck_marker: `${STRUCK_SPAWN_LABEL.x},${STRUCK_SPAWN_LABEL.y}`,
      },
    );
  }
  return aIsStruck
    ? {
        clearName: nameB,
        clearPage: pageB,
        struckName: nameA,
        struckPage: pageA,
      }
    : {
        clearName: nameA,
        clearPage: pageA,
        struckName: nameB,
        struckPage: pageB,
      };
}

/**
 * Everything both flows do before the hazard matters: start the server, seat both browsers, reach
 * `running`, and establish that each blob is where the map put it.
 */
async function startMatchWithTwoBrowsers(
  browser: Browser,
  request: APIRequestContext,
  server: BlobRoyaleServerProcess,
  contexts: BrowserContext[],
  pageErrors: string[],
): Promise<Seating> {
  await server.start();
  await waitForReadyServer(request, server);

  // `lobby_seat_count` is the whole field and there is no bot, so the match cannot leave the lobby
  // until both seats are taken and somebody presses Start -- which this flow does below, after both
  // browsers have connected, so neither is ever a mid-match joiner the spawn policy would defer.
  const contextA = await browser.newContext({ baseURL: PRODUCTION_ORIGIN });
  contexts.push(contextA);
  const pageA = await openSessionPage(contextA, pageErrors);
  await expect(pageA.getByRole('status')).toHaveText(CONNECTED_STATUS);
  await expect(matchHudCell(pageA, 'Phase')).toHaveText('lobby');
  await expect(matchHudCell(pageA, 'Alive')).toHaveText('1');
  const nameA = await readOwnDisplayName(pageA);

  const contextB = await browser.newContext({ baseURL: PRODUCTION_ORIGIN });
  contexts.push(contextB);
  const pageB = await openSessionPage(contextB, pageErrors);
  await expect(pageB.getByRole('status')).toHaveText(CONNECTED_STATUS);
  const nameB = await readOwnDisplayName(pageB);
  expect(nameB).not.toBe(nameA);

  // **The lobby is operated over the published wire, not by the client.** Since protocol 2.3 a match
  // starts only when every seat is filled and somebody sends `start_match`, and the lobby UI that
  // would do it is a later step of the same plan. The driver opens its own session, presses Start,
  // waits for `running`, and closes; the server destroys whatever it drove when it leaves, so
  // nothing it did is still in the world when the geometry below is read.
  await BlobRoyaleLobbyDriver.fillSeatsAndStart(SEATED_NPC_KIND);

  for (const page of [pageA, pageB]) {
    await expect(matchHudCell(page, 'Phase')).toHaveText('running', {
      timeout: MATCH_START_TIMEOUT_MILLISECONDS,
    });
  }
  await expect(matchHudCell(pageA, 'Alive')).toHaveText('2');
  await expect(matchHudCell(pageA, 'Placement')).toHaveText('In play');
  await expect(matchHudCell(pageB, 'Placement')).toHaveText('In play');

  // Both blobs are drawn at rest on their markers, which is the premise every geometric claim
  // below rests on. Asserting it here means a map edit that moved a marker fails with "the blobs
  // are not where the map put them" rather than as a mysteriously absent collision later.
  await expect
    .poll(async () => (await readCanvasFrame(pageA))?.labels.length ?? 0, {
      message:
        'both blobs must be labelled on the canvas before the match is read',
      timeout: MATCH_START_TIMEOUT_MILLISECONDS,
    })
    .toBe(2);
  return readSeating(pageA, nameA, pageB, nameB);
}

async function withServer(
  fixture: BlobRoyaleServerFixture,
  body: (server: BlobRoyaleServerProcess) => Promise<void>,
): Promise<void> {
  const server = await BlobRoyaleServerProcess.createFromEnvironment(fixture);
  try {
    await body(server);
  } finally {
    await server.cleanup();
  }
}

test('a lethal hazard eliminates the browser blob standing on its path', async ({
  browser,
  request,
}) => {
  // Two contexts, a lobby, a countdown, up to one spawn interval of waiting and a crossing.
  test.setTimeout(120_000);

  const pageErrors: string[] = [];
  const contexts: BrowserContext[] = [];

  try {
    await withServer(LETHAL_FIXTURE, async (server) => {
      const seating = await startMatchWithTwoBrowsers(
        browser,
        request,
        server,
        contexts,
        pageErrors,
      );

      // ------------------------------------------------------------ the comet is drawn
      // Before anything is claimed about an elimination, the thing that causes it has to be on the
      // screen. A blob is 10 canvas pixels and a hazard is 15, so this is the hazard and not a
      // player.
      await expect
        .poll(async () => countHazardArcs(seating.struckPage), {
          message:
            'the configured hazard kind must be seated and drawn as a disc of its own radius',
          timeout: HAZARD_ARRIVAL_TIMEOUT_MILLISECONDS,
        })
        .toBeGreaterThan(0);

      // ------------------------------------------------------------ and it kills
      // The struck session's own HUD is what says it: `#2` is the placement
      // `placement_recorder` writes for the first of two players to leave the roster. Nothing else
      // in this fixture can eliminate anybody — there is no bot, no session presses a key, and both
      // blobs sit more than 700 wu inside a zone that contracts at 1.6 wu/s.
      await expect(matchHudCell(seating.struckPage, 'Placement')).toHaveText(
        ELIMINATED_PLACEMENT,
        { timeout: HAZARD_ARRIVAL_TIMEOUT_MILLISECONDS },
      );

      // The blob is gone from the arena, not merely relabelled in the HUD: an eliminated entity is
      // destroyed, so the client stops being told about a body and draws neither disc nor name.
      await expect
        .poll(
          async () =>
            (await readCanvasFrame(seating.clearPage))?.labels.some(
              (label) => label.text === seating.struckName,
            ) ?? true,
          {
            message:
              'the eliminated blob must stop being drawn for the other browser too',
            timeout: HAZARD_ARRIVAL_TIMEOUT_MILLISECONDS,
          },
        )
        .toBe(false);

      // ------------------------------------------------------------ and only that one
      // The other blob is 503 wu clear of the same crossing, so it is still playing and still
      // exactly where it was seated. Without this the flow would also pass if the hazard had
      // eliminated everything, or if the zone had.
      await expect(matchHudCell(seating.clearPage, 'Alive')).toHaveText('1');
      await expect(matchHudCell(seating.clearPage, 'Placement')).toHaveText(
        'In play',
      );
      expect(
        separation(
          await readLabel(seating.clearPage, seating.clearName),
          CLEAR_SPAWN_LABEL,
        ),
      ).toBeLessThan(UNDISTURBED_TOLERANCE_CANVAS_PIXELS);

      // One player left is royale's own outcome, so the match ends. It stays ended:
      // `restart_delay_seconds=120` outlives this flow, which is what keeps the placement above
      // from being cleared by a restart while it is being read.
      await expect(matchHudCell(seating.clearPage, 'Phase')).toHaveText(
        'ended',
        { timeout: MATCH_START_TIMEOUT_MILLISECONDS },
      );

      await server.terminateWithSigterm();
    });
  } finally {
    for (const context of contexts) {
      await context.close();
    }
  }

  expect(pageErrors).toEqual([]);
});

test('a heavy hazard shoves a browser blob aside without eliminating it', async ({
  browser,
  request,
}) => {
  test.setTimeout(120_000);

  const pageErrors: string[] = [];
  const contexts: BrowserContext[] = [];

  try {
    await withServer(HEAVY_FIXTURE, async (server) => {
      const seating = await startMatchWithTwoBrowsers(
        browser,
        request,
        server,
        contexts,
        pageErrors,
      );

      // ------------------------------------------------------------ the same comet, not lethal
      await expect
        .poll(async () => countHazardArcs(seating.struckPage), {
          message:
            'the configured hazard kind must be seated and drawn as a disc of its own radius',
          timeout: HAZARD_ARRIVAL_TIMEOUT_MILLISECONDS,
        })
        .toBeGreaterThan(0);

      // ------------------------------------------------------------ it moves the blob it hits
      // The struck blob was seated at rest and nothing else in this world can accelerate it, so a
      // displacement of four canvas pixels is the mass-200 contact and can be nothing else. Both
      // browsers are asked: a blob that moved for one viewer and not the other would be a rendering
      // bug wearing this assertion's clothes.
      for (const page of [seating.struckPage, seating.clearPage]) {
        await expect
          .poll(
            async () =>
              readDisplacementFromSpawn(
                page,
                seating.struckName,
                STRUCK_SPAWN_LABEL,
              ),
            {
              message:
                'the hazard must visibly deflect the blob standing on its path',
              timeout: HAZARD_ARRIVAL_TIMEOUT_MILLISECONDS,
            },
          )
          .toBeGreaterThan(DEFLECTION_THRESHOLD_CANVAS_PIXELS);
      }
      const deflectionObservedAtMilliseconds = Date.now();

      // ------------------------------------------------------------ and eliminates nobody
      // The whole difference from the flow above is `lethal_on_contact`. The struck session is
      // still playing, still owns a body, and is still drawn.
      await expect(matchHudCell(seating.struckPage, 'Placement')).toHaveText(
        'In play',
      );
      await expect(matchHudCell(seating.clearPage, 'Placement')).toHaveText(
        'In play',
      );
      await expect(matchHudCell(seating.struckPage, 'Alive')).toHaveText('2');
      await expect(matchHudCell(seating.struckPage, 'Phase')).toHaveText(
        'running',
      );

      // ------------------------------------------------------------ and moves nothing else
      // The blob the crossing misses has not been drawn anywhere but on its own marker. This is
      // what separates "a hazard deflected the blob it hit" from "something moved both blobs".
      requireDeflectionIsolationBudget(deflectionObservedAtMilliseconds);
      expect(
        separation(
          await readLabel(seating.clearPage, seating.clearName),
          CLEAR_SPAWN_LABEL,
        ),
      ).toBeLessThan(UNDISTURBED_TOLERANCE_CANVAS_PIXELS);

      await server.terminateWithSigterm();
    });
  } finally {
    for (const context of contexts) {
      await context.close();
    }
  }

  expect(pageErrors).toEqual([]);
});
