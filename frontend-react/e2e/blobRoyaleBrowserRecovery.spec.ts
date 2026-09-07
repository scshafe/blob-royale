import { expect, test as playwrightTest, type Locator } from '@playwright/test';

import { BlobRoyaleServerProcess } from './BlobRoyaleServerProcess';
import {
  CONNECTED_STATUS,
  RETRYING_STATUS,
  matchHudCell,
  waitForReadyServer,
} from './browserFlowSupport';

/**
 * The sandbox fixture, and it stays sandbox. This flow asserts that the same world is rendered
 * before and after a server restart, and a royale process restarts a match rather than resuming
 * one; the royale flow lives in `blobRoyaleBrowserRoyaleMatch.spec.ts` on its own configuration.
 */
const RECOVERY_FIXTURE = Object.freeze({
  configurationFileName: 'blob-royale-browser-e2e.cfg',
  scenarioFileName: 'blob-royale-browser-e2e.csv',
});

/**
 * Two entities and two players, and no third entity for the browser.
 *
 * A scenario row seeds an entity that decides for itself, with `controller_id == entity_id`
 * (`src/simulation/game_world.hpp` § `EntitySeed::create`), so both seeded discs already carry a
 * controller and both count as players. The controller directory issues session ids from one, so
 * the first session's controller id is the one entity 1 already carries: the session recognises
 * that entity as its own body and never asks to spawn. That is the documented "until sessions
 * issue their own controller ids" hand-off, and it holds identically across the restart because
 * the replacement process issues ids from one again.
 */
const COMPLETE_TICK_PATTERN =
  /^Complete tick ([1-9][0-9]*) with 2 entities and 2 players\.$/;

interface BrowserE2EFixtures {
  readonly blobRoyaleServer: BlobRoyaleServerProcess;
}

const test = playwrightTest.extend<BrowserE2EFixtures>({
  blobRoyaleServer: async ({}, use) => {
    const server =
      await BlobRoyaleServerProcess.createFromEnvironment(RECOVERY_FIXTURE);
    try {
      await use(server);
    } finally {
      await server.cleanup();
    }
  },
});

async function readCompleteTick(caption: Locator): Promise<number> {
  const captionText = await caption.textContent();
  const match = captionText?.match(COMPLETE_TICK_PATTERN);
  return match === null || match === undefined ? -1 : Number(match[1]);
}

async function assertTwoIncreasingCompleteTicks(
  pageCaption: Locator,
): Promise<void> {
  await expect(pageCaption).toHaveText(COMPLETE_TICK_PATTERN);
  const firstTick = await readCompleteTick(pageCaption);
  expect(firstTick).toBeGreaterThan(0);
  await expect
    .poll(() => readCompleteTick(pageCaption), {
      message: 'the rendered complete tick must advance',
      timeout: 5_000,
    })
    .toBeGreaterThan(firstTick);
}

async function readSnapshotRequestId(metadataTable: Locator): Promise<string> {
  const requestId = await metadataTable
    .getByRole('row', { name: /^Request ID / })
    .getByRole('cell')
    .textContent();
  return requestId?.trim() ?? '';
}

test('production Chromium reconnects to a restarted exact server', async ({
  blobRoyaleServer,
  page,
  request,
}) => {
  const pageErrors: string[] = [];
  page.on('pageerror', (error) => {
    pageErrors.push(error.message);
  });

  await blobRoyaleServer.start();
  await waitForReadyServer(request, blobRoyaleServer);

  const navigationResponse = await page.goto('/', {
    waitUntil: 'domcontentloaded',
  });
  expect(navigationResponse?.status()).toBe(200);
  await expect(page).toHaveTitle('Blob Royale');
  await expect(
    page.getByRole('heading', { level: 1, name: 'Blob Royale' }),
  ).toBeVisible();
  await expect(page.getByRole('status')).toHaveText(CONNECTED_STATUS);

  const canvas = page.getByRole('img', {
    name: 'Blob Royale simulation world',
  });
  await expect(canvas).toBeVisible();
  const completeTickCaption = page.locator('.SimulationCanvas figcaption');
  await assertTwoIncreasingCompleteTicks(completeTickCaption);
  // One of those two players is this session: without this the caption above would also pass with
  // the browser connected but holding no body at all.
  await expect(matchHudCell(page, 'Placement')).toHaveText('In play');

  // Protocol metadata is a disclosure now, so a player who wants it must ask. Opening it here is
  // part of the flow rather than a detour: the request ID underneath is how this test tells one
  // server's snapshot stream from the next one's.
  const detailsToggle = page.getByRole('button', {
    name: /^(?:Show|Hide) simulation details$/,
  });
  await expect(detailsToggle).toHaveText('Show simulation details');
  await expect(detailsToggle).toHaveAttribute('aria-expanded', 'false');
  await expect(
    page.getByRole('table', { name: 'Session protocol metadata' }),
  ).toHaveCount(0);
  await detailsToggle.click();
  await expect(detailsToggle).toHaveText('Hide simulation details');
  await expect(detailsToggle).toHaveAttribute('aria-expanded', 'true');

  const metadataTable = page.getByRole('table', {
    name: 'Session protocol metadata',
  });
  await expect(metadataTable).toBeVisible();
  const firstServerRequestId = await readSnapshotRequestId(metadataTable);
  expect(firstServerRequestId).toMatch(/^br-[0-9a-f]+-[0-9a-f]+$/);

  await Promise.all([
    blobRoyaleServer.terminateWithSigterm(),
    expect(page.getByRole('status')).toHaveText(RETRYING_STATUS),
  ]);
  await expect(canvas).toHaveCount(0);

  await blobRoyaleServer.start();
  await waitForReadyServer(request, blobRoyaleServer);
  await expect(page.getByRole('status')).toHaveText(CONNECTED_STATUS, {
    timeout: 10_000,
  });
  await expect(
    page.getByRole('img', { name: 'Blob Royale simulation world' }),
  ).toBeVisible();
  // The disclosure the player opened survives a reconnect: losing the server must not silently
  // close the panel they were reading.
  await expect(detailsToggle).toHaveText('Hide simulation details');
  await expect(detailsToggle).toHaveAttribute('aria-expanded', 'true');
  await expect
    .poll(() => readSnapshotRequestId(metadataTable), {
      message:
        'the restarted server must produce a new snapshot stream request ID',
      timeout: 10_000,
    })
    .not.toBe(firstServerRequestId);
  await assertTwoIncreasingCompleteTicks(completeTickCaption);
  await expect(matchHudCell(page, 'Placement')).toHaveText('In play');

  await blobRoyaleServer.terminateWithSigterm();
  expect(pageErrors).toEqual([]);
});
