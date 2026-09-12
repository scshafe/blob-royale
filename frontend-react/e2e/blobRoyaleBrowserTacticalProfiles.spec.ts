import { expect, test as playwrightTest, type Page } from '@playwright/test';
import type {
  SessionSnapshotMessage,
  SessionWelcomeMessage,
} from '../src/features/simulation/simulationProtocolTypes';
import { BlobRoyaleServerProcess } from './BlobRoyaleServerProcess';
import {
  CONNECTED_STATUS,
  connectionStatus,
  lobbyStartButton,
  matchHudCell,
  recordSessionTraffic,
  waitForReadyServer,
  type RecordedSessionTraffic,
} from './browserFlowSupport';

const TACTICAL_PROFILES_FIXTURE = Object.freeze({
  configurationFileName: 'blob-royale-browser-e2e-tactical-profiles.cfg',
  scenarioFileName: null,
});
const SELECTED_PROFILE_SEAT_INDEX = 2;
const EXPECTED_PROFILE_CHOICES = Object.freeze([
  { npc_kind: 'tactical', profile_name: 'steady' },
  { npc_kind: 'tactical', profile_name: 'quick' },
]);

const test = playwrightTest.extend<{
  readonly blobRoyaleServer: BlobRoyaleServerProcess;
}>({
  blobRoyaleServer: async ({}, use) => {
    const server = await BlobRoyaleServerProcess.createFromEnvironment(
      TACTICAL_PROFILES_FIXTURE,
    );
    try {
      await use(server);
    } finally {
      await server.cleanup();
    }
  },
});

/** Record every DOM publication, including joining states too brief for outside polling. */
async function installProfileLabelRecorder(page: Page): Promise<void> {
  await page.evaluate(() => {
    const violations: string[] = [];
    const inspect = (): void => {
      for (const element of document.querySelectorAll(
        '.SeatCard-npc .SeatLabel',
      )) {
        const label = element.textContent ?? '';
        if (!/ \/ (steady|quick)( \(joining\))?$/.test(label))
          violations.push(label);
      }
    };
    new MutationObserver(inspect).observe(document.body, {
      childList: true,
      characterData: true,
      subtree: true,
    });
    (
      window as unknown as { __tacticalProfileLabelViolations: string[] }
    ).__tacticalProfileLabelViolations = violations;
    inspect();
  });
}

async function expectNoProfileLabelViolations(page: Page): Promise<void> {
  expect(
    await page.evaluate(() => {
      const violations = (
        window as unknown as { __tacticalProfileLabelViolations?: string[] }
      ).__tacticalProfileLabelViolations;
      if (violations === undefined)
        throw new Error('TEST.TACTICAL_LABEL_RECORDER_MISSING');
      return violations;
    }),
  ).toEqual([]);
}

function welcomeFrames(
  traffic: RecordedSessionTraffic,
): readonly SessionWelcomeMessage[] {
  return traffic.receivedFrames
    .map((frame) => JSON.parse(frame) as SessionWelcomeMessage)
    .filter(
      (frame) =>
        frame.meta.schema_id === 'blob-royale://protocol/v3/welcome-message',
    );
}

/** Both joining and occupied snapshots must carry complete declarations whenever observed. */
function expectCompleteProfileFrames(traffic: RecordedSessionTraffic): void {
  const frames = traffic.receivedFrames
    .map((frame) => JSON.parse(frame) as SessionSnapshotMessage)
    .filter(
      (frame) =>
        frame.meta.schema_id === 'blob-royale://protocol/v3/snapshot-message',
    );
  expect(frames.length).toBeGreaterThan(0);
  let observedOccupiedQuick = false;
  for (const frame of frames) {
    for (const seat of frame.data.match.seats) {
      if (seat.kind !== 'npc') continue;
      expect(seat.npc_kind).toBe('tactical');
      expect(['steady', 'quick']).toContain(seat.profile_name);
      if (seat.profile_name === 'quick' && seat.controller_id !== null)
        observedOccupiedQuick = true;
    }
  }
  expect(observedOccupiedQuick).toBe(true);
}

test('configured and menu-selected tactical profiles survive a new browser session in the same room', async ({
  blobRoyaleServer,
  page,
  request,
}) => {
  test.setTimeout(60_000);
  const pageErrors: string[] = [];
  page.on('pageerror', (error) => {
    pageErrors.push(error.message);
  });
  await blobRoyaleServer.start();
  await waitForReadyServer(request, blobRoyaleServer);
  const traffic = recordSessionTraffic(page);
  const response = await page.goto('/?lobby=1', {
    waitUntil: 'domcontentloaded',
  });
  expect(response?.status()).toBe(200);
  await expect(connectionStatus(page)).toHaveText(CONNECTED_STATUS);
  await expect(matchHudCell(page, 'Phase')).toHaveText('lobby');
  await expect(page.locator('.SeatCard').nth(0)).toContainText('/ steady');
  await expect(page.locator('.SeatCard').nth(0)).not.toContainText('(joining)');
  await expect(lobbyStartButton(page)).toBeDisabled();
  await installProfileLabelRecorder(page);
  await page
    .getByRole('button', { name: 'Seat 3 Empty' })
    .click({ button: 'right' });
  const menu = page.getByRole('menu', { name: 'Bots for seat 3' });
  await expect(menu).toHaveCSS('overflow-y', 'auto');
  await expect(
    menu.getByRole('menuitem', { name: 'tactical', exact: true }),
  ).toHaveCount(0);
  await expect(
    menu.getByRole('menuitem', { name: 'wanderer', exact: true }),
  ).toBeVisible();
  await expect(
    menu.getByRole('menuitem', { name: 'tactical / steady' }),
  ).toBeVisible();
  await menu.getByRole('menuitem', { name: 'tactical / quick' }).click();
  await expect(menu).toHaveCount(0);
  const selectedSeat = page
    .locator('.SeatCard')
    .nth(SELECTED_PROFILE_SEAT_INDEX);
  await expect(selectedSeat).toContainText('/ quick');
  await expect(selectedSeat).not.toContainText('(joining)');
  await expect(lobbyStartButton(page)).toBeEnabled();
  await expectNoProfileLabelViolations(page);

  const firstWelcome = welcomeFrames(traffic)[0];
  expect(firstWelcome?.data.npc_profiles).toEqual(EXPECTED_PROFILE_CHOICES);
  expect(firstWelcome?.data.npc_controller_kinds).not.toContain('tactical');
  const sentBeforeReload = traffic.sentFrames.length;
  await page.reload({ waitUntil: 'domcontentloaded' });
  await expect(connectionStatus(page)).toHaveText(CONNECTED_STATUS);
  await expect(matchHudCell(page, 'Phase')).toHaveText('lobby');
  await expect(page.locator('.SeatCard').nth(0)).toContainText('/ steady');
  await expect(selectedSeat).toContainText('/ quick');
  await expect(selectedSeat).not.toContainText('(joining)');
  await expect(lobbyStartButton(page)).toBeEnabled();
  expect(traffic.sentFrames).toHaveLength(sentBeforeReload);
  const welcomes = welcomeFrames(traffic);
  expect(welcomes).toHaveLength(2);
  expect(welcomes[1]?.meta.request_id).not.toBe(firstWelcome?.meta.request_id);
  expect(welcomes[1]?.data.npc_profiles).toEqual(EXPECTED_PROFILE_CHOICES);
  expectCompleteProfileFrames(traffic);
  expect(traffic.sentFrames.map((frame) => JSON.parse(frame))).toEqual([
    {
      kind: 'seat_npc',
      payload: {
        seat_index: SELECTED_PROFILE_SEAT_INDEX,
        npc_kind: 'tactical',
        profile_name: 'quick',
      },
    },
  ]);
  expect(pageErrors).toEqual([]);
});
