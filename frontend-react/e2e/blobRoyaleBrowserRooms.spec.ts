import {
  expect,
  test as playwrightTest,
  type BrowserContext,
  type Locator,
  type Page,
} from '@playwright/test';

import { BlobRoyaleServerProcess } from './BlobRoyaleServerProcess';
import {
  CONNECTED_STATUS,
  PRODUCTION_ORIGIN,
  joinRoomButton,
  lobbyStartButton,
  matchHudCell,
  roomCard,
  roomFact,
  startMatchFromLobby,
  waitForReadyServer,
} from './browserFlowSupport';

/**
 * Two rooms, two seats each, no configured bot. See `fixtures/blob-royale-browser-e2e-rooms.cfg`
 * for why each number is the number it is; the two this file depends on by name are
 * `lobby_seat_count=2` and `restart_delay_seconds=4`.
 */
const ROOMS_FIXTURE = Object.freeze({
  configurationFileName: 'blob-royale-browser-e2e-rooms.cfg',
  scenarioFileName: null,
});

const SESSION_DISPLAY_NAME_PATTERN = /^player-[1-9][0-9]*$/;
/** A countdown of `[royale] countdown_seconds` and the frame that publishes `running` after it. */
const MATCH_START_TIMEOUT_MILLISECONDS = 20_000;
/**
 * The last person leaving a running match ends it within a control poll, and the machine walks
 * `ended -> lobby` after the fixture's four-second restart delay; the directory then reads it on
 * its next once-a-second refresh. Fifteen seconds is headroom for a slow emulated toolchain.
 */
const ROOM_RESET_TIMEOUT_MILLISECONDS = 15_000;

interface RoomsFixtures {
  readonly blobRoyaleServer: BlobRoyaleServerProcess;
}

const test = playwrightTest.extend<RoomsFixtures>({
  blobRoyaleServer: async ({}, use) => {
    const server =
      await BlobRoyaleServerProcess.createFromEnvironment(ROOMS_FIXTURE);
    try {
      await use(server);
    } finally {
      await server.cleanup();
    }
  },
});

async function openDirectory(
  context: BrowserContext,
  pageErrors: string[],
): Promise<Page> {
  const page = await context.newPage();
  page.on('pageerror', (error) => {
    pageErrors.push(error.message);
  });
  const navigationResponse = await page.goto('/', {
    waitUntil: 'domcontentloaded',
  });
  expect(navigationResponse?.status()).toBe(200);
  await expect(page).toHaveTitle('Blob Royale');
  await expect(
    page.getByRole('heading', { level: 1, name: 'Blob Royale' }),
  ).toBeVisible();
  await expect(
    page.getByRole('heading', { level: 2, name: 'Rooms' }),
  ).toBeVisible();
  return page;
}

async function readOwnDisplayName(page: Page): Promise<string> {
  const displayNameCell = matchHudCell(page, 'Player');
  await expect(displayNameCell).toHaveText(SESSION_DISPLAY_NAME_PATTERN);
  return (await displayNameCell.textContent())?.trim() ?? '';
}

/** The seat cards of the lobby panel, in seat-index order. */
function seatCards(page: Page): Locator {
  return page.locator('.SeatGrid').getByRole('listitem');
}

/**
 * Records, from inside the page, every committed DOM state in which Start was enabled while a seat
 * still read `Empty` or `(joining)`. The joining state lasts one control poll -- the reconciliation
 * builds a declared bot within 25 ms -- which a polled assertion from outside the page would see
 * only sometimes; a MutationObserver sees every committed frame, so the rule is proven for each
 * state the client actually rendered rather than for the ones this process happened to sample.
 */
async function installStartInvariantRecorder(page: Page): Promise<void> {
  await page.evaluate(() => {
    const violations: string[] = [];
    const check = (): void => {
      const start = document.querySelector<HTMLButtonElement>('.StartButton');
      if (start === null || start.disabled) {
        return;
      }
      const seatLabels = [...document.querySelectorAll('.SeatLabel')].map(
        (element) => element.textContent ?? '',
      );
      if (
        seatLabels.some(
          (label) => label === 'Empty' || label.includes('(joining)'),
        )
      ) {
        violations.push(seatLabels.join('|'));
      }
    };
    new MutationObserver(check).observe(document.body, {
      attributes: true,
      characterData: true,
      childList: true,
      subtree: true,
    });
    (
      window as unknown as { __startInvariantViolations: string[] }
    ).__startInvariantViolations = violations;
  });
}

async function readStartInvariantViolations(page: Page): Promise<string[]> {
  return page.evaluate(
    () =>
      (window as unknown as { __startInvariantViolations?: string[] })
        .__startInvariantViolations ?? ['recorder_not_installed'],
  );
}

/**
 * Fills the one empty seat of a two-seat lobby with a wanderer through the lobby's own menu, and
 * proves along the way that Start was disabled while the seat was empty and never enabled while
 * the bot was still joining.
 */
async function seatBotAndStart(
  page: Page,
  openMenu: (emptySeat: Locator) => Promise<void>,
): Promise<void> {
  await expect(lobbyStartButton(page)).toBeDisabled();
  await expect(
    page.getByText('Waiting for 1 empty seat to be filled.'),
  ).toBeVisible();

  const emptySeat = page.getByRole('button', { name: 'Seat 2 Empty' });
  await openMenu(emptySeat);
  const menu = page.getByRole('menu', { name: 'Bots for seat 2' });
  await expect(menu).toBeVisible();
  await installStartInvariantRecorder(page);
  await menu.getByRole('menuitem', { name: 'wanderer' }).click();
  await expect(menu).toHaveCount(0);

  const secondSeat = seatCards(page).nth(1);
  await expect(secondSeat).toContainText('wanderer');
  await expect(secondSeat).not.toContainText('(joining)');
  // The reason under Start is gone; the overlay's own "Waiting for players" is the arena's, not Start's.
  await expect(page.locator('.StartReason')).toHaveCount(0);
  expect(await readStartInvariantViolations(page)).toEqual([]);

  await startMatchFromLobby(page);
  await expect(matchHudCell(page, 'Phase')).toHaveText('running', {
    timeout: MATCH_START_TIMEOUT_MILLISECONDS,
  });
  await expect(matchHudCell(page, 'Alive')).toHaveText('2');
}

test('two browsers play in two rooms, and a room whose last person left returns to its lobby', async ({
  blobRoyaleServer,
  browser,
  request,
}) => {
  // Two contexts, two seatings, two countdowns, and a restart delay do not fit the default budget.
  test.setTimeout(120_000);

  const pageErrors: string[] = [];
  const contexts: BrowserContext[] = [];

  try {
    await blobRoyaleServer.start();
    await waitForReadyServer(request, blobRoyaleServer);

    // ---------------------------------------------------------------- the directory, empty
    const contextA = await browser.newContext({ baseURL: PRODUCTION_ORIGIN });
    contexts.push(contextA);
    const pageA = await openDirectory(contextA, pageErrors);
    for (const lobbyId of [1, 2]) {
      const card = roomCard(pageA, lobbyId);
      await expect(card).toBeVisible();
      await expect(roomFact(card, 'Mode')).toHaveText(
        'royale on e2e-royale-1920x1280',
      );
      await expect(roomFact(card, 'Phase')).toHaveText('In the lobby');
      await expect(roomFact(card, 'Seats')).toHaveText('0 of 2 seats filled');
      await expect(roomFact(card, 'In the room')).toHaveText(
        '0 players, 0 bots',
      );
      await expect(joinRoomButton(pageA, lobbyId)).toBeEnabled();
    }

    // ---------------------------------------------------------------- A joins room 1
    await joinRoomButton(pageA, 1).click();
    await expect(pageA).toHaveURL(`${PRODUCTION_ORIGIN}/?lobby=1`);
    await expect(
      pageA.getByRole('heading', { level: 2, name: 'Room 1' }),
    ).toBeVisible();
    await expect(pageA.getByRole('status')).toHaveText(CONNECTED_STATUS);
    await expect(matchHudCell(pageA, 'Phase')).toHaveText('lobby');
    const nameA = await readOwnDisplayName(pageA);
    await expect(seatCards(pageA)).toHaveCount(2);
    await expect(seatCards(pageA).first()).toContainText(`${nameA} (you)`);

    // A right-clicks the empty seat, seats a wanderer, and starts room 1.
    await seatBotAndStart(pageA, async (emptySeat) => {
      await emptySeat.click({ button: 'right' });
    });

    // ---------------------------------------------------------------- B sees room 1 occupied
    const contextB = await browser.newContext({ baseURL: PRODUCTION_ORIGIN });
    contexts.push(contextB);
    const pageB = await openDirectory(contextB, pageErrors);
    const roomOneForB = roomCard(pageB, 1);
    await expect(roomFact(roomOneForB, 'Phase')).toHaveText('Match running');
    await expect(roomFact(roomOneForB, 'Seats')).toHaveText(
      '2 of 2 seats filled',
    );
    await expect(roomFact(roomOneForB, 'In the room')).toHaveText(
      '1 player, 1 bot',
    );
    // Every seat is taken and the match is past the lobby, so there is no seat a join could take:
    // the directory says so before the server would have to close a hopeful session.
    await expect(joinRoomButton(pageB, 1)).toBeDisabled();
    await expect(roomOneForB).toContainText('Room 1 has every seat taken.');
    await expect(joinRoomButton(pageB, 2)).toBeEnabled();

    // ---------------------------------------------------------------- B joins room 2
    await joinRoomButton(pageB, 2).click();
    await expect(pageB).toHaveURL(`${PRODUCTION_ORIGIN}/?lobby=2`);
    await expect(
      pageB.getByRole('heading', { level: 2, name: 'Room 2' }),
    ).toBeVisible();
    await expect(pageB.getByRole('status')).toHaveText(CONNECTED_STATUS);
    await expect(matchHudCell(pageB, 'Phase')).toHaveText('lobby');
    const nameB = await readOwnDisplayName(pageB);
    expect(nameB).not.toBe(nameA);

    // B opens the same menu from the seat button itself, which is the keyboard's path too.
    await seatBotAndStart(pageB, async (emptySeat) => {
      await emptySeat.click();
    });

    // ---------------------------------------------------------------- two matches, independent
    await expect(matchHudCell(pageA, 'Phase')).toHaveText('running');
    await expect(matchHudCell(pageA, 'Alive')).toHaveText('2');
    await expect(matchHudCell(pageB, 'Alive')).toHaveText('2');

    // ---------------------------------------------------------------- A leaves room 1
    await pageA.getByRole('button', { name: 'Leave room' }).click();
    await expect(pageA).toHaveURL(`${PRODUCTION_ORIGIN}/`);
    await expect(
      pageA.getByRole('heading', { level: 2, name: 'Rooms' }),
    ).toBeVisible();
    // A leave is not a refusal: the directory shows no notice.
    await expect(pageA.getByRole('alert')).toHaveCount(0);

    // Room 1 had nobody left in it: the control loop retired its bot, the match ended, and the
    // machine walked `ended -> lobby` after the restart delay, where the reconciliation reseated
    // the declared bot. That is the abandonment rule, observed end to end from the directory.
    const roomOneForA = roomCard(pageA, 1);
    await expect(roomFact(roomOneForA, 'Phase')).toHaveText('In the lobby', {
      timeout: ROOM_RESET_TIMEOUT_MILLISECONDS,
    });
    await expect(roomFact(roomOneForA, 'In the room')).toHaveText(
      '0 players, 1 bot',
    );
    await expect(roomFact(roomOneForA, 'Seats')).toHaveText(
      '1 of 2 seats filled',
    );
    await expect(joinRoomButton(pageA, 1)).toBeEnabled();

    // Room 2 never noticed.
    const roomTwoForA = roomCard(pageA, 2);
    await expect(roomFact(roomTwoForA, 'Phase')).toHaveText('Match running');
    await expect(roomFact(roomTwoForA, 'In the room')).toHaveText(
      '1 player, 1 bot',
    );
    await expect(matchHudCell(pageB, 'Phase')).toHaveText('running');
    await expect(matchHudCell(pageB, 'Alive')).toHaveText('2');

    await blobRoyaleServer.terminateWithSigterm();
  } finally {
    for (const context of contexts) {
      await context.close();
    }
  }

  expect(pageErrors).toEqual([]);
});
