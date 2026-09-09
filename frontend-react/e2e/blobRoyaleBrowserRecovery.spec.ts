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
 * Three entities and three players: the two the scenario seeds, plus the one the browser spawns.
 *
 * A scenario row seeds an entity that decides for itself, with `controller_id == entity_id`
 * (`src/simulation/game_world.hpp` § `EntitySeed::create`), so both seeded discs already carry a
 * controller and both count as players. The browser is the third. It gets its own body because the
 * runtime opens its controller cursor above every controller id the loaded world already carries
 * (`src/runtime/simulation_runtime.cpp` § `first_session_controller_id`); without that floor the
 * first session would be issued the id entity 1 already holds and would adopt that disc instead of
 * spawning, since the client resolves its own body by controller id on every frame. This assertion
 * is therefore the observable form of that fix: a session that adopted a seeded body would show two
 * entities here, not three. It holds identically across the restart, because the replacement
 * process computes the same floor from the same seeded world.
 */
const COMPLETE_TICK_PATTERN =
  /^Complete tick ([1-9][0-9]*) with 3 entities and 3 players\.$/;

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

  // Room 1 by its URL: the directory in front of it is Step 15's to drive through the UI.
  const navigationResponse = await page.goto('/?lobby=1', {
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
  // One of those three players is this session, and it is the one the session spawned rather than a
  // seeded disc it adopted: without this the caption above would also pass with the browser
  // connected but holding no body at all.
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
