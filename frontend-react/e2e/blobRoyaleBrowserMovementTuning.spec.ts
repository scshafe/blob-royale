import { expect, test, type BrowserContext, type Page } from '@playwright/test';
import { BlobRoyaleServerProcess } from './BlobRoyaleServerProcess';
import { BrowserE2EError } from './BrowserE2EError';
import type { SessionLobbyDirectoryMessage } from '../src/features/simulation/simulationProtocolTypes';
import {
  CONNECTED_STATUS,
  PRODUCTION_ORIGIN,
  aimFromPaintedBody,
  connectionStatus,
  entityForController,
  focusSimulationCanvas,
  installCanvasRecorder,
  matchHudCell,
  recordedCommands,
  recordedSnapshots,
  recordedWelcome,
  recordSessionTraffic,
  startMatchFromLobby,
  waitForReadyServer,
  type RecordedSessionTraffic,
} from './browserFlowSupport';

interface TuningSession {
  readonly page: Page;
  readonly traffic: RecordedSessionTraffic;
  readonly controllerId: number;
  readonly displayName: string;
}

async function openTuningSession(
  context: BrowserContext,
  lobbyId: number,
  errors: string[],
): Promise<TuningSession> {
  const page = await context.newPage();
  page.on('pageerror', (error) => errors.push(error.message));
  const traffic = recordSessionTraffic(page);
  await installCanvasRecorder(page);
  expect(
    (
      await page.goto(`/?lobby=${lobbyId}`, { waitUntil: 'domcontentloaded' })
    )?.status(),
  ).toBe(200);
  await expect(connectionStatus(page)).toHaveText(CONNECTED_STATUS);
  await expect(matchHudCell(page, 'Phase')).toHaveText('lobby');
  await expect
    .poll(() => recordedWelcome(traffic)?.data.controller_id)
    .toBeGreaterThan(0);
  const welcome = recordedWelcome(traffic);
  if (welcome === undefined)
    throw new BrowserE2EError(
      'BROWSER_E2E.TUNING_WELCOME_ABSENT',
      'A connected tuning session must have an observed welcome.',
      { lobby_id: lobbyId },
    );
  await expect(matchHudCell(page, 'Player')).toHaveText(/^player-[1-9][0-9]*$/);
  const displayName =
    (await matchHudCell(page, 'Player').textContent())?.trim() ?? '';
  return {
    page,
    traffic,
    controllerId: welcome.data.controller_id,
    displayName,
  };
}

function latestSnapshot(session: TuningSession) {
  return recordedSnapshots(session.traffic).at(-1);
}

function tuningPanel(session: TuningSession) {
  return session.page.getByRole('region', { name: 'Movement tuning' });
}

test('a running room commits peer tuning to held cursor input while another room and a dirty draft stay isolated', async ({
  browser,
  request,
}) => {
  test.setTimeout(75_000);
  const server = await BlobRoyaleServerProcess.createFromEnvironment({
    configurationFileName: 'blob-royale-browser-e2e-rooms.cfg',
    scenarioFileName: null,
  });
  const contexts: BrowserContext[] = [];
  const errors: string[] = [];
  try {
    await server.start();
    await waitForReadyServer(request, server);
    for (let index = 0; index < 3; index += 1)
      contexts.push(
        await browser.newContext({
          baseURL: PRODUCTION_ORIGIN,
          viewport: { width: 1920, height: 1200 },
        }),
      );
    const holder = await openTuningSession(contexts[0]!, 1, errors);
    const peer = await openTuningSession(contexts[1]!, 1, errors);
    const other = await openTuningSession(contexts[2]!, 2, errors);
    await startMatchFromLobby(holder.page);
    await expect(matchHudCell(holder.page, 'Phase')).toHaveText('running');
    await expect(matchHudCell(peer.page, 'Phase')).toHaveText('running');

    // A peer commit must preserve these unsubmitted edits and require explicit review.
    await tuningPanel(holder)
      .getByRole('spinbutton', { name: 'Acceleration (wu/s²)', exact: true })
      .fill('650');
    await tuningPanel(peer)
      .getByRole('spinbutton', { name: 'Acceleration (wu/s²)', exact: true })
      .fill('800');
    await tuningPanel(peer)
      .getByRole('spinbutton', { name: 'Normal top speed (wu/s)', exact: true })
      .fill('9000');
    expect(recordedCommands(holder.traffic)).toEqual([
      { kind: 'start_match', payload: {} },
    ]);
    expect(recordedCommands(peer.traffic)).toEqual([]);

    await holder.page.bringToFront();
    await focusSimulationCanvas(holder.page);
    await aimFromPaintedBody(
      holder.page,
      holder.displayName,
      { x: 60, y: 0 },
      20,
    );
    await holder.page.keyboard.down('Space');
    await expect
      .poll(() => {
        const snapshot = latestSnapshot(holder);
        return snapshot === undefined
          ? undefined
          : entityForController(snapshot, holder.controllerId)?.components
              .physics_body?.acceleration;
      })
      .toEqual({ x: 400, y: 0 });
    const heldCommands = recordedCommands(holder.traffic);
    expect(heldCommands.at(-1)).toMatchObject({
      kind: 'set_thrust',
      payload: { x: 1, y: 0 },
    });
    const baseline = latestSnapshot(holder)!;
    const generation = entityForController(baseline, holder.controllerId)
      ?.components.controllable?.input_generation;
    const otherBefore = latestSnapshot(other)!;

    // Peer input runs in another real context. Any resulting holder blur/cancel is a failure,
    // because this case specifically owes adoption by an already-held activation.
    await tuningPanel(peer)
      .getByRole('button', { name: 'Apply movement tuning', exact: true })
      .click();
    await expect(tuningPanel(peer).getByRole('status')).toHaveText(
      /^Request 1 applied at revision 1, tick [1-9][0-9]*\./,
    );
    await expect
      .poll(() => latestSnapshot(holder)?.match.movement.revision)
      .toBe(1);
    const committed = latestSnapshot(peer)!.match.movement;
    expect(committed.current).toEqual({
      acceleration_world_units_per_second_squared: 800,
      normal_top_speed_world_units_per_second: 9000,
    });
    expect(committed.effective_tick).toBeGreaterThan(baseline.tick_sequence);
    await expect
      .poll(() => latestSnapshot(holder)?.match.movement)
      .toEqual(committed);
    await expect
      .poll(() =>
        recordedSnapshots(holder.traffic).some((snapshot) => {
          const entity = entityForController(snapshot, holder.controllerId);
          return (
            snapshot.tick_sequence >= committed.effective_tick &&
            snapshot.match.phase === 'running' &&
            entity?.components.physics_body?.acceleration.x === 800 &&
            entity.components.physics_body.acceleration.y === 0 &&
            entity.components.controllable?.input_generation === generation
          );
        }),
      )
      .toBe(true);
    expect(recordedCommands(holder.traffic)).toEqual(heldCommands);

    // Moving the cursor farther along the same ray still does not alter command magnitude.
    await aimFromPaintedBody(
      holder.page,
      holder.displayName,
      { x: 160, y: 0 },
      20,
    );
    const beforeDistance = latestSnapshot(holder)!.tick_sequence;
    await expect
      .poll(() => latestSnapshot(holder)?.tick_sequence ?? 0)
      .toBeGreaterThan(beforeDistance + 40);
    expect(recordedCommands(holder.traffic)).toEqual(heldCommands);
    await holder.page.keyboard.up('Space');
    await expect
      .poll(() => recordedCommands(holder.traffic).at(-1))
      .toMatchObject({ kind: 'set_thrust', payload: { x: 0, y: 0 } });
    await expect
      .poll(() => {
        const snapshot = latestSnapshot(holder);
        return snapshot === undefined
          ? undefined
          : entityForController(snapshot, holder.controllerId)?.components
              .physics_body?.acceleration;
      })
      .toEqual({ x: 0, y: 0 });

    // Read a post-commit public directory barrier so pre-Apply buffered traffic cannot prove
    // isolation. The other room must subsequently publish at least that committed tick.
    const directoryResponse = await request.get('/api/v3/lobbies', {
      headers: { Origin: PRODUCTION_ORIGIN, Accept: 'application/json' },
    });
    expect(directoryResponse.status()).toBe(200);
    const directory =
      (await directoryResponse.json()) as SessionLobbyDirectoryMessage;
    const otherBarrier = directory.data.lobbies.find(
      (room) => room.lobby_id === 2,
    );
    expect(otherBarrier).toBeDefined();
    await expect
      .poll(() => latestSnapshot(other)?.tick_sequence ?? 0)
      .toBeGreaterThanOrEqual(otherBarrier!.tick_sequence);
    expect(latestSnapshot(other)!.match.movement).toEqual(
      otherBefore.match.movement,
    );
    expect(recordedCommands(other.traffic)).toEqual([]);
    await expect(
      tuningPanel(holder).getByRole('spinbutton', {
        name: 'Acceleration (wu/s²)',
        exact: true,
      }),
    ).toHaveValue('650');
    await expect(
      tuningPanel(holder).getByRole('button', {
        name: 'Apply movement tuning',
        exact: true,
      }),
    ).toBeDisabled();
    await expect(
      tuningPanel(holder).getByRole('button', {
        name: 'Review current values; keep draft',
        exact: true,
      }),
    ).toBeEnabled();
    expect(recordedCommands(peer.traffic)).toEqual([
      {
        kind: 'set_movement_tuning',
        payload: {
          tuning_request_id: 1,
          expected_revision: 0,
          acceleration_world_units_per_second_squared: 800,
          normal_top_speed_world_units_per_second: 9000,
        },
      },
    ]);
    // The high ceiling is synchronized here; drag40 keeps this case below either ceiling.
    await server.terminateWithSigterm();
  } finally {
    for (const context of contexts) await context.close();
    await server.cleanup();
  }
  expect(errors).toEqual([]);
});
