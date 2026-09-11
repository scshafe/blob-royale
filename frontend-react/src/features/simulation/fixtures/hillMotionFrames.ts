import { hillMotionComponentExample } from './protocolV3Examples';
import {
  hillSnapshotDocument,
  MAXIMUM_PUBLISHED_WORLD_SCALAR,
} from './sessionFrames';

/** Full hill frames keep the legacy mode state and geometry; only roam adds public velocity. */
export function hillMotionSnapshotDocument(
  motion: unknown = structuredClone(hillMotionComponentExample),
) {
  const document = hillSnapshotDocument();
  const hill = document.data.entities.find(
    (entity) => entity.components.hill !== undefined,
  );
  if (hill === undefined)
    throw new Error('TEST.HILL_MOTION_FIXTURE_HILL_MISSING');
  Reflect.set(hill.components, 'hill_motion', motion);
  return document;
}

export const acceptedHillMotionComponents = Object.freeze([
  { name: 'zero after cancellation', value: { velocity: { x: 0, y: 0 } } },
  { name: 'below sampled minimum', value: { velocity: { x: 0.000001, y: 0 } } },
  { name: 'golden signed velocity', value: hillMotionComponentExample },
  {
    name: 'inclusive component bounds',
    value: {
      velocity: {
        x: MAXIMUM_PUBLISHED_WORLD_SCALAR,
        y: -MAXIMUM_PUBLISHED_WORLD_SCALAR,
      },
    },
  },
]);

export const malformedHillMotionComponents: readonly {
  readonly name: string;
  readonly value: unknown;
}[] = [
  { name: 'missing velocity', value: {} },
  { name: 'null component', value: null },
  { name: 'null velocity', value: { velocity: null } },
  { name: 'missing x', value: { velocity: { y: 0 } } },
  { name: 'missing y', value: { velocity: { x: 0 } } },
  { name: 'string coordinate', value: { velocity: { x: '30', y: 0 } } },
  {
    name: 'extra vector coordinate',
    value: { velocity: { x: 0, y: 0, z: 0 } },
  },
  {
    name: 'positive bound overflow',
    value: { velocity: { x: MAXIMUM_PUBLISHED_WORLD_SCALAR + 1, y: 0 } },
  },
  {
    name: 'negative bound overflow',
    value: { velocity: { x: 0, y: -MAXIMUM_PUBLISHED_WORLD_SCALAR - 1 } },
  },
  { name: 'NaN', value: { velocity: { x: Number.NaN, y: 0 } } },
  {
    name: 'infinity',
    value: { velocity: { x: 0, y: Number.POSITIVE_INFINITY } },
  },
  { name: 'negative zero x', value: { velocity: { x: -0, y: 0 } } },
  { name: 'negative zero y', value: { velocity: { x: 0, y: -0 } } },
  {
    name: 'private schedule',
    value: {
      velocity: { x: 0, y: 0 },
      schedule: { next_retarget_tick: 140, random_stream: 'hill' },
    },
  },
  {
    name: 'private next retarget',
    value: { velocity: { x: 0, y: 0 }, next_retarget_tick: 140 },
  },
  {
    name: 'private stream',
    value: { velocity: { x: 0, y: 0 }, random_stream: 'hill' },
  },
];
