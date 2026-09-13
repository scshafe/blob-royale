import {
  expect,
  test,
  type APIRequestContext,
  type Browser,
  type BrowserContext,
  type Page,
} from '@playwright/test';

import type {
  SessionEntitySnapshot,
  SessionPhysicsBodyComponent,
  SessionShieldComponent,
  SessionWorldSnapshot,
} from '../src/features/simulation/simulationProtocolTypes';
import {
  SHIELD_PERFECT_RING_COLOR,
  SHIELD_PROTECTION_RING_COLOR,
} from '../src/features/simulation/rendering/shieldRenderer';
import { STUN_ARC_COLOR } from '../src/features/simulation/rendering/stunRenderer';
import { BlobRoyaleServerProcess } from './BlobRoyaleServerProcess';
import { BrowserE2EError } from './BrowserE2EError';
import {
  CONNECTED_STATUS,
  PRODUCTION_ORIGIN,
  aimFromPaintedBody,
  connectionStatus,
  entityForController,
  findLabel,
  focusSimulationCanvas,
  installCanvasRecorder,
  matchHudCell,
  readCanvasFrame,
  recordedCommands,
  recordedSnapshots,
  recordedWelcome,
  recordSessionTraffic,
  requirePaintedBody,
  startMatchFromLobby,
  waitForReadyServer,
  type RecordedSessionTraffic,
} from './browserFlowSupport';

/** Fixture timings are observation margins, not shipped balance or input-latency claims. */
const COMBAT = Object.freeze({
  bodyRadius: 20,
  burstSpeed: 150,
  quarterSpeed: 37.5,
  shieldTicks: 3200,
  perfectTicks: 1200,
  cooldownTicks: 4000,
  stunTicks: 800,
  chargeActiveTicks: 480,
  chargeHitStunTicks: 800,
  chargeCooldownTicks: 480,
  observationTimeout: 25_000,
  viewport: Object.freeze({ width: 1920, height: 1200 }),
  aimOffset: 260,
  pairSpawns: Object.freeze([600, 740]),
  ordinaryLaunchAge: 1400,
  lateLaunchAge: 2400,
  postStunIdleTicks: 160,
  hazardRadius: 30,
  hazardSpawn: Object.freeze({ x: 1289.199716891, y: 716.760606588 }),
});

interface CombatSession {
  readonly page: Page;
  readonly traffic: RecordedSessionTraffic;
  readonly controllerId: number;
  readonly displayName: string;
}

/** Every input below is delivered to the production UI; this owns only process/session cleanup. */
async function withCombatSessions(
  browser: Browser,
  request: APIRequestContext,
  fixtureName: string,
  sessionCount: number,
  run: (sessions: readonly CombatSession[]) => Promise<void>,
): Promise<void> {
  const server = await BlobRoyaleServerProcess.createFromEnvironment({
    configurationFileName: `blob-royale-browser-e2e-combat-${fixtureName}.cfg`,
    scenarioFileName: null,
  });
  const contexts: BrowserContext[] = [];
  const errors: string[] = [];
  try {
    await server.start();
    await waitForReadyServer(request, server);
    const sessions: CombatSession[] = [];
    for (let index = 0; index < sessionCount; index += 1) {
      const context = await browser.newContext({
        baseURL: PRODUCTION_ORIGIN,
        viewport: COMBAT.viewport,
      });
      contexts.push(context);
      const page = await context.newPage();
      page.on('pageerror', (error) => errors.push(error.message));
      const traffic = recordSessionTraffic(page);
      await installCanvasRecorder(page);
      const response = await page.goto('/?lobby=1', {
        waitUntil: 'domcontentloaded',
      });
      expect(response?.status()).toBe(200);
      await expect(connectionStatus(page)).toHaveText(CONNECTED_STATUS);
      await expect(matchHudCell(page, 'Phase')).toHaveText('lobby');
      const welcome = recordedWelcome(traffic);
      if (welcome === undefined) {
        throw new BrowserE2EError(
          'BROWSER_E2E.COMBAT_WELCOME_MISSING',
          'A connected combat session must retain its real welcome identity.',
        );
      }
      sessions.push({
        page,
        traffic,
        controllerId: welcome.data.controller_id,
        displayName: welcome.data.display_name,
      });
    }
    await run(sessions);
    for (const session of sessions) {
      await expect(connectionStatus(session.page)).toHaveText(CONNECTED_STATUS);
    }
    expect(errors).toEqual([]);
    await server.terminateWithSigterm();
  } finally {
    for (const context of contexts) await context.close();
    await server.cleanup();
  }
}

function requireSession(
  sessions: readonly CombatSession[],
  index: number,
): CombatSession {
  const session = sessions[index];
  if (session === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.COMBAT_SESSION_MISSING',
      'The fixture must open its declared real sessions.',
      { index },
    );
  }
  return session;
}

function requireEntity(
  snapshot: SessionWorldSnapshot,
  controllerId: number,
): SessionEntitySnapshot {
  const entity = entityForController(snapshot, controllerId);
  if (entity === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.COMBAT_ENTITY_MISSING',
      'The isolated combat observation must retain the requested participant.',
      { controller_id: controllerId, tick_sequence: snapshot.tick_sequence },
    );
  }
  return entity;
}

function requireBody(
  entity: SessionEntitySnapshot,
): SessionPhysicsBodyComponent {
  const body = entity.components.physics_body;
  if (body === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.COMBAT_BODY_MISSING',
      'This combat observation requires a live body.',
      { entity_id: entity.entity_id },
    );
  }
  return body;
}

async function waitSnapshot(
  session: CombatSession,
  predicate: (snapshot: SessionWorldSnapshot) => boolean,
  message: string,
): Promise<SessionWorldSnapshot> {
  await expect
    .poll(() => recordedSnapshots(session.traffic).some(predicate), {
      timeout: COMBAT.observationTimeout,
      message,
    })
    .toBe(true);
  const snapshot = recordedSnapshots(session.traffic).find(predicate);
  if (snapshot === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.COMBAT_OBSERVATION_LOST',
      'A completed transport observation must remain in the recorder.',
      { observation: message },
    );
  }
  return snapshot;
}

async function startCombat(
  session: CombatSession,
): Promise<SessionWorldSnapshot> {
  await startMatchFromLobby(session.page);
  return waitSnapshot(
    session,
    (snapshot) => snapshot.match.phase === 'running',
    'the lobby Start control must produce a running match',
  );
}

async function aimCharge(
  session: CombatSession,
  direction: number,
): Promise<void> {
  await session.page.bringToFront();
  await focusSimulationCanvas(session.page);
  await aimFromPaintedBody(
    session.page,
    session.displayName,
    { x: COMBAT.aimOffset * direction, y: 0 },
    COMBAT.bodyRadius,
  );
}

async function expectParticipantRing(
  session: CombatSession,
  displayName: string,
  color: string,
  count: number,
): Promise<void> {
  await expect
    .poll(
      async () => {
        const frame = await readCanvasFrame(session.page);
        if (frame === null || findLabel(frame, displayName) === null)
          return null;
        const body = requirePaintedBody(frame, displayName, COMBAT.bodyRadius);
        return frame.strokedArcs.filter(
          (arc) =>
            arc.strokeStyle === color &&
            Math.hypot(arc.x - body.x, arc.y - body.y) < 0.01,
        ).length;
      },
      {
        message: `the production canvas must paint ${count} ${color} arcs on ${displayName}`,
      },
    )
    .toBe(count);
}

async function activateShield(
  session: CombatSession,
): Promise<SessionShieldComponent> {
  const button = session.page.getByRole('button', {
    name: 'Shield',
    exact: true,
  });
  await expect(button).toHaveAttribute('aria-disabled', 'false');
  await button.click();
  const frame = await waitSnapshot(
    session,
    (snapshot) =>
      entityForController(snapshot, session.controllerId)?.components.shield !==
      undefined,
    'the UI shield pulse must commit a published Shield',
  );
  const shield = requireEntity(frame, session.controllerId).components.shield;
  if (shield === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.COMBAT_SHIELD_MISSING',
      'The observed shield activation must retain its captured windows.',
    );
  }
  expect(shield.perfect_expiry_tick - shield.activation_tick).toBe(
    COMBAT.perfectTicks,
  );
  expect(shield.shield_expiry_tick - shield.activation_tick).toBe(
    COMBAT.shieldTicks,
  );
  expect(shield.cooldown_expiry_tick - shield.activation_tick).toBe(
    COMBAT.cooldownTicks,
  );
  expect(
    recordedCommands(session.traffic).filter(
      (command) => command.kind === 'shield',
    ),
  ).toHaveLength(1);
  return shield;
}

async function pairSessions(sessions: readonly CombatSession[]) {
  const first = requireSession(sessions, 0);
  const second = requireSession(sessions, 1);
  const lobby = await waitSnapshot(
    first,
    (snapshot) =>
      sessions.every(
        (session) =>
          entityForController(snapshot, session.controllerId)?.components
            .physics_body !== undefined,
      ),
    'both real sessions must occupy the authored combat spawns',
  );
  const ordered = [...sessions].sort(
    (left, right) =>
      requireBody(requireEntity(lobby, left.controllerId)).position.x -
      requireBody(requireEntity(lobby, right.controllerId)).position.x,
  );
  expect(
    ordered.map(
      (session) =>
        requireBody(requireEntity(lobby, session.controllerId)).position.x,
    ),
  ).toEqual(COMBAT.pairSpawns);
  for (const session of [first, second]) {
    const body = requireBody(requireEntity(lobby, session.controllerId));
    expect(body.position.y).toBe(640);
    expect(body.velocity).toEqual({ x: 0, y: 0 });
    expect(body.acceleration).toEqual({ x: 0, y: 0 });
    expect(body.mass).toBe(1);
  }
  const attacker = requireSession(ordered, 0);
  const defender = requireSession(ordered, 1);
  await startCombat(attacker);
  return { attacker, defender };
}

test('an unshielded charge hit refunds immediately and its stunned target keeps moving', async ({
  browser,
  request,
}) => {
  test.setTimeout(75_000);
  await withCombatSessions(browser, request, 'pair', 2, async (sessions) => {
    const { attacker, defender } = await pairSessions(sessions);
    await aimCharge(attacker, 1);
    await attacker.page.keyboard.press('KeyD');
    const launched = await waitSnapshot(
      attacker,
      (snapshot) =>
        entityForController(snapshot, attacker.controllerId)?.components
          .charge !== undefined,
      'the actual charge key must publish the active hit attempt before contact',
    );
    const charge = requireEntity(launched, attacker.controllerId).components
      .charge!;
    expect(charge.active_expiry_tick - charge.activation_tick).toBe(
      COMBAT.chargeActiveTicks,
    );
    expect(charge.hit_stun_duration_ticks).toBe(COMBAT.chargeHitStunTicks);
    expect(charge.cooldown_expiry_tick - charge.activation_tick).toBe(
      COMBAT.chargeCooldownTicks,
    );
    expect(
      requireBody(requireEntity(launched, attacker.controllerId)).velocity,
    ).toEqual({ x: COMBAT.burstSpeed, y: 0 });

    const hit = await waitSnapshot(
      attacker,
      (snapshot) =>
        entityForController(snapshot, defender.controllerId)?.components
          .stun !== undefined,
      'the unshielded player impact must commit a target stun',
    );
    const target = requireEntity(hit, defender.controllerId);
    const stun = target.components.stun!;
    expect(stun.activation_tick).toBeGreaterThan(charge.activation_tick);
    expect(stun.activation_tick).toBeLessThan(charge.active_expiry_tick);
    expect(stun.expiry_tick - stun.activation_tick).toBe(
      COMBAT.chargeHitStunTicks,
    );
    expect(target.components.controllable?.input_generation).toBe(
      stun.activation_tick,
    );
    expect(requireBody(target).velocity).toEqual({
      x: COMBAT.burstSpeed,
      y: 0,
    });
    expect(requireBody(target).acceleration).toEqual({ x: 0, y: 0 });
    expect(
      requireBody(requireEntity(hit, attacker.controllerId)).velocity,
    ).toEqual({ x: 0, y: 0 });
    expect(
      requireEntity(hit, attacker.controllerId).components.charge,
    ).toBeUndefined();
    expect(hit.tick_sequence).toBeLessThan(charge.cooldown_expiry_tick);

    // The consumed cooldown is absent in the same committed contact snapshot. A real second
    // activation must also arrive before that original cooldown could expire naturally.
    await aimCharge(attacker, -1);
    await expect(
      attacker.page.getByRole('button', { name: 'Charge', exact: true }),
    ).toHaveAttribute('aria-disabled', 'false');
    await attacker.page.keyboard.press('KeyD');
    const recharged = await waitSnapshot(
      attacker,
      (snapshot) =>
        (entityForController(snapshot, attacker.controllerId)?.components.charge
          ?.activation_tick ?? 0) > stun.activation_tick,
      'the refunded charge must accept another real key pulse immediately after the hit',
    );
    const secondCharge = requireEntity(recharged, attacker.controllerId)
      .components.charge!;
    expect(secondCharge.activation_tick).toBeLessThan(
      charge.cooldown_expiry_tick,
    );
    expect(
      requireBody(requireEntity(recharged, attacker.controllerId)).velocity,
    ).toEqual({ x: -COMBAT.burstSpeed, y: 0 });
    await expectParticipantRing(
      attacker,
      defender.displayName,
      STUN_ARC_COLOR,
      4,
    );
    const drifting = await waitSnapshot(
      attacker,
      (snapshot) =>
        snapshot.tick_sequence >=
          hit.tick_sequence + COMBAT.postStunIdleTicks &&
        entityForController(snapshot, defender.controllerId)?.components
          .stun !== undefined,
      'multiple published ticks must show the stunned target continuing to drift',
    );
    const driftTarget = requireEntity(drifting, defender.controllerId);
    expect(driftTarget.components.stun).toEqual(stun);
    expect(requireBody(driftTarget).position.x).toBeGreaterThan(
      requireBody(target).position.x,
    );
    expect(requireBody(driftTarget).velocity).toEqual({
      x: COMBAT.burstSpeed,
      y: 0,
    });
    expect(requireBody(driftTarget).acceleration).toEqual({ x: 0, y: 0 });
    const recovered = await waitSnapshot(
      attacker,
      (snapshot) => snapshot.tick_sequence >= stun.expiry_tick,
      'the target stun must end at its authoritative expiry while momentum remains',
    );
    expect(
      requireEntity(recovered, defender.controllerId).components.stun,
    ).toBeUndefined();
    expect(
      requireBody(requireEntity(recovered, defender.controllerId)).velocity,
    ).toEqual({ x: COMBAT.burstSpeed, y: 0 });
    expect(recordedCommands(defender.traffic)).toEqual([]);
    expect(
      recordedCommands(attacker.traffic).filter(
        (command) => command.kind === 'charge',
      ),
    ).toEqual([
      { kind: 'charge', payload: { x: 1, y: 0 } },
      { kind: 'charge', payload: { x: -1, y: 0 } },
    ]);
  });
});

test('Q and E turn active charge velocity, both turn buttons work and held Space brakes to rest', async ({
  browser,
  request,
}) => {
  test.setTimeout(75_000);
  await withCombatSessions(browser, request, 'pair', 2, async (sessions) => {
    const { attacker } = await pairSessions(sessions);
    // Charge away from the other player, so contact cannot supply the direction changes below.
    await aimCharge(attacker, -1);
    await attacker.page.keyboard.press('KeyD');
    const launched = await waitSnapshot(
      attacker,
      (snapshot) =>
        entityForController(snapshot, attacker.controllerId)?.components
          .charge !== undefined,
      'a real charge must first publish leftward velocity',
    );
    const charge = requireEntity(launched, attacker.controllerId).components
      .charge!;
    expect(
      requireBody(requireEntity(launched, attacker.controllerId)).velocity,
    ).toEqual({ x: -COMBAT.burstSpeed, y: 0 });
    let previousTick = launched.tick_sequence;
    for (const turn of [
      {
        key: 'KeyQ',
        direction: 'left',
        velocity: { x: 0, y: COMBAT.burstSpeed },
      },
      {
        key: 'KeyE',
        direction: 'right',
        velocity: { x: -COMBAT.burstSpeed, y: 0 },
      },
    ] as const) {
      await attacker.page.keyboard.press(turn.key);
      const turned = await waitSnapshot(
        attacker,
        (snapshot) => {
          const velocity = entityForController(snapshot, attacker.controllerId)
            ?.components.physics_body?.velocity;
          return (
            snapshot.tick_sequence > previousTick &&
            velocity?.x === turn.velocity.x &&
            velocity.y === turn.velocity.y
          );
        },
        `${turn.key} must commit an exact quarter turn during the active charge`,
      );
      expect(turned.tick_sequence).toBeLessThan(charge.active_expiry_tick);
      expect(
        requireEntity(turned, attacker.controllerId).components.charge,
      ).toEqual(charge);
      expect(
        Math.hypot(
          requireBody(requireEntity(turned, attacker.controllerId)).velocity.x,
          requireBody(requireEntity(turned, attacker.controllerId)).velocity.y,
        ),
      ).toBe(COMBAT.burstSpeed);
      previousTick = turned.tick_sequence;
    }
    for (const turn of [
      { direction: 'left', velocity: { x: 0, y: COMBAT.burstSpeed } },
      { direction: 'right', velocity: { x: -COMBAT.burstSpeed, y: 0 } },
    ] as const) {
      await attacker.page
        .getByRole('button', { name: `Rotate ${turn.direction}`, exact: true })
        .click();
      const turned = await waitSnapshot(
        attacker,
        (snapshot) => {
          const velocity = entityForController(snapshot, attacker.controllerId)
            ?.components.physics_body?.velocity;
          return (
            snapshot.tick_sequence > previousTick &&
            velocity?.x === turn.velocity.x &&
            velocity.y === turn.velocity.y
          );
        },
        `the Rotate ${turn.direction} button must commit the same exact quarter turn`,
      );
      previousTick = turned.tick_sequence;
    }
    expect(
      recordedCommands(attacker.traffic).filter(
        (command) => command.kind === 'rotate_velocity',
      ),
    ).toEqual([
      { kind: 'rotate_velocity', payload: { direction: 'left' } },
      { kind: 'rotate_velocity', payload: { direction: 'right' } },
      { kind: 'rotate_velocity', payload: { direction: 'left' } },
      { kind: 'rotate_velocity', payload: { direction: 'right' } },
    ]);
    await focusSimulationCanvas(attacker.page);
    await attacker.page.keyboard.down('Space');
    await expect
      .poll(() => recordedCommands(attacker.traffic).at(-1))
      .toEqual({
        kind: 'set_thrust',
        payload: { x: 0, y: 0, braking: true },
      });
    // Drag is zero in this fixture: coasting cannot reach rest. Braking 150 wu/s takes only
    // 25 simulation ticks, so require the exact stopped result without assuming that the 20 Hz
    // transport publishes an intermediate slowdown before it coalesces the latest world.
    const stopped = await waitSnapshot(
      attacker,
      (snapshot) =>
        snapshot.tick_sequence > previousTick &&
        entityForController(snapshot, attacker.controllerId)?.components
          .physics_body?.velocity.x === 0,
      'held Space must stop existing charge momentum exactly without propulsion',
    );
    const heldAtRest = await waitSnapshot(
      attacker,
      (snapshot) =>
        snapshot.tick_sequence >=
        stopped.tick_sequence + COMBAT.postStunIdleTicks,
      'continuing to hold brakes must preserve rest across later publications',
    );
    for (const snapshot of recordedSnapshots(attacker.traffic).filter(
      (entry) =>
        entry.tick_sequence > previousTick &&
        entry.tick_sequence <= heldAtRest.tick_sequence,
    )) {
      const body = requireBody(requireEntity(snapshot, attacker.controllerId));
      expect(body.velocity.x).toBeLessThanOrEqual(0);
      expect(body.velocity.y).toBe(0);
      expect(body.acceleration).toEqual({ x: 0, y: 0 });
      if (snapshot.tick_sequence >= stopped.tick_sequence) {
        expect(body.velocity).toEqual({ x: 0, y: 0 });
        expect(body.position).toEqual(
          requireBody(requireEntity(stopped, attacker.controllerId)).position,
        );
      }
    }
    await attacker.page.keyboard.up('Space');
    await expect
      .poll(() => recordedCommands(attacker.traffic).at(-1))
      .toEqual({
        kind: 'set_thrust',
        payload: { x: 0, y: 0, braking: false },
      });
  });
});

test('perfect shield stops a real charging player and held left mouse stays cancelled through stun', async ({
  browser,
  request,
}) => {
  test.setTimeout(75_000);
  await withCombatSessions(browser, request, 'pair', 2, async (sessions) => {
    const { attacker, defender } = await pairSessions(sessions);
    const shield = await activateShield(defender);
    await expectParticipantRing(
      defender,
      defender.displayName,
      SHIELD_PERFECT_RING_COLOR,
      2,
    );
    await aimCharge(attacker, 1);
    await attacker.page.mouse.down({ button: 'left' });
    const propelling = await waitSnapshot(
      attacker,
      (snapshot) =>
        snapshot.tick_sequence > shield.activation_tick &&
        (entityForController(snapshot, attacker.controllerId)?.components
          .physics_body?.acceleration.x ?? 0) > 0,
      'held left mouse must commit positive propulsion before the incoming charge is parried',
    );
    expect(
      requireBody(requireEntity(propelling, attacker.controllerId)).position.x,
    ).toBeLessThan(700);
    expect(recordedCommands(attacker.traffic)).toContainEqual({
      kind: 'set_thrust',
      payload: { x: 1, y: 0 },
    });
    await attacker.page.keyboard.press('KeyD');
    const stunnedFrame = await waitSnapshot(
      attacker,
      (snapshot) =>
        entityForController(snapshot, attacker.controllerId)?.components
          .stun !== undefined,
      'a real held-thrust charge must be parried by the peer shield',
    );
    const stunned = requireEntity(stunnedFrame, attacker.controllerId);
    const stun = stunned.components.stun;
    if (stun === undefined)
      throw new BrowserE2EError(
        'BROWSER_E2E.COMBAT_STUN_MISSING',
        'The parry observation must contain its committed stun.',
      );
    expect(stun.activation_tick).toBeGreaterThan(shield.activation_tick);
    expect(stun.activation_tick).toBeLessThan(shield.perfect_expiry_tick);
    expect(stun.expiry_tick - stun.activation_tick).toBe(COMBAT.stunTicks);
    expect(stunned.components.controllable?.input_generation).toBe(
      stun.activation_tick,
    );
    expect(stunned.components.charge?.activation_tick).toBeLessThanOrEqual(
      stun.activation_tick,
    );
    expect(requireBody(stunned).velocity).toEqual({ x: 0, y: 0 });
    expect(requireBody(stunned).acceleration).toEqual({ x: 0, y: 0 });
    await expectParticipantRing(
      attacker,
      attacker.displayName,
      STUN_ARC_COLOR,
      4,
    );
    const commandsAtStun = attacker.traffic.sentFrames.length;
    const idleFrame = await waitSnapshot(
      attacker,
      (snapshot) =>
        snapshot.tick_sequence >= stun.expiry_tick + COMBAT.postStunIdleTicks,
      'published ticks must pass stun expiry while the original left mouse button remains physically held',
    );
    for (const snapshot of recordedSnapshots(attacker.traffic).filter(
      (value) =>
        value.tick_sequence >= stunnedFrame.tick_sequence &&
        value.tick_sequence <= idleFrame.tick_sequence,
    )) {
      const entity = requireEntity(snapshot, attacker.controllerId);
      expect(requireBody(entity).velocity).toEqual({ x: 0, y: 0 });
      expect(requireBody(entity).acceleration).toEqual({ x: 0, y: 0 });
      expect(requireBody(entity).position).toEqual(
        requireBody(stunned).position,
      );
      expect(entity.components.controllable?.input_generation).toBe(
        stun.activation_tick,
      );
    }
    expect(
      requireEntity(idleFrame, attacker.controllerId).components.stun,
    ).toBeUndefined();
    await expect(matchHudCell(attacker.page, 'Thrust')).toHaveText('idle');
    // Reentry with the same physical mouse hold must not become a fresh activation.
    const repeatTick = recordedSnapshots(attacker.traffic).at(
      -1,
    )!.tick_sequence;
    await aimCharge(attacker, 1);
    await waitSnapshot(
      attacker,
      (snapshot) => snapshot.tick_sequence >= repeatTick + 40,
      'the existing mouse hold must remain idle across another publication interval',
    );
    for (const command of recordedCommands(attacker.traffic).slice(
      commandsAtStun,
    )) {
      if (command.kind === 'set_thrust')
        expect(command.payload).toMatchObject({ x: 0, y: 0 });
    }
    const beforeFresh = attacker.traffic.sentFrames.length;
    await attacker.page.mouse.up({ button: 'left' });
    await attacker.page.mouse.down({ button: 'left' });
    const resumed = await waitSnapshot(
      attacker,
      (snapshot) =>
        snapshot.tick_sequence > idleFrame.tick_sequence &&
        (entityForController(snapshot, attacker.controllerId)?.components
          .physics_body?.acceleration.x ?? 0) > 0,
      'a fresh left-button press must resume real propulsion with the new generation',
    );
    expect(
      requireEntity(resumed, attacker.controllerId).components.controllable
        ?.input_generation,
    ).toBe(stun.activation_tick);
    expect(
      recordedCommands(attacker.traffic).slice(beforeFresh),
    ).toContainEqual({
      kind: 'set_thrust',
      payload: { x: 1, y: 0, input_generation: stun.activation_tick },
    });
    await attacker.page.mouse.up({ button: 'left' });
  });
});

for (const timing of [
  { name: 'ordinary', launchAge: COMBAT.ordinaryLaunchAge },
  { name: 'late protected', launchAge: COMBAT.lateLaunchAge },
] as const) {
  test(`${timing.name} shield preserves exactly quarter knockback without a parry`, async ({
    browser,
    request,
  }) => {
    test.setTimeout(75_000);
    await withCombatSessions(browser, request, 'pair', 2, async (sessions) => {
      const { attacker, defender } = await pairSessions(sessions);
      const shield = await activateShield(defender);
      await aimCharge(attacker, 1);
      await waitSnapshot(
        attacker,
        (snapshot) =>
          snapshot.tick_sequence >= shield.activation_tick + timing.launchAge,
        'launch age must be established by a published shield-relative tick',
      );
      await expectParticipantRing(
        attacker,
        defender.displayName,
        SHIELD_PROTECTION_RING_COLOR,
        1,
      );
      await attacker.page.keyboard.press('KeyD');
      const charged = await waitSnapshot(
        attacker,
        (snapshot) =>
          entityForController(snapshot, attacker.controllerId)?.components
            .charge !== undefined,
        'the real charge key must commit its one-shot burst',
      );
      expect(
        requireBody(requireEntity(charged, attacker.controllerId)).velocity,
      ).toEqual({ x: COMBAT.burstSpeed, y: 0 });
      const after = await waitSnapshot(
        attacker,
        (snapshot) =>
          (entityForController(snapshot, defender.controllerId)?.components
            .physics_body?.velocity.x ?? 0) > 0,
        'the shielded peer must receive committed physical knockback',
      );
      const before = recordedSnapshots(attacker.traffic)
        .filter((snapshot) => snapshot.tick_sequence < after.tick_sequence)
        .at(-1);
      if (before === undefined)
        throw new BrowserE2EError(
          'BROWSER_E2E.COMBAT_CONTACT_BRACKET_MISSING',
          'Quarter knockback needs the retained publication immediately before contact.',
        );
      // Both ends lie in the ordinary window, so no hidden contact tick can be perfect or expired.
      expect(before.tick_sequence).toBeGreaterThanOrEqual(
        shield.perfect_expiry_tick,
      );
      expect(before.tick_sequence).toBeGreaterThanOrEqual(
        shield.activation_tick + timing.launchAge,
      );
      expect(after.tick_sequence).toBeLessThan(shield.shield_expiry_tick);
      expect(
        requireBody(requireEntity(before, defender.controllerId)).velocity,
      ).toEqual({ x: 0, y: 0 });
      expect(
        requireBody(requireEntity(after, attacker.controllerId)).velocity,
      ).toEqual({ x: 0, y: 0 });
      expect(
        requireBody(requireEntity(after, defender.controllerId)).velocity,
      ).toEqual({ x: COMBAT.quarterSpeed, y: 0 });
      for (const snapshot of recordedSnapshots(attacker.traffic).filter(
        (value) =>
          value.tick_sequence >= charged.tick_sequence &&
          value.tick_sequence <= after.tick_sequence,
      )) {
        for (const session of [attacker, defender])
          expect(
            requireEntity(snapshot, session.controllerId).components.stun,
          ).toBeUndefined();
      }
      expect(
        recordedCommands(attacker.traffic).filter(
          (command) => command.kind === 'set_thrust',
        ),
      ).toEqual([]);
      expect(
        recordedCommands(attacker.traffic).filter(
          (command) => command.kind === 'charge',
        ),
      ).toEqual([{ kind: 'charge', payload: { x: 1, y: 0 } }]);
    });
  });
}

test('a bounceable hazard is parried into a published stun while its original lifetime continues', async ({
  browser,
  request,
}) => {
  test.setTimeout(75_000);
  await withCombatSessions(browser, request, 'hazard', 2, async (sessions) => {
    const first = requireSession(sessions, 0);
    const lobby = await waitSnapshot(
      first,
      (snapshot) =>
        sessions.every(
          (session) =>
            entityForController(snapshot, session.controllerId)?.components
              .physics_body !== undefined,
        ),
      'hazard witnesses need both authored player spawns',
    );
    const struck = sessions.find((session) => {
      const position = requireBody(
        requireEntity(lobby, session.controllerId),
      ).position;
      return (
        Math.hypot(
          position.x - COMBAT.hazardSpawn.x,
          position.y - COMBAT.hazardSpawn.y,
        ) < 0.01
      );
    });
    const clear = sessions.find((session) => session !== struck);
    if (struck === undefined || clear === undefined)
      throw new BrowserE2EError(
        'BROWSER_E2E.COMBAT_HAZARD_SEATING_INVALID',
        'The seeded crossing must identify one struck and one clear browser.',
      );
    const clearBody = requireBody(requireEntity(lobby, clear.controllerId));
    await startCombat(struck);
    await struck.page.bringToFront();
    await focusSimulationCanvas(struck.page);
    const incoming = await waitSnapshot(
      struck,
      (snapshot) =>
        snapshot.entities.some(
          (entity) =>
            entity.components.lifetime !== undefined &&
            entity.components.physics_body?.radius === COMBAT.hazardRadius,
        ),
      'the first seeded hazard must enter the real published world',
    );
    const hazard = incoming.entities.find(
      (entity) =>
        entity.components.lifetime !== undefined &&
        entity.components.physics_body?.radius === COMBAT.hazardRadius,
    );
    if (hazard?.components.lifetime === undefined)
      throw new BrowserE2EError(
        'BROWSER_E2E.COMBAT_HAZARD_MISSING',
        'The incoming hazard must retain its lifetime.',
      );
    expect(hazard.components.lethal_on_contact).toBeUndefined();
    expect(hazard.components.controllable).toBeUndefined();
    expect(
      Math.hypot(
        requireBody(hazard).velocity.x,
        requireBody(hazard).velocity.y,
      ),
    ).toBeCloseTo(600, 8);
    const lifetimeEnd =
      incoming.tick_sequence + hazard.components.lifetime.ticks_remaining;
    const shield = await activateShield(struck);
    const parried = await waitSnapshot(
      struck,
      (snapshot) =>
        snapshot.entities.find(
          (entity) => entity.entity_id === hazard.entity_id,
        )?.components.stun !== undefined,
      'the actual incoming hazard must receive a committed parry stun',
    );
    const stopped = parried.entities.find(
      (entity) => entity.entity_id === hazard.entity_id,
    );
    const stun = stopped?.components.stun;
    if (stopped === undefined || stun === undefined)
      throw new BrowserE2EError(
        'BROWSER_E2E.COMBAT_HAZARD_STUN_MISSING',
        'The parried hazard must retain its body and stun.',
      );
    expect(stun.activation_tick).toBeGreaterThan(shield.activation_tick);
    expect(stun.activation_tick).toBeLessThan(shield.perfect_expiry_tick);
    expect(stun.expiry_tick - stun.activation_tick).toBe(COMBAT.stunTicks);
    expect(requireBody(stopped).velocity).toEqual({ x: 0, y: 0 });
    expect(requireBody(stopped).acceleration).toEqual({ x: 0, y: 0 });
    expect(
      parried.tick_sequence +
        (stopped.components.lifetime?.ticks_remaining ?? -1),
    ).toBe(lifetimeEnd);
    await expect
      .poll(
        async () => {
          const frame = await readCanvasFrame(struck.page);
          if (frame === null) return 0;
          const scale = frame.width / frame.cssWidth;
          return frame.strokedArcs.filter(
            (arc) =>
              arc.strokeStyle === STUN_ARC_COLOR &&
              frame.arcs.some(
                (body) =>
                  Math.abs(body.radius - COMBAT.hazardRadius * scale) < 0.01 &&
                  Math.hypot(body.x - arc.x, body.y - arc.y) < 0.01,
              ),
          ).length;
        },
        {
          message:
            'the production canvas must paint four broken stun arcs on the hazard body',
        },
      )
      .toBe(4);
    const expired = await waitSnapshot(
      struck,
      (snapshot) =>
        snapshot.tick_sequence >= lifetimeEnd &&
        !snapshot.entities.some(
          (entity) => entity.entity_id === hazard.entity_id,
        ),
      'the stopped hazard must expire at its original lifetime',
    );
    const retained = recordedSnapshots(struck.traffic).filter(
      (snapshot) =>
        snapshot.tick_sequence >= parried.tick_sequence &&
        snapshot.tick_sequence < lifetimeEnd,
    );
    expect(retained.length).toBeGreaterThan(1);
    for (const snapshot of retained) {
      const entity = snapshot.entities.find(
        (entry) => entry.entity_id === hazard.entity_id,
      );
      expect(entity).toBeDefined();
      if (entity === undefined) continue;
      expect(requireBody(entity).position).toEqual(
        requireBody(stopped).position,
      );
      expect(requireBody(entity).velocity).toEqual({ x: 0, y: 0 });
      expect(
        snapshot.tick_sequence +
          (entity.components.lifetime?.ticks_remaining ?? -1),
      ).toBe(lifetimeEnd);
      expect(requireBody(requireEntity(snapshot, clear.controllerId))).toEqual(
        clearBody,
      );
    }
    expect(expired.match.placements).toEqual([]);
  });
});

function botController(
  snapshot: SessionWorldSnapshot,
  profileName: string,
): number {
  const seat = snapshot.match.seats.find(
    (entry) =>
      entry.kind === 'npc' &&
      entry.npc_kind === 'tactical' &&
      entry.profile_name === profileName,
  );
  if (seat?.controller_id === null || seat?.controller_id === undefined)
    throw new BrowserE2EError(
      'BROWSER_E2E.COMBAT_BOT_SEAT_MISSING',
      'The authored tactical profile must resolve through its real published seat.',
      { profile_name: profileName },
    );
  return seat.controller_id;
}

function botLobbyReady(
  snapshot: SessionWorldSnapshot,
  human: CombatSession,
  profileName: string,
): boolean {
  const seat = snapshot.match.seats.find(
    (entry) => entry.kind === 'npc' && entry.profile_name === profileName,
  );
  return (
    snapshot.match.phase === 'lobby' &&
    seat?.controller_id !== undefined &&
    seat.controller_id !== null &&
    entityForController(snapshot, seat.controller_id)?.components
      .physics_body !== undefined &&
    entityForController(snapshot, human.controllerId)?.components
      .physics_body !== undefined
  );
}

test('at deployed drag a tactical charge transfers momentum and knocks its untouched target into a hole', async ({
  browser,
  request,
}) => {
  test.setTimeout(75_000);
  await withCombatSessions(
    browser,
    request,
    'bot-offense',
    1,
    async (sessions) => {
      const human = requireSession(sessions, 0);
      const lobby = await waitSnapshot(
        human,
        (snapshot) => botLobbyReady(snapshot, human, 'offense'),
        'the offensive bot and human must publish their authored bodies',
      );
      const botId = botController(lobby, 'offense');
      expect(requireBody(requireEntity(lobby, botId)).position).toEqual({
        x: 740,
        y: 640,
      });
      expect(
        requireBody(requireEntity(lobby, human.controllerId)).position,
      ).toEqual({ x: 900, y: 640 });
      await startCombat(human);
      const charged = await waitSnapshot(
        human,
        (snapshot) =>
          entityForController(snapshot, botId)?.components.charge !== undefined,
        'the hosted tactical controller must commit an actual Charge component',
      );
      const bot = requireEntity(charged, botId);
      const firstCharge = bot.components.charge!;
      expect(requireBody(bot).velocity.x).toBeGreaterThan(300);
      expect(requireBody(bot).velocity.y).toBe(0);
      expect(requireBody(bot).acceleration).toEqual({ x: 0, y: 0 });
      const pushed = await waitSnapshot(
        human,
        (snapshot) => {
          const target = entityForController(snapshot, human.controllerId)
            ?.components.physics_body;
          const attacker = entityForController(snapshot, botId)?.components
            .physics_body;
          return (
            target !== undefined &&
            attacker !== undefined &&
            target.velocity.x > 0 &&
            target.velocity.x > attacker.velocity.x
          );
        },
        'the untouched human must receive charge momentum and separate ahead of the bot',
      );
      const pushedTarget = requireEntity(pushed, human.controllerId);
      const pushedBot = requireEntity(pushed, botId);
      const targetBody = requireBody(pushedTarget);
      const botBody = requireBody(pushedBot);
      expect(targetBody.position.x).toBeGreaterThan(900);
      expect(targetBody.position.x).toBeLessThan(940);
      expect(targetBody.velocity.x).toBeGreaterThan(botBody.velocity.x);
      expect(botBody.velocity.x).toBeGreaterThanOrEqual(0);
      expect(targetBody.velocity.y).toBe(0);
      expect(botBody.velocity.y).toBe(0);
      expect(targetBody.acceleration).toEqual({ x: 0, y: 0 });
      expect(botBody.acceleration).toEqual({ x: 0, y: 0 });
      expect(pushedTarget.components.stun).toBeDefined();
      const stun = pushedTarget.components.stun!;
      expect(stun.activation_tick).toBeGreaterThan(firstCharge.activation_tick);
      expect(stun.activation_tick).toBeLessThan(firstCharge.active_expiry_tick);
      const latestHitTick =
        pushedTarget.components.controllable!.input_generation!;
      expect(latestHitTick).toBeGreaterThanOrEqual(stun.activation_tick);
      expect(latestHitTick).toBeLessThanOrEqual(pushed.tick_sequence);
      expect(stun.expiry_tick - latestHitTick).toBe(
        firstCharge.hit_stun_duration_ticks,
      );
      expect(pushed.tick_sequence).toBeLessThan(
        firstCharge.cooldown_expiry_tick,
      );
      // A successful hit immediately permits another tactical burst. Repeated equal-mass impacts
      // can leave the bot moving with the target's earlier momentum; the target still separates
      // ahead with zero propulsion. The original activation must already have been consumed.
      if (pushedBot.components.charge !== undefined) {
        expect(pushedBot.components.charge.activation_tick).toBeGreaterThan(
          firstCharge.activation_tick,
        );
        expect(pushedBot.components.charge.activation_tick).toBeLessThan(
          firstCharge.cooldown_expiry_tick,
        );
      }
      const fallen = await waitSnapshot(
        human,
        (snapshot) =>
          snapshot.match.placements.some(
            (placement) =>
              placement.controller_id === human.controllerId &&
              placement.placement === 2,
          ),
        'the received momentum must carry the real target into the authored hole',
      );
      expect(
        entityForController(fallen, human.controllerId)?.components
          .physics_body,
      ).toBeUndefined();
      expect(requireBody(requireEntity(fallen, botId)).position.x).toBeLessThan(
        940,
      );
      await expect(matchHudCell(human.page, 'Placement')).toHaveText('#2');
      expect(recordedCommands(human.traffic)).toEqual([
        { kind: 'start_match', payload: {} },
      ]);
    },
  );
});

test('at deployed drag a tactical closing prediction commits a shield that parries the real incoming charge', async ({
  browser,
  request,
}) => {
  test.setTimeout(75_000);
  await withCombatSessions(
    browser,
    request,
    'bot-defense',
    1,
    async (sessions) => {
      const human = requireSession(sessions, 0);
      const lobby = await waitSnapshot(
        human,
        (snapshot) => botLobbyReady(snapshot, human, 'defense'),
        'the defensive bot and human must publish their authored bodies',
      );
      const botId = botController(lobby, 'defense');
      expect(requireBody(requireEntity(lobby, botId)).position).toEqual({
        x: 760,
        y: 640,
      });
      expect(
        requireBody(requireEntity(lobby, human.controllerId)).position,
      ).toEqual({ x: 600, y: 640 });
      await startCombat(human);
      await aimCharge(human, 1);
      await human.page.keyboard.press('KeyD');
      const defended = await waitSnapshot(
        human,
        (snapshot) =>
          entityForController(snapshot, human.controllerId)?.components.stun !==
          undefined,
        'the bot closing predictor at drag2 must commit a shield that actually parries',
      );
      const attacker = requireEntity(defended, human.controllerId);
      const defender = requireEntity(defended, botId);
      const stun = attacker.components.stun;
      const shield = defender.components.shield;
      if (stun === undefined || shield === undefined)
        throw new BrowserE2EError(
          'BROWSER_E2E.COMBAT_BOT_DEFENSE_MISSING',
          'A real defensive bot parry must publish both authoritative windows.',
        );
      expect(stun.activation_tick).toBeGreaterThan(shield.activation_tick);
      expect(stun.activation_tick).toBeLessThan(shield.perfect_expiry_tick);
      expect(shield.perfect_expiry_tick - shield.activation_tick).toBe(
        COMBAT.perfectTicks,
      );
      expect(requireBody(attacker).velocity).toEqual({ x: 0, y: 0 });
      expect(requireBody(attacker).acceleration).toEqual({ x: 0, y: 0 });
      expect(requireBody(defender).velocity.x).toBeGreaterThan(0);
      expect(defender.components.charge).toBeUndefined();
      await expectParticipantRing(human, human.displayName, STUN_ARC_COLOR, 4);
      await expectParticipantRing(
        human,
        defender.components.controllable?.display_name ?? '',
        SHIELD_PERFECT_RING_COLOR,
        2,
      );
      expect(
        recordedCommands(human.traffic).filter(
          (command) =>
            command.kind === 'shield' || command.kind === 'set_thrust',
        ),
      ).toEqual([]);
      expect(
        recordedCommands(human.traffic).filter(
          (command) => command.kind === 'charge',
        ),
      ).toEqual([{ kind: 'charge', payload: { x: 1, y: 0 } }]);
    },
  );
});
