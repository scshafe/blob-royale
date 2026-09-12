import type {
  ThrustAimObservation,
  ThrustDirection,
  ThrustInputOptions,
} from '../useThrustInput';
import type { SimulationCommandSender } from '../useSimulationConnection';
import { cameraSessionIdentity } from './simulationCameraFrames';

export const THRUST_ZERO = Object.freeze({ x: 0, y: 0 });
export const THRUST_RIGHT = Object.freeze({ x: 1, y: 0 });
export const THRUST_LEFT = Object.freeze({ x: -1, y: 0 });
export const THRUST_UP = Object.freeze({ x: 0, y: -1 });
export const THRUST_DOWN = Object.freeze({ x: 0, y: 1 });
export const THRUST_BODY_CENTER = Object.freeze({ x: 300, y: 200 });
export const THRUST_REPLACEMENT_ENTITY_ID = 70;
export const REMOVED_DIRECTION_KEYS = Object.freeze([
  'KeyW',
  'KeyA',
  'KeyS',
  'KeyD',
  'ArrowUp',
  'ArrowLeft',
  'ArrowDown',
  'ArrowRight',
]);

/** Offset geometry is authored independently of normalization; all distances are CSS pixels. */
export function thrustAim(
  offset: ThrustDirection = THRUST_RIGHT,
  distance = 100,
  cameraGestureActive = false,
): ThrustAimObservation {
  return {
    pointer: {
      x: THRUST_BODY_CENTER.x + offset.x * distance,
      y: THRUST_BODY_CENTER.y + offset.y * distance,
    },
    projectedBodyCenter: THRUST_BODY_CENTER,
    cameraGestureActive,
  };
}

export const THRUST_DIRECTION_CASES = Object.freeze([
  { name: 'right', offset: THRUST_RIGHT, expected: THRUST_RIGHT },
  { name: 'left', offset: THRUST_LEFT, expected: THRUST_LEFT },
  { name: 'up', offset: THRUST_UP, expected: THRUST_UP },
  { name: 'down', offset: THRUST_DOWN, expected: THRUST_DOWN },
  {
    name: 'upper right',
    offset: { x: 1, y: -1 },
    expected: { x: Math.SQRT1_2, y: -Math.SQRT1_2 },
  },
  {
    name: 'lower left',
    offset: { x: -1, y: 1 },
    expected: { x: -Math.SQRT1_2, y: Math.SQRT1_2 },
  },
]);

/** Same pointer, changed projected centre: a camera/body update without another pointer event. */
export const THRUST_STATIONARY_POINTER_UP: ThrustAimObservation = Object.freeze(
  {
    pointer: thrustAim().pointer,
    projectedBodyCenter: { x: 400, y: 300 },
    cameraGestureActive: false,
  },
);

export const THRUST_INVALID_AIMS: readonly ThrustAimObservation[] =
  Object.freeze([
    { ...thrustAim(), pointer: { x: Number.NaN, y: 200 } },
    {
      ...thrustAim(),
      projectedBodyCenter: { x: 300, y: Number.POSITIVE_INFINITY },
    },
    {
      ...thrustAim(),
      pointer: { x: Number.MAX_VALUE, y: 200 },
      projectedBodyCenter: { x: -Number.MAX_VALUE, y: 200 },
    },
  ]);

/** Validated welcome identities deliberately retain equal numeric IDs across fresh calls. */
export function thrustInputOptions(
  sendCommand: SimulationCommandSender,
  overrides: Partial<ThrustInputOptions> = {},
): ThrustInputOptions {
  const session = cameraSessionIdentity();
  return {
    enabled: true,
    session,
    ownEntityId: session.firstEntityId,
    inputLocked: false,
    inputGeneration: undefined,
    sendCommand,
    ...overrides,
  };
}
