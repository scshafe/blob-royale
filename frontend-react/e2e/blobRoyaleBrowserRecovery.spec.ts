import {
  expect,
  test as playwrightTest,
  type APIRequestContext,
  type Locator,
} from '@playwright/test';

import { BlobRoyaleServerProcess } from './BlobRoyaleServerProcess';
import { BrowserE2EError } from './BrowserE2EError';

const PRODUCTION_ORIGIN = 'http://127.0.0.1:5173';
const READINESS_PATH = '/api/v1/health/ready';
const READINESS_TIMEOUT_MILLISECONDS = 10_000;
const READINESS_RETRY_INTERVAL_MILLISECONDS = 50;
const CONNECTED_STATUS = 'Connected to the read-only snapshot stream.';
const RETRYING_STATUS =
  'The snapshot stream disconnected. Retrying with bounded backoff…';
const COMPLETE_TICK_PATTERN = /^Complete tick ([1-9][0-9]*) with 2 players\.$/;

interface BrowserE2EFixtures {
  readonly blobRoyaleServer: BlobRoyaleServerProcess;
}

const test = playwrightTest.extend<BrowserE2EFixtures>({
  blobRoyaleServer: async ({}, use) => {
    const server = await BlobRoyaleServerProcess.createFromEnvironment();
    try {
      await use(server);
    } finally {
      await server.cleanup();
    }
  },
});

function delay(milliseconds: number): Promise<void> {
  return new Promise((resolve) => {
    setTimeout(resolve, milliseconds);
  });
}

function isRetryableReadinessStatus(responseStatus: number): boolean {
  return responseStatus === 502 || responseStatus === 503;
}

async function waitForReadyServer(
  request: APIRequestContext,
  server: BlobRoyaleServerProcess,
): Promise<void> {
  const deadline = Date.now() + READINESS_TIMEOUT_MILLISECONDS;
  let lastTransportError: unknown = null;

  while (Date.now() < deadline) {
    await server.assertRunning();
    try {
      const response = await request.get(READINESS_PATH, {
        headers: {
          Accept: 'application/json',
          Origin: PRODUCTION_ORIGIN,
          'X-Request-ID': 'browser-e2e-readiness',
        },
        timeout: 1_000,
      });
      if (isRetryableReadinessStatus(response.status())) {
        await delay(READINESS_RETRY_INTERVAL_MILLISECONDS);
        continue;
      }
      if (response.status() !== 200) {
        throw new BrowserE2EError(
          'BROWSER_E2E.READINESS_STATUS_INVALID',
          'Readiness through the production same-origin proxy returned an unexpected status.',
          { response_status: response.status() },
        );
      }

      expect(response.headers()['content-type']).toMatch(
        /^application\/json(?:;|$)/,
      );
      expect(await response.json()).toEqual({
        data: {
          snapshot_available: true,
          status: 'ready',
        },
        error: null,
        meta: {
          protocol_version: '1.0',
          request_id: 'browser-e2e-readiness',
          schema_id: 'blob-royale://protocol/v1/readiness-response',
        },
      });
      return;
    } catch (error) {
      if (error instanceof BrowserE2EError) {
        throw error;
      }
      lastTransportError = error;
      await delay(READINESS_RETRY_INTERVAL_MILLISECONDS);
    }
  }

  throw new BrowserE2EError(
    'BROWSER_E2E.READINESS_TIMEOUT',
    'The exact server did not become ready through the production same-origin proxy.',
    { timeout_milliseconds: READINESS_TIMEOUT_MILLISECONDS },
    lastTransportError,
  );
}

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

  const metadataTable = page.getByRole('table', {
    name: 'Snapshot protocol metadata',
  });
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
  await expect
    .poll(() => readSnapshotRequestId(metadataTable), {
      message:
        'the restarted server must produce a new snapshot stream request ID',
      timeout: 10_000,
    })
    .not.toBe(firstServerRequestId);
  await assertTwoIncreasingCompleteTicks(completeTickCaption);

  await blobRoyaleServer.terminateWithSigterm();
  expect(pageErrors).toEqual([]);
});
