import { act, fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import {
  SimulationViewer as RoomViewer,
  type SimulationViewerProps,
} from './SimulationViewer';
import { useMovementTuning } from './useMovementTuning';
import { SimulationApiError } from './SimulationApiError';
import { useThrustInput } from './useThrustInput';
import type { SimulationAbility, ThrustAimObservation } from './useThrustInput';
import {
  aimPointer,
  installCanvasAimSurface,
} from './fixtures/canvasAimObservations';
import { cursorSteeringConnection } from './fixtures/cursorSteeringFrames';
import {
  QUICK_NPC_PROFILE,
  STEADY_NPC_PROFILE,
  tacticalNpcCatalogue,
  tacticalSeatCommand,
} from './fixtures/tacticalProfileFrames';
import {
  STUN_INPUT_GENERATION,
  STUN_INPUT_NEXT_GENERATION,
  STUN_INPUT_WINDOW,
  stunInputConnection,
} from './fixtures/stunInputFrames';
import {
  CANCELLED_SHIELD_WINDOWS,
  SHIELD_ACTIVATION_TICK,
  SHIELD_PARRY_STUN_DURATION_TICKS,
  SHIELD_WINDOWS,
} from './fixtures/shieldFrames';
import { CHARGE_COOLDOWN } from './fixtures/chargeFrames';
import {
  abilityAvailabilityReport,
  selectThrustInputOptions,
  type AbilityAvailability,
  type AbilityAvailabilityReport,
} from './sessionSelectors';
import {
  CHARGE_KEY_CODE,
  SHIELD_KEY_CODE,
  THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS,
} from './simulationConstants';
import type { SessionCommand } from './simulationProtocolTypes';
import {
  cameraConfiguration,
  cameraSessionIdentity,
  cameraSnapshot,
  type CameraSnapshotScenario,
} from './fixtures/simulationCameraFrames';
import type {
  SimulationConnection,
  SimulationSessionIdentity,
} from './useSimulationConnection';
import type { SessionEntitySnapshot } from './simulationProtocolTypes';
import { configurationResponseExample } from './fixtures/protocolV1Examples';
import { raceTerrain, solidTerrain } from './fixtures/terrainFrames';
import {
  legacyNpcCatalogue,
  hillSnapshotDocument,
  raceScenarioDocument,
  type RaceSnapshotScenario,
  snapshotDocument,
  welcomeDocument,
} from './fixtures/sessionFrames';
import { validateSessionSnapshotMessage } from './sessionProtocolValidation';
import { validateSimulationConfigurationResponse } from './simulationProtocolValidation';

const configuration = validateSimulationConfigurationResponse(
  structuredClone(configurationResponseExample),
  configurationResponseExample.meta.request_id,
).data;

const snapshot = validateSessionSnapshotMessage(snapshotDocument(), {
  messageSequence: 1,
  requestId: snapshotDocument().meta.request_id,
  tickSequence: null,
  npcCatalogue: legacyNpcCatalogue,
  terrain: solidTerrain,
});

const hillSnapshot = validateSessionSnapshotMessage(hillSnapshotDocument(), {
  messageSequence: 1,
  requestId: hillSnapshotDocument().meta.request_id,
  tickSequence: null,
  npcCatalogue: legacyNpcCatalogue,
  terrain: solidTerrain,
});

const session: SimulationSessionIdentity = Object.freeze({
  acceptedCommandKinds: ['set_thrust'] as const,
  controllerId: 3,
  displayName: 'Cole Shaffer',
  firstEntityId: 7,
  lobbyId: 1,
  map: 'arena-960x640',
  mode: 'royale',
  movementTuningMinimumIntervalMilliseconds:
    welcomeDocument().data.movement_tuning_minimum_interval_milliseconds,
  npcCatalogue: legacyNpcCatalogue,
  seatCountMaximum: 32,
  terrain: solidTerrain,
});

const zeroThrust = Object.freeze({ x: 0, y: 0 });

/**
 * What the many readout cases get: two controls that are present, explained, and not pressable. The
 * cases that press one compose the real availability instead, exactly as the feature does.
 */
const UNADVERTISED_ABILITIES: AbilityAvailabilityReport = Object.freeze({
  charge: unadvertisedAbility('charge'),
  shield: unadvertisedAbility('shield'),
});

function unadvertisedAbility(kind: SimulationAbility): AbilityAvailability {
  return {
    kind,
    canAttempt: false,
    reason: 'not_advertised',
    explanation: `This room does not accept the ${kind} command.`,
  };
}

/** Mirror the feature's always-mounted controls owner while exercising the view in isolation. */
function SimulationViewer(
  props: Omit<
    SimulationViewerProps,
    | 'abilityControls'
    | 'movementTuning'
    | 'onActivateAbility'
    | 'onAimObservation'
  > &
    Partial<
      Pick<
        SimulationViewerProps,
        'abilityControls' | 'onActivateAbility' | 'onAimObservation'
      >
    >,
) {
  const movementTuning = useMovementTuning({
    lobbyId: props.lobbyId,
    connection: props.connection,
  });
  return (
    <RoomViewer
      abilityControls={UNADVERTISED_ABILITIES}
      onActivateAbility={ignoreAbilityActivation}
      onAimObservation={ignoreAimObservation}
      {...props}
      movementTuning={movementTuning}
    />
  );
}

function ignoreAimObservation(observation: ThrustAimObservation | null): void {
  void observation;
}

function ignoreAbilityActivation(ability: SimulationAbility): void {
  void ability;
}

/** Same composition as Feature: actual body availability and stable welcome/entity incarnation. */
function CursorViewer({
  connection,
}: {
  readonly connection: SimulationConnection;
}) {
  const thrust = useThrustInput(selectThrustInputOptions(connection, 1));
  return (
    <SimulationViewer
      abilityControls={abilityAvailabilityReport({
        connection,
        lastNonzeroAimDirection: thrust.lastNonzeroAimDirection,
        lobbyId: 1,
      })}
      lobbyId={1}
      connection={connection}
      thrust={thrust.direction}
      onActivateAbility={thrust.activateAbility}
      onAimObservation={thrust.observeAim}
    />
  );
}

/** Every ability command a sender saw, in order, ignoring the steering traffic around them. */
function abilityCommands(
  calls: readonly (readonly [SessionCommand])[],
): readonly SessionCommand[] {
  return calls
    .map(([command]) => command)
    .filter(
      (command) => command.kind === 'charge' || command.kind === 'shield',
    );
}

async function advanceThrustInterval() {
  await act(async () => {
    await vi.advanceTimersByTimeAsync(THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS);
  });
}

function createConnection(
  overrides: Partial<SimulationConnection> = {},
): SimulationConnection {
  return Object.freeze({
    configuration,
    entities: snapshot.data.entities,
    error: null,
    match: snapshot.data.match,
    movementTuning: Object.freeze({ status: 'idle' }),
    ownEntityId: 7,
    reconnectAttempt: 0,
    sendCommand: vi.fn(() => true),
    session,
    snapshot,
    status: 'connected',
    ...overrides,
  });
}

function createRaceConnection(
  scenario: RaceSnapshotScenario = 'running',
  controllerId = 3,
): SimulationConnection {
  const document = raceScenarioDocument(scenario);
  const frame = validateSessionSnapshotMessage(document, {
    messageSequence: 1,
    requestId: document.meta.request_id,
    tickSequence: null,
    npcCatalogue: legacyNpcCatalogue,
    terrain: raceTerrain,
  });
  return createConnection({
    entities: frame.data.entities,
    match: frame.data.match,
    ownEntityId:
      frame.data.entities.find(
        (entity) =>
          entity.components.controllable?.controller_id === controllerId,
      )?.entity_id ?? null,
    session: { ...session, controllerId, mode: 'race', terrain: raceTerrain },
    snapshot: frame,
  });
}

/** The golden roster with the session's own blob (entity 7) reporting a given exposure counter. */
function entitiesWithOwnExposure(
  outsideTicks: number,
): readonly SessionEntitySnapshot[] {
  return snapshot.data.entities.map((entity) =>
    entity.entity_id === 7
      ? {
          entity_id: entity.entity_id,
          components: {
            ...entity.components,
            zone_exposure: { outside_ticks: outsideTicks },
          },
        }
      : entity,
  );
}

/**
 * The golden roster with the session's own blob (entity 7) publishing ability windows, read at the
 * golden frame's own tick of 12,904 and the published cadence of 400 ticks per second. The windows
 * are supplied rather than round-tripped through the validator for the reason
 * `entitiesWithOwnExposure` is: this suite exercises the composed view, and every window meets the
 * real validator at its boundary ticks in `sessionSelectors.test.ts`.
 */
function entitiesWithOwnAbilities(
  abilities: Pick<
    SessionEntitySnapshot['components'],
    'charge' | 'shield' | 'stun'
  >,
): readonly SessionEntitySnapshot[] {
  return snapshot.data.entities.map((entity) =>
    entity.entity_id === 7
      ? {
          entity_id: entity.entity_id,
          components: { ...entity.components, ...abilities },
        }
      : entity,
  );
}

/** The one live perfect opening in this suite: 200 of its ticks are still ahead of tick 12,904. */
const OPEN_PERFECT_SHIELD_WINDOWS = Object.freeze({
  activation_tick: 12900,
  shield_expiry_tick: 13160,
  perfect_expiry_tick: 13104,
  cooldown_expiry_tick: 13260,
  parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
});

afterEach(() => {
  vi.useRealTimers();
  vi.restoreAllMocks();
});

describe('SimulationViewer', () => {
  it('steers exclusively from cursor direction at fixed strength and releases to coast without reactivation after pointer departure', async () => {
    vi.useFakeTimers();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const sender = vi.fn((command: SessionCommand) => {
      void command;
      return true;
    });
    render(
      <CursorViewer
        connection={cursorSteeringConnection(sender, cameraSessionIdentity())}
      />,
    );
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    installCanvasAimSurface(canvas);
    fireEvent.pointerDown(canvas, aimPointer(590, 370, { buttons: 1 }));
    fireEvent.keyDown(canvas, { code: 'KeyW' });
    fireEvent.keyDown(canvas, { code: 'ArrowRight' });
    expect(sender).not.toHaveBeenCalled();
    fireEvent.keyDown(canvas, { code: 'Space' });
    expect(sender).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 1, y: 0 },
    });
    fireEvent.pointerMove(canvas, aimPointer(980, 370));
    expect(sender).toHaveBeenCalledTimes(1);
    fireEvent.pointerMove(canvas, aimPointer(680, 470));
    await advanceThrustInterval();
    const diagonal = sender.mock.lastCall?.[0];
    if (diagonal?.kind !== 'set_thrust')
      throw new Error('TEST.CURSOR_THRUST_MISSING');
    expect(diagonal.payload.x).toBeCloseTo(Math.SQRT1_2, 12);
    expect(diagonal.payload.y).toBeCloseTo(Math.SQRT1_2, 12);
    fireEvent.pointerMove(canvas, aimPointer(580, 370));
    await advanceThrustInterval();
    expect(sender).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 0, y: 0 },
    });
    fireEvent.pointerMove(canvas, aimPointer(680, 370));
    await advanceThrustInterval();
    expect(sender).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 1, y: 0 },
    });
    fireEvent.pointerLeave(canvas, aimPointer());
    await advanceThrustInterval();
    expect(sender).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 0, y: 0 },
    });
    const beforeReentry = sender.mock.calls.length;
    fireEvent.pointerEnter(canvas, aimPointer());
    fireEvent.keyDown(canvas, { code: 'Space', repeat: true });
    await advanceThrustInterval();
    expect(sender).toHaveBeenCalledTimes(beforeReentry);
    fireEvent.keyUp(canvas, { code: 'Space' });
    fireEvent.keyDown(canvas, { code: 'Space' });
    expect(sender).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 1, y: 0 },
    });
    fireEvent.keyUp(canvas, { code: 'Space' });
    await advanceThrustInterval();
    expect(sender).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 0, y: 0 },
    });
  });

  it('cancels held input across a missed stun through the production selector and accepts fresh same-direction activation', async () => {
    vi.useFakeTimers();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const sender = vi.fn((command: SessionCommand) => {
      void command;
      return true;
    });
    const identity = cameraSessionIdentity();
    const view = render(
      <CursorViewer
        connection={stunInputConnection(sender, identity, undefined)}
      />,
    );
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    installCanvasAimSurface(canvas);
    fireEvent.pointerMove(canvas, aimPointer());
    fireEvent.keyDown(canvas, { code: 'Space' });
    fireEvent.pointerMove(canvas, aimPointer(580, 270));
    view.rerender(
      <CursorViewer
        connection={stunInputConnection(
          sender,
          identity,
          STUN_INPUT_GENERATION,
        )}
      />,
    );
    fireEvent.pointerMove(canvas, aimPointer());
    fireEvent.keyDown(canvas, { code: 'Space', repeat: true });
    await advanceThrustInterval();
    expect(sender).toHaveBeenCalledTimes(1);
    fireEvent.keyUp(canvas, { code: 'Space' });
    fireEvent.keyDown(canvas, { code: 'Space' });
    expect(sender).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 1, y: 0, input_generation: STUN_INPUT_GENERATION },
    });
    view.rerender(
      <CursorViewer
        connection={stunInputConnection(
          sender,
          identity,
          STUN_INPUT_GENERATION,
        )}
      />,
    );
    fireEvent.pointerMove(canvas, aimPointer(580, 270));
    await advanceThrustInterval();
    expect(sender).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 0, y: -1, input_generation: STUN_INPUT_GENERATION },
    });
  });

  it('keeps observed stun locked until authoritative expiry and then requires fresh Space', async () => {
    vi.useFakeTimers();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const sender = vi.fn((command: SessionCommand) => {
      void command;
      return true;
    });
    const identity = cameraSessionIdentity();
    const view = render(
      <CursorViewer
        connection={stunInputConnection(
          sender,
          identity,
          STUN_INPUT_GENERATION,
        )}
      />,
    );
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    installCanvasAimSurface(canvas);
    fireEvent.pointerMove(canvas, aimPointer());
    fireEvent.keyDown(canvas, { code: 'Space' });
    view.rerender(
      <CursorViewer
        connection={stunInputConnection(
          sender,
          identity,
          STUN_INPUT_NEXT_GENERATION,
          STUN_INPUT_WINDOW,
        )}
      />,
    );
    fireEvent.keyUp(canvas, { code: 'Space' });
    fireEvent.pointerMove(canvas, aimPointer());
    fireEvent.keyDown(canvas, { code: 'Space' });
    await act(async () => {
      await vi.advanceTimersByTimeAsync(60_000);
    });
    fireEvent.keyDown(canvas, { code: 'Space' });
    expect(sender).toHaveBeenCalledTimes(1);
    view.rerender(
      <CursorViewer
        connection={stunInputConnection(
          sender,
          identity,
          STUN_INPUT_NEXT_GENERATION,
          STUN_INPUT_WINDOW,
          STUN_INPUT_WINDOW.expiry_tick,
        )}
      />,
    );
    fireEvent.keyDown(canvas, { code: 'Space', repeat: true });
    await advanceThrustInterval();
    expect(sender).toHaveBeenCalledTimes(1);
    fireEvent.keyUp(canvas, { code: 'Space' });
    fireEvent.keyDown(canvas, { code: 'Space' });
    expect(sender).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 1, y: 0, input_generation: STUN_INPUT_NEXT_GENERATION },
    });
  });

  it('updates stationary-cursor aim as a body moves in manual view without treating new snapshot objects as a new body', async () => {
    vi.useFakeTimers();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const sender = vi.fn((command: SessionCommand) => {
      void command;
      return true;
    });
    const identity = cameraSessionIdentity();
    const view = render(
      <CursorViewer connection={cursorSteeringConnection(sender, identity)} />,
    );
    const manual = screen.getByRole('button', { name: 'Manual view' });
    act(() => manual.focus());
    fireEvent.click(manual);
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    installCanvasAimSurface(canvas);
    act(() => canvas.focus());
    fireEvent.pointerMove(canvas, aimPointer());
    fireEvent.keyDown(canvas, { code: 'Space' });
    expect(sender).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 1, y: 0 },
    });
    view.rerender(
      <CursorViewer
        connection={cursorSteeringConnection(sender, identity, 'initial', {
          x: 600,
          y: 430,
        })}
      />,
    );
    await advanceThrustInterval();
    expect(sender).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 0, y: -1 },
    });
    const sends = sender.mock.calls.length;
    view.rerender(
      <CursorViewer
        connection={cursorSteeringConnection(sender, identity, 'initial', {
          x: 600,
          y: 430,
        })}
      />,
    );
    await advanceThrustInterval();
    expect(sender).toHaveBeenCalledTimes(sends);
    fireEvent.pointerDown(canvas, aimPointer(700, 390, { buttons: 1 }));
    await advanceThrustInterval();
    expect(sender).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 0, y: 0 },
    });
    fireEvent.pointerUp(canvas, aimPointer(700, 390));
    fireEvent.lostPointerCapture(canvas, aimPointer(700, 390));
    fireEvent.keyDown(canvas, { code: 'Space', repeat: true });
    await advanceThrustInterval();
    expect(sender).toHaveBeenCalledTimes(sends + 1);
    // Ordinary capture release keeps the in-canvas aim: only a new go press, not mouse motion,
    // is required. Unexpected capture loss is a separate cancellation path in Canvas tests.
    fireEvent.keyUp(canvas, { code: 'Space' });
    fireEvent.keyDown(canvas, { code: 'Space' });
    expect(sender).toHaveBeenCalledTimes(sends + 2);
    const resumed = sender.mock.lastCall?.[0];
    if (resumed?.kind !== 'set_thrust')
      throw new Error('TEST.CURSOR_THRUST_MISSING');
    expect(resumed.payload.x).toBeCloseTo(20 / Math.hypot(20, -80), 12);
    expect(resumed.payload.y).toBeCloseTo(-80 / Math.hypot(20, -80), 12);
  });

  it('requires fresh go after actual body loss, entity replacement, and a same-number new welcome', async () => {
    vi.useFakeTimers();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const sender = vi.fn((command: SessionCommand) => {
      void command;
      return true;
    });
    let identity = cameraSessionIdentity();
    const view = render(
      <CursorViewer connection={cursorSteeringConnection(sender, identity)} />,
    );
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    installCanvasAimSurface(canvas);
    fireEvent.pointerDown(canvas, aimPointer(680, 370, { buttons: 1 }));
    fireEvent.keyDown(canvas, { code: 'Space' });
    expect(sender).toHaveBeenCalledTimes(1);
    view.rerender(
      <CursorViewer
        connection={cursorSteeringConnection(sender, identity, 'bodyless')}
      />,
    );
    view.rerender(
      <CursorViewer connection={cursorSteeringConnection(sender, identity)} />,
    );
    fireEvent.keyDown(canvas, { code: 'Space', repeat: true });
    await advanceThrustInterval();
    expect(sender).toHaveBeenCalledTimes(1);
    fireEvent.keyUp(canvas, { code: 'Space' });
    fireEvent.keyDown(canvas, { code: 'Space' });
    expect(sender).toHaveBeenCalledTimes(2);
    view.rerender(
      <CursorViewer
        connection={cursorSteeringConnection(sender, identity, 'replacement')}
      />,
    );
    fireEvent.keyDown(canvas, { code: 'Space', repeat: true });
    await advanceThrustInterval();
    expect(sender).toHaveBeenCalledTimes(2);
    fireEvent.keyUp(canvas, { code: 'Space' });
    fireEvent.keyDown(canvas, { code: 'Space' });
    expect(sender).toHaveBeenCalledTimes(3);
    identity = { ...identity };
    view.rerender(
      <CursorViewer
        connection={cursorSteeringConnection(sender, identity, 'replacement')}
      />,
    );
    fireEvent.keyDown(canvas, { code: 'Space', repeat: true });
    await advanceThrustInterval();
    expect(sender).toHaveBeenCalledTimes(3);
  });

  it('isolates numeric and range tuning edits from steering and camera movement until explicit Apply', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const connection = createConnection({
      session: {
        ...session,
        acceptedCommandKinds: ['set_thrust', 'set_movement_tuning'],
      },
    });
    function EditableViewer() {
      const thrust = useThrustInput(selectThrustInputOptions(connection, 1));
      return (
        <SimulationViewer
          lobbyId={1}
          connection={connection}
          thrust={thrust.direction}
          onAimObservation={thrust.observeAim}
        />
      );
    }
    render(<EditableViewer />);
    const canvas = screen.getByRole('img');
    const beforeX = canvas.getAttribute('data-camera-center-x');
    const beforeY = canvas.getAttribute('data-camera-center-y');
    const acceleration = screen.getByRole('spinbutton', {
      name: 'Acceleration (wu/s²)',
    });
    act(() => acceleration.focus());
    expect(fireEvent.keyDown(acceleration, { code: 'ArrowUp' })).toBe(true);
    fireEvent.keyUp(acceleration, { code: 'ArrowUp' });
    fireEvent.keyDown(acceleration, { code: 'KeyW' });
    fireEvent.keyUp(acceleration, { code: 'KeyW' });
    expect(fireEvent.keyDown(acceleration, { code: 'Space' })).toBe(true);
    fireEvent.keyUp(acceleration, { code: 'Space' });
    fireEvent.change(acceleration, { target: { value: '480' } });
    fireEvent.change(
      screen.getByRole('slider', { name: 'Normal top speed slider' }),
      { target: { value: '1250' } },
    );
    expect(connection.sendCommand).not.toHaveBeenCalled();
    expect(canvas).toHaveAttribute('data-camera-center-x', beforeX);
    expect(canvas).toHaveAttribute('data-camera-center-y', beforeY);
    const apply = screen.getByRole('button', { name: 'Apply movement tuning' });
    act(() => apply.focus());
    fireEvent.keyDown(apply, { code: 'ArrowRight' });
    fireEvent.keyUp(apply, { code: 'ArrowRight' });
    expect(fireEvent.keyDown(apply, { code: 'Space' })).toBe(true);
    fireEvent.keyUp(apply, { code: 'Space' });
    expect(connection.sendCommand).not.toHaveBeenCalled();
    fireEvent.click(apply);
    expect(connection.sendCommand).toHaveBeenCalledTimes(1);
    expect(connection.sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_movement_tuning',
      payload: {
        tuning_request_id: 1,
        expected_revision: snapshot.data.match.movement.revision,
        acceleration_world_units_per_second_squared: 480,
        normal_top_speed_world_units_per_second: 1250,
      },
    });
    expect(canvas).toHaveAttribute('data-camera-center-x', beforeX);
    expect(canvas).toHaveAttribute('data-camera-center-y', beforeY);
  });

  it('keeps native camera activation independent and removes directional keyboard steering', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const connection = createConnection();
    function SteerableViewer() {
      const thrust = useThrustInput(selectThrustInputOptions(connection, 1));
      return (
        <SimulationViewer
          lobbyId={1}
          connection={connection}
          thrust={thrust.direction}
          onAimObservation={thrust.observeAim}
        />
      );
    }
    render(<SteerableViewer />);
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    installCanvasAimSurface(canvas);
    const initialX = Number(canvas.getAttribute('data-camera-center-x'));
    const initialY = Number(canvas.getAttribute('data-camera-center-y'));
    expect(
      screen.getByRole('button', { name: 'Follow player' }),
    ).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getByRole('button', { name: 'Pan right' })).toBeDisabled();
    fireEvent.click(screen.getByRole('button', { name: 'Manual view' }));
    const panRight = screen.getByRole('button', { name: 'Pan right' });
    fireEvent.pointerMove(canvas, aimPointer());
    act(() => panRight.focus());
    expect(panRight).toBeEnabled();
    expect(fireEvent.keyDown(panRight, { code: 'Space' })).toBe(true);
    fireEvent.keyUp(panRight, { code: 'Space' });
    fireEvent.click(panRight);
    expect(canvas).toHaveAttribute('data-camera-mode', 'manual');
    expect(Number(canvas.getAttribute('data-camera-center-x'))).toBe(
      initialX + 96,
    );
    expect(Number(canvas.getAttribute('data-camera-center-y'))).toBe(initialY);
    expect(connection.sendCommand).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: 'Follow player' }));
    expect(Number(canvas.getAttribute('data-camera-center-x'))).toBe(initialX);
    expect(connection.sendCommand).not.toHaveBeenCalled();
    fireEvent.keyDown(screen.getByRole('button', { name: 'Follow player' }), {
      code: 'ArrowRight',
    });
    expect(connection.sendCommand).not.toHaveBeenCalled();
    expect(Number(canvas.getAttribute('data-camera-center-x'))).toBe(initialX);
  });

  it('retains manual and bodyless views but resets a new welcome without losing debug disclosure', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const welcome = cameraSessionIdentity();
    const world = cameraConfiguration();
    function cameraConnection(
      scenario: CameraSnapshotScenario,
      identity = welcome,
    ): SimulationConnection {
      const data = cameraSnapshot(scenario);
      return createConnection({
        configuration: world,
        session: identity,
        entities: data.entities,
        match: data.match,
        ownEntityId:
          data.entities.find(
            (entity) =>
              entity.components.controllable?.controller_id ===
              identity.controllerId,
          )?.entity_id ?? null,
        snapshot: { ...snapshot, data },
      });
    }
    const view = render(
      <SimulationViewer
        lobbyId={1}
        connection={cameraConnection('initial')}
        thrust={zeroThrust}
      />,
    );
    fireEvent.click(
      screen.getByRole('button', { name: 'Show simulation details' }),
    );
    const canvas = screen.getByRole('img');
    expect(canvas).toHaveAttribute('data-camera-center-x', '500');
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={cameraConnection('moved')}
        thrust={zeroThrust}
      />,
    );
    expect(canvas).toHaveAttribute('data-camera-center-x', '1500');
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={cameraConnection('bodyless')}
        thrust={zeroThrust}
      />,
    );
    expect(canvas).toHaveAttribute('data-camera-center-x', '1500');
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={cameraConnection('replacement')}
        thrust={zeroThrust}
      />,
    );
    expect(canvas).toHaveAttribute('data-camera-center-x', '700');
    fireEvent.click(screen.getByRole('button', { name: 'Manual view' }));
    fireEvent.click(screen.getByRole('button', { name: 'Pan right' }));
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={cameraConnection('moved')}
        thrust={zeroThrust}
      />,
    );
    expect(canvas).toHaveAttribute('data-camera-center-x', '796');
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={cameraConnection('initial', cameraSessionIdentity())}
        thrust={zeroThrust}
      />,
    );
    expect(canvas).toHaveAttribute('data-camera-mode', 'follow');
    expect(canvas).toHaveAttribute('data-camera-center-x', '500');
    expect(
      screen.getByRole('button', { name: 'Hide simulation details' }),
    ).toHaveAttribute('aria-expanded', 'true');
  });

  it('renders accessible match state with the debug panel behind a toggle', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection()}
        thrust={zeroThrust}
      />,
    );

    // The app's own name is the shell's h1; a room view is one level down.
    expect(
      screen.getByRole('heading', { level: 2, name: 'Room 1' }),
    ).toBeVisible();
    expect(screen.getByRole('status')).toHaveTextContent(
      'Connected to the match session.',
    );
    expect(screen.getByRole('img')).toHaveAccessibleDescription(
      'Complete tick 12904 with 4 entities and 2 players.',
    );

    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(within(hud).getByText('Cole Shaffer')).toBeVisible();
    expect(within(hud).getByText('running')).toBeVisible();
    expect(within(hud).getByText('In play')).toBeVisible();
    expect(within(hud).getByRole('row', { name: 'Alive 2' })).toBeVisible();

    expect(
      screen.queryByRole('table', { name: 'Session identity' }),
    ).toBeNull();
    const toggle = screen.getByRole('button', {
      name: 'Show simulation details',
    });
    expect(toggle).toHaveAttribute('aria-expanded', 'false');

    fireEvent.click(toggle);

    expect(
      screen.getByRole('table', { name: 'Session identity' }),
    ).toBeVisible();
    expect(
      screen.getByRole('button', { name: 'Hide simulation details' }),
    ).toHaveAttribute('aria-expanded', 'true');
  });

  it('tells a deferred joiner it is waiting for the next match', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: [],
          match: null,
          ownEntityId: null,
          session: null,
          snapshot: null,
          status: 'awaiting_match',
        })}
        thrust={zeroThrust}
      />,
    );

    expect(screen.getByRole('status')).toHaveTextContent(
      'Joined the session. Waiting for the next match to seat a blob…',
    );
    expect(screen.getByRole('img')).toHaveAccessibleDescription(
      'Waiting for the first complete world snapshot.',
    );
  });

  it('announces the eliminated overlay for a player with a placement', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const eliminatedSession: SimulationSessionIdentity = {
      ...session,
      controllerId: 6,
    };

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          ownEntityId: null,
          session: eliminatedSession,
        })}
        thrust={zeroThrust}
      />,
    );

    expect(screen.getByText('Eliminated')).toBeVisible();
    expect(screen.getByText(/You placed #3\./)).toBeVisible();
    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(within(hud).getByText('#3')).toBeVisible();
  });

  it('announces the winner overlay when the match has ended', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          match: {
            ...snapshot.data.match,
            outcome: {
              kind: 'won_by_entity',
              winner_entity_id: 7,
              winner_team_id: null,
            },
            phase: 'ended',
          },
        })}
        thrust={zeroThrust}
      />,
    );

    expect(screen.getByText('You win')).toBeVisible();
    expect(
      screen.getByText('You were the last blob in the zone.'),
    ).toBeVisible();
  });

  it('counts the remaining grace down in the HUD while the own blob is outside', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const hudRowHeader = () =>
      within(screen.getByRole('table', { name: 'Match status' })).queryByRole(
        'rowheader',
        { name: 'Zone exposure' },
      );

    // The golden own blob is inside the zone, so there is nothing to warn about.
    const view = render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection()}
        thrust={zeroThrust}
      />,
    );
    expect(hudRowHeader()).toBeNull();

    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: entitiesWithOwnExposure(200),
        })}
        thrust={zeroThrust}
      />,
    );

    expect(hudRowHeader()).toBeVisible();
    // 200 of the golden frame's published 1,200 grace ticks spent leaves 1,000, which is 2.5 s at
    // the published 400 ticks/s. Remaining, not elapsed: `elimination_grace_ticks` reaches the
    // client in the royale mode-state block since protocol 2.2, so the row answers the question the
    // player actually has.
    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(
      within(hud).getByRole('row', { name: 'Zone exposure 2.5 s left' }),
    ).toBeVisible();

    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({ entities: entitiesWithOwnExposure(0) })}
        thrust={zeroThrust}
      />,
    );

    expect(hudRowHeader()).toBeNull();
    expect(
      within(screen.getByRole('table', { name: 'Match status' })).queryByText(
        /left/,
      ),
    ).toBeNull();
  });

  it('falls back to elapsed exposure when the frame publishes no grace', () => {
    // A mode with no non-entity-shaped state publishes the `none` block and therefore no grace.
    // The client must not invent a duration; it reports the counter it really was given.
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: entitiesWithOwnExposure(1_200),
          match: {
            ...snapshot.data.match,
            mode_state: {
              schema_id: 'blob-royale://protocol/v3/mode-state/none',
              value: {},
            },
          },
        })}
        thrust={zeroThrust}
      />,
    );

    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(
      within(hud).getByRole('row', { name: 'Zone exposure Outside 3.0 s' }),
    ).toBeVisible();
  });

  it('shows no time left rather than a negative countdown past the grace', () => {
    // `zone_elimination` eliminates on the tick the counter reaches the bound and the recorder
    // destroys the entity in the same tick, so a published counter should never exceed it. The
    // clamp is what keeps a frame that says otherwise from rendering a negative remainder.
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: entitiesWithOwnExposure(5_000),
        })}
        thrust={zeroThrust}
      />,
    );

    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(
      within(hud).getByRole('row', { name: 'Zone exposure 0.0 s left' }),
    ).toBeVisible();
  });

  it('does not warn about a peer that is outside the zone', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    // Entity 8 in the golden snapshot has been outside for 214 ticks; entity 7 is this session.
    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection()}
        thrust={zeroThrust}
      />,
    );

    expect(
      within(screen.getByRole('table', { name: 'Match status' })).queryByRole(
        'rowheader',
        { name: 'Zone exposure' },
      ),
    ).toBeNull();
  });

  it('states both cooldowns as remaining time and never claims an ability is ready', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: entitiesWithOwnAbilities({
            charge: CHARGE_COOLDOWN,
            shield: SHIELD_WINDOWS,
          }),
        })}
        thrust={zeroThrust}
      />,
    );

    // Golden tick 12,904 at the published 400 ticks per second: protection runs to 12,960, the
    // shield cooldown to 13,160 and the charge cooldown to 13,280, and the perfect opening closed
    // at 12,832. Every row states remaining time against a window the frame really carries.
    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(
      within(hud).getByRole('row', { name: 'Shield Protected 0.1 s left' }),
    ).toBeVisible();
    expect(
      within(hud).getByRole('row', { name: 'Shield cooldown Cooling 0.6 s' }),
    ).toBeVisible();
    expect(
      within(hud).getByRole('row', { name: 'Charge cooldown Cooling 0.9 s' }),
    ).toBeVisible();

    // Readiness is not derivable and must not be implied. Charge's final refusal is the server's
    // safety envelope, which is deliberately not on the wire, so a row that promised availability
    // would lie on exactly the frames a player would act on it.
    expect(within(hud).queryByText(/ready/i)).toBeNull();
    expect(within(hud).queryByText(/available/i)).toBeNull();
  });

  it('separates the perfect opening from ordinary protection in the shield row', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: entitiesWithOwnAbilities({
            shield: OPEN_PERFECT_SHIELD_WINDOWS,
          }),
        })}
        thrust={zeroThrust}
      />,
    );

    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(
      within(hud).getByRole('row', {
        name: 'Shield Perfect opening 0.5 s left',
      }),
    ).toBeVisible();
    expect(
      within(hud).getByRole('row', { name: 'Shield cooldown Cooling 0.9 s' }),
    ).toBeVisible();
  });

  it('reports a cancelled shield as no protection rather than as a published component', () => {
    // A stun shortens still-live protection to the cancelling tick and leaves the cooldown running,
    // so the component outlives the protection by the whole remainder of its cooldown. This is the
    // majority of a shield's published life by duration, and a row keyed on presence would tell a
    // stunned player they are safe.
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: entitiesWithOwnAbilities({
            shield: CANCELLED_SHIELD_WINDOWS,
          }),
        })}
        thrust={zeroThrust}
      />,
    );

    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(
      within(hud).getByRole('row', { name: 'Shield No protection' }),
    ).toBeVisible();
    expect(
      within(hud).getByRole('row', { name: 'Shield cooldown Cooling 0.6 s' }),
    ).toBeVisible();
    expect(within(hud).queryByText(/Perfect/)).toBeNull();
  });

  it('renders the zero-cooldown shield frame as an elapsed cooldown, not as a division', () => {
    // `cooldown_expiry_tick === activation_tick` is authored tuning the configuration permits, so
    // this is a frame the server is required to be able to send and the one that divides by zero if
    // the denominator is trusted. The row reports the cooldown as over and prints no NaN.
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: entitiesWithOwnAbilities({
            shield: {
              ...SHIELD_WINDOWS,
              cooldown_expiry_tick: SHIELD_ACTIVATION_TICK,
            },
          }),
        })}
        thrust={zeroThrust}
      />,
    );

    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(
      within(hud).getByRole('row', { name: 'Shield cooldown Cooldown over' }),
    ).toBeVisible();
    expect(within(hud).queryByText(/NaN|Infinity/)).toBeNull();
    expect(within(hud).queryByText(/ready/i)).toBeNull();
  });

  it('shows the stun row only while the published window still covers the tick', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const stunRow = () =>
      within(screen.getByRole('table', { name: 'Match status' })).queryByRole(
        'rowheader',
        { name: 'Stunned' },
      );

    const view = render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: entitiesWithOwnAbilities({
            stun: { activation_tick: 12800, expiry_tick: 13000 },
          }),
        })}
        thrust={zeroThrust}
      />,
    );

    // 13,000 - 12,904 leaves 96 ticks, which is 0.24 s at 400 ticks per second.
    expect(
      within(screen.getByRole('table', { name: 'Match status' })).getByRole(
        'row',
        { name: 'Stunned 0.2 s left' },
      ),
    ).toBeVisible();

    // A window whose endpoint is this very tick no longer contains it. Containment is half-open, so
    // the row clears with the frame that ended the stun and no client timer can disagree.
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: entitiesWithOwnAbilities({
            stun: { activation_tick: 12800, expiry_tick: 12904 },
          }),
        })}
        thrust={zeroThrust}
      />,
    );

    expect(stunRow()).toBeNull();
  });

  it('shows no ability row without a published component or without a tick to read against', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const abilityHeaders = () => {
      const hud = within(screen.getByRole('table', { name: 'Match status' }));
      return [
        hud.queryByRole('rowheader', { name: 'Stunned' }),
        hud.queryByRole('rowheader', { name: 'Shield' }),
        hud.queryByRole('rowheader', { name: 'Shield cooldown' }),
        hud.queryByRole('rowheader', { name: 'Charge cooldown' }),
      ];
    };

    // The golden blob has published no ability component at all.
    const view = render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection()}
        thrust={zeroThrust}
      />,
    );
    expect(abilityHeaders()).toEqual([null, null, null, null]);

    // Windows are absolute committed ticks, so a session holding a roster but no snapshot has
    // nothing to read them against. It shows no row rather than a countdown of its own invention.
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: entitiesWithOwnAbilities({
            charge: CHARGE_COOLDOWN,
            shield: SHIELD_WINDOWS,
            stun: { activation_tick: 12800, expiry_tick: 13000 },
          }),
          snapshot: null,
        })}
        thrust={zeroThrust}
      />,
    );
    expect(abilityHeaders()).toEqual([null, null, null, null]);
  });

  it('offers a named, keyboard-operable button for each ability and sends each its own payload shape', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const sender = vi.fn((command: SessionCommand) => {
      void command;
      return true;
    });
    render(
      <CursorViewer
        connection={cursorSteeringConnection(sender, cameraSessionIdentity())}
      />,
    );
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    installCanvasAimSurface(canvas);
    fireEvent.pointerMove(canvas, aimPointer());

    const shield = screen.getByRole('button', { name: 'Shield' });
    const charge = screen.getByRole('button', { name: 'Charge' });
    expect(shield).toHaveAttribute('aria-keyshortcuts', 'S');
    expect(charge).toHaveAttribute('aria-keyshortcuts', 'D');
    expect(shield).toHaveAttribute('aria-disabled', 'false');
    expect(charge).toHaveAttribute('aria-disabled', 'false');
    expect(shield).not.toHaveAccessibleDescription();

    // Enter is a button's own activation key, fired by the browser on keydown and repeated while
    // the key is held. The control takes both edges over and keeps the focus its user chose.
    act(() => shield.focus());
    expect(fireEvent.keyDown(shield, { key: 'Enter' })).toBe(false);
    fireEvent.keyUp(shield, { key: 'Enter' });
    expect(document.activeElement).toBe(shield);

    // A focused button also activates on Space, which is the go key. Prevented on both edges,
    // because a space press activates a button on its way up rather than on its way down.
    act(() => charge.focus());
    expect(fireEvent.keyDown(charge, { key: ' ', code: 'Space' })).toBe(false);
    expect(fireEvent.keyUp(charge, { key: ' ', code: 'Space' })).toBe(false);

    // Never invalidated is the state every blob is in until its first stun, and the two payloads
    // spell it differently: shield's member is required and nullable, charge's is omitted exactly
    // as `set_thrust`'s is. Sending shield the thrust way produces `{}` and is refused in silence.
    expect(abilityCommands(sender.mock.calls)).toEqual([
      { kind: 'shield', payload: { input_generation: null } },
      { kind: 'charge', payload: { x: 1, y: 0 } },
    ]);
  });

  it('activates once for a held Enter on a button and once for a held ability key', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const sender = vi.fn((command: SessionCommand) => {
      void command;
      return true;
    });
    render(
      <CursorViewer
        connection={cursorSteeringConnection(sender, cameraSessionIdentity())}
      />,
    );
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    installCanvasAimSurface(canvas);
    fireEvent.pointerMove(canvas, aimPointer());

    // A held Enter on a focused button is a second repeat source: the browser fires one click per
    // repeat, so a control that simply trusted the click would send a pulse per repeat.
    const shield = screen.getByRole('button', { name: 'Shield' });
    act(() => shield.focus());
    fireEvent.keyDown(shield, { key: 'Enter' });
    fireEvent.keyDown(shield, { key: 'Enter', repeat: true });
    fireEvent.keyDown(shield, { key: 'Enter', repeat: true });
    fireEvent.keyUp(shield, { key: 'Enter' });
    expect(abilityCommands(sender.mock.calls)).toHaveLength(1);

    // The key path carries the same one-shot rule. It is pressed from the arena rather than from
    // the control just used, because a focused button swallows an ability key by design.
    act(() => shield.blur());
    fireEvent.keyDown(canvas, { code: CHARGE_KEY_CODE });
    fireEvent.keyDown(canvas, { code: CHARGE_KEY_CODE, repeat: true });
    fireEvent.keyUp(canvas, { code: CHARGE_KEY_CODE });
    expect(abilityCommands(sender.mock.calls)).toHaveLength(2);
  });

  it('hands the go key back after a pointer press on a control and keeps focus after a keyboard one', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const sender = vi.fn((command: SessionCommand) => {
      void command;
      return true;
    });
    render(
      <CursorViewer
        connection={cursorSteeringConnection(sender, cameraSessionIdentity())}
      />,
    );
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    installCanvasAimSurface(canvas);
    fireEvent.pointerMove(canvas, aimPointer());

    // A click leaves the button focused, a focused button is read as UI and swallows the ability
    // key, and the browser then activates that same button on Space. Left alone, one press of a
    // mouse would turn the go key into the shield key for the rest of the match.
    const shield = screen.getByRole('button', { name: 'Shield' });
    act(() => shield.focus());
    fireEvent.click(shield, { detail: 1 });
    expect(abilityCommands(sender.mock.calls)).toHaveLength(1);
    expect(document.activeElement).not.toBe(shield);
    fireEvent.keyDown(canvas, { code: 'Space' });
    expect(sender).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 1, y: 0 },
    });
    fireEvent.keyUp(canvas, { code: 'Space' });

    // A keyboard activation never reaches the click path at all, so it keeps the focus ring its
    // user is navigating with and can be pressed again without finding the control a second time.
    const charge = screen.getByRole('button', { name: 'Charge' });
    act(() => charge.focus());
    fireEvent.keyDown(charge, { key: 'Enter' });
    fireEvent.keyUp(charge, { key: 'Enter' });
    expect(document.activeElement).toBe(charge);
    expect(abilityCommands(sender.mock.calls)).toHaveLength(2);
  });

  it('keeps an unavailable ability focusable with its reason, and outside the Match status table', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const sender = vi.fn((command: SessionCommand) => {
      void command;
      return true;
    });
    // This suite's welcome advertises `set_thrust` alone, which is a narrowing a real mode
    // publishes. An unadvertised kind is refused at the boundary and returns false from the sender,
    // so a control that looked live would silently do nothing at all.
    const connection = createConnection({ sendCommand: sender });
    function AbilityViewer() {
      const thrust = useThrustInput(selectThrustInputOptions(connection, 1));
      return (
        <SimulationViewer
          abilityControls={abilityAvailabilityReport({
            connection,
            lastNonzeroAimDirection: thrust.lastNonzeroAimDirection,
            lobbyId: 1,
          })}
          lobbyId={1}
          connection={connection}
          thrust={thrust.direction}
          onActivateAbility={thrust.activateAbility}
          onAimObservation={thrust.observeAim}
        />
      );
    }
    render(<AbilityViewer />);

    const shield = screen.getByRole('button', { name: 'Shield' });
    expect(shield).toHaveAttribute('aria-disabled', 'true');
    expect(screen.getByRole('button', { name: 'Charge' })).toHaveAttribute(
      'aria-disabled',
      'true',
    );
    // Not the `disabled` attribute the pan buttons use. That takes the control out of the tab
    // order and takes the node carrying its explanation with it, which would leave the reason
    // legible to a reader who can see the screen and to nobody else.
    expect(shield).toBeEnabled();
    act(() => shield.focus());
    expect(document.activeElement).toBe(shield);
    expect(shield).toHaveAccessibleDescription();

    fireEvent.click(shield, { detail: 1 });
    fireEvent.keyDown(shield, { key: 'Enter' });
    fireEvent.keyUp(shield, { key: 'Enter' });
    expect(abilityCommands(sender.mock.calls)).toEqual([]);

    // "unavailable" contains "available", and this table is pinned against that substring and
    // against its exact rowheader list. A control is not a published status row in any case.
    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(within(hud).queryByText(/available/i)).toBeNull();
    expect(within(hud).queryByRole('button')).toBeNull();
    const controls = screen.getByRole('region', { name: 'Ability controls' });
    expect(within(controls).getAllByRole('button')).toHaveLength(2);
  });

  it('activates no ability from a camera gesture or from a settings field', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const sender = vi.fn((command: SessionCommand) => {
      void command;
      return true;
    });
    render(
      <CursorViewer
        connection={cursorSteeringConnection(sender, cameraSessionIdentity())}
      />,
    );
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    installCanvasAimSurface(canvas);
    fireEvent.pointerMove(canvas, aimPointer());

    // Dragging the map is the camera's gesture and nothing else's. It already cancels propulsion,
    // and an ability pressed in the middle of a drag must not slip past the same rule.
    fireEvent.click(screen.getByRole('button', { name: 'Manual view' }));
    act(() => canvas.focus());
    fireEvent.pointerDown(canvas, aimPointer(700, 390, { buttons: 1 }));
    fireEvent.keyDown(canvas, { code: SHIELD_KEY_CODE });
    fireEvent.keyDown(canvas, { code: CHARGE_KEY_CODE });
    expect(abilityCommands(sender.mock.calls)).toEqual([]);
    fireEvent.pointerUp(canvas, aimPointer(700, 390));
    fireEvent.lostPointerCapture(canvas, aimPointer(700, 390));

    // Typing a tuning value is not playing. Both ability keys are ordinary letters, so a settings
    // field keeps them, and Space stays the browser's rather than becoming propulsion.
    const acceleration = screen.getByRole('spinbutton', {
      name: 'Acceleration (wu/s²)',
    });
    act(() => acceleration.focus());
    expect(document.activeElement).toBe(acceleration);
    expect(fireEvent.keyDown(acceleration, { code: SHIELD_KEY_CODE })).toBe(
      true,
    );
    expect(fireEvent.keyDown(acceleration, { code: CHARGE_KEY_CODE })).toBe(
      true,
    );
    expect(fireEvent.keyDown(acceleration, { code: 'Space' })).toBe(true);
    fireEvent.keyUp(acceleration, { code: 'Space' });
    expect(sender).not.toHaveBeenCalled();
  });

  it('announces a terminal connection failure as an alert', () => {
    const error = new SimulationApiError(
      'SIMULATION.RECONNECT_EXHAUSTED',
      'Simulation reconnect budget was exhausted.',
    );

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          configuration: null,
          entities: [],
          error,
          match: null,
          ownEntityId: null,
          reconnectAttempt: 5,
          session: null,
          snapshot: null,
          status: 'failed',
        })}
        thrust={zeroThrust}
      />,
    );

    expect(screen.getByRole('alert')).toHaveTextContent('could not connect');
    expect(screen.getByText(/SIMULATION.RECONNECT_EXHAUSTED/)).toBeVisible();
  });

  it('shows the lobby panel exactly while the match is in lobby and the session may start one', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const lobbyMatch = { ...snapshot.data.match, phase: 'lobby' as const };
    const startingSession: SimulationSessionIdentity = {
      ...session,
      acceptedCommandKinds: ['set_thrust', 'start_match'],
    };

    const view = render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          match: lobbyMatch,
          session: startingSession,
        })}
        thrust={zeroThrust}
      />,
    );
    expect(
      screen.getByRole('heading', { level: 3, name: 'Lobby' }),
    ).toBeVisible();
    expect(screen.getByRole('button', { name: 'Start match' })).toBeDisabled();

    // A running match has no lobby to operate, and neither does a session whose welcome advertised
    // no `start_match`: `sandbox` publishes only `set_thrust`.
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({ session: startingSession })}
        thrust={zeroThrust}
      />,
    );
    expect(
      screen.queryByRole('heading', { level: 3, name: 'Lobby' }),
    ).toBeNull();
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({ match: lobbyMatch })}
        thrust={zeroThrust}
      />,
    );
    expect(
      screen.queryByRole('heading', { level: 3, name: 'Lobby' }),
    ).toBeNull();
  });

  it('passes the current room catalogue into the real lobby and sends its complete profiled selection', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const connection = createConnection({
      match: { ...snapshot.data.match, phase: 'lobby' },
      session: {
        ...session,
        acceptedCommandKinds: ['set_thrust', 'seat_npc', 'start_match'],
        npcCatalogue: tacticalNpcCatalogue([STEADY_NPC_PROFILE]),
      },
    });
    const view = render(
      <SimulationViewer
        lobbyId={1}
        connection={connection}
        thrust={zeroThrust}
      />,
    );
    fireEvent.click(screen.getByRole('button', { name: 'Seat 4 Empty' }));
    expect(
      screen.getByRole('menuitem', { name: 'tactical / steady' }),
    ).toBeVisible();
    const nextSession = connection.session;
    if (nextSession === null) throw new Error('TEST.TACTICAL_SESSION_MISSING');
    view.rerender(
      <SimulationViewer
        lobbyId={2}
        connection={{
          ...connection,
          session: {
            ...nextSession,
            lobbyId: 2,
            npcCatalogue: tacticalNpcCatalogue([QUICK_NPC_PROFILE]),
          },
        }}
        thrust={zeroThrust}
      />,
    );
    expect(
      screen.queryByRole('menuitem', { name: 'tactical / steady' }),
    ).toBeNull();
    fireEvent.click(screen.getByRole('menuitem', { name: 'tactical / quick' }));
    expect(connection.sendCommand).toHaveBeenCalledExactlyOnceWith(
      tacticalSeatCommand(QUICK_NPC_PROFILE, 3),
    );
  });

  it('shows the hill section and the scoreboard for a hill frame only', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const hillSession: SimulationSessionIdentity = {
      ...session,
      map: 'hills-960x640',
      mode: 'king_of_the_hill',
    };

    const view = render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: hillSnapshot.data.entities,
          match: hillSnapshot.data.match,
          session: hillSession,
          snapshot: hillSnapshot,
        })}
        thrust={zeroThrust}
      />,
    );

    const hud = screen.getByRole('table', { name: 'Match status' });
    // 94,000 running ticks left of 96,000 at the published 400 ticks/s; 4 of 30 points; 280 of the
    // 400-tick interval still to hold.
    expect(
      within(hud).getByRole('row', { name: 'Time left 235.0 s' }),
    ).toBeVisible();
    expect(
      within(hud).getByRole('row', { name: 'Score 4 of 30' }),
    ).toBeVisible();
    expect(
      within(hud).getByRole('row', { name: 'Hill 0.7 s to a point' }),
    ).toBeVisible();
    expect(
      within(hud).queryByRole('rowheader', { name: 'Placement' }),
    ).toBeNull();

    const board = screen.getByRole('table', { name: 'Scoreboard' });
    expect(
      within(board)
        .getAllByRole('row')
        .map((row) => row.textContent),
    ).toEqual(['wanderer-16', 'Cole Shaffer4', 'chaser-22 (out)']);
    expect(
      within(board).getByRole('row', { name: 'Cole Shaffer 4' }),
    ).toHaveAttribute('aria-current', 'true');
    expect(screen.getByRole('img')).toHaveAccessibleDescription(
      'Complete tick 12904 with 4 entities and 2 players.',
    );

    // The royale golden frame renders exactly the rows it always did: no hill section, no board.
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection()}
        thrust={zeroThrust}
      />,
    );
    const royaleHud = screen.getByRole('table', { name: 'Match status' });
    expect(
      within(royaleHud)
        .getAllByRole('rowheader')
        .map((header) => header.textContent),
    ).toEqual([
      'Player',
      'Phase',
      'Phase elapsed',
      'Alive',
      'Placement',
      'Thrust',
    ]);
    expect(screen.queryByRole('table', { name: 'Scoreboard' })).toBeNull();
  });

  it('counts a knocked-out player down to its seat instead of calling it in play', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const knockedOutSession: SimulationSessionIdentity = {
      ...session,
      controllerId: 5,
      displayName: 'chaser-2',
      mode: 'king_of_the_hill',
    };

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: hillSnapshot.data.entities,
          match: hillSnapshot.data.match,
          ownEntityId: 10,
          session: knockedOutSession,
          snapshot: hillSnapshot,
        })}
        thrust={zeroThrust}
      />,
    );

    const hud = screen.getByRole('table', { name: 'Match status' });
    // 300 ticks at 400 ticks/s. The entity is still on the frame, so no overlay covers the arena.
    expect(
      within(hud).getByRole('row', { name: 'Hill Back in 0.8 s' }),
    ).toHaveClass('MatchHudDanger');
    expect(
      within(hud).getByRole('row', { name: 'Score 2 of 30' }),
    ).toBeVisible();
    expect(screen.queryByText('Eliminated')).toBeNull();
  });

  it('announces the hill winner with its points when the match has ended', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: hillSnapshot.data.entities,
          match: {
            ...hillSnapshot.data.match,
            outcome: {
              kind: 'won_by_entity',
              winner_entity_id: 8,
              winner_team_id: null,
            },
            phase: 'ended',
          },
          session: { ...session, mode: 'king_of_the_hill' },
          snapshot: hillSnapshot,
        })}
        thrust={zeroThrust}
      />,
    );

    expect(screen.getByText('Winner')).toBeVisible();
    expect(
      screen.getByText('wanderer-1 held the hill with 6 points.'),
    ).toBeVisible();
  });

  it('shows race gates and its clock while keeping unfinished racers out of the standings', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    render(
      <SimulationViewer
        lobbyId={1}
        connection={createRaceConnection()}
        thrust={zeroThrust}
      />,
    );
    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(within(hud).getByRole('row', { name: 'Gate 1 of 3' })).toBeVisible();
    expect(
      within(hud).getByRole('row', { name: 'Time left 235.0 s' }),
    ).toBeVisible();
    expect(
      within(hud).queryByRole('rowheader', { name: 'Placement' }),
    ).toBeNull();
    expect(
      within(screen.getByRole('table', { name: 'Standings' })).getByText(
        'No finishers yet',
      ),
    ).toBeVisible();
  });

  it('switches from the race clock to the finish window when the first standing arrives', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const view = render(
      <SimulationViewer
        lobbyId={1}
        connection={createRaceConnection()}
        thrust={zeroThrust}
      />,
    );
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createRaceConnection('finish_window')}
        thrust={zeroThrust}
      />,
    );
    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(
      within(hud).queryByRole('rowheader', { name: 'Time left' }),
    ).toBeNull();
    expect(
      within(hud).getByRole('row', { name: 'Finish window 4.0 s' }),
    ).toBeVisible();
    expect(within(hud).getByRole('row', { name: 'Gate 3 of 3' })).toBeVisible();
    expect(
      within(screen.getByRole('table', { name: 'Standings' })).getByRole(
        'row',
        { name: 'You #1 tick 12504 + 0.25' },
      ),
    ).toHaveAttribute('aria-current', 'true');
  });

  it('keeps a bodyless racer visible through timer expiry and an occupied checkpoint wait', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const view = render(
      <SimulationViewer
        lobbyId={1}
        connection={createRaceConnection('running', 5)}
        thrust={zeroThrust}
      />,
    );
    expect(
      screen.getByRole('row', { name: 'Return Back on the road in 0.8 s' }),
    ).toHaveClass('MatchHudDanger');
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createRaceConnection('awaiting_checkpoint', 5)}
        thrust={zeroThrust}
      />,
    );
    expect(
      screen.getByRole('row', {
        name: 'Return Back on the road in 0.0 s · waiting for a clear checkpoint',
      }),
    ).toBeVisible();
    expect(screen.queryByText('Eliminated')).toBeNull();
    expect(screen.queryByText('Waiting for the next match')).toBeNull();
  });

  it('announces an own race finish from the recorded controller after its entity is absent', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    render(
      <SimulationViewer
        lobbyId={1}
        connection={createRaceConnection('own_finish_after_wipe')}
        thrust={zeroThrust}
      />,
    );
    expect(screen.getByText('You win')).toBeVisible();
    expect(screen.getByText('You finished #1.')).toBeVisible();
    expect(
      within(screen.getByRole('table', { name: 'Standings' })).getByRole(
        'row',
        { name: 'You #1 tick 12504 + 0.25' },
      ),
    ).toHaveAttribute('aria-current', 'true');
  });

  it('renders shared finish ranks and the race draw instead of elimination results', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    render(
      <SimulationViewer
        lobbyId={1}
        connection={createRaceConnection('tied_finish')}
        thrust={zeroThrust}
      />,
    );
    const standings = screen.getByRole('table', { name: 'Standings' });
    expect(
      within(standings)
        .getAllByRole('row')
        .map((row) => row.textContent),
    ).toEqual(['You#1tick 12504 + 0.25', 'wanderer-1#1tick 12504 + 0.25']);
    expect(
      screen.getByText(
        'The first finishers crossed at the same instant. The next lobby opens shortly.',
      ),
    ).toBeVisible();
  });

  it('announces a gate leader when the clock ends a race without a finisher', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    render(
      <SimulationViewer
        lobbyId={1}
        connection={createRaceConnection('clock_win')}
        thrust={zeroThrust}
      />,
    );
    expect(
      screen.getByText(
        'wanderer-1 led on gates taken when time ran out (2 of 3).',
      ),
    ).toBeVisible();
    expect(screen.getByText('No finishers yet')).toBeVisible();
  });
});
