import { describe, expect, it, vi } from 'vitest';

import {
  STUN_INPUT_SNAPSHOT_TICK,
  STUN_INPUT_WINDOW,
} from '../fixtures/stunInputFrames';
import type {
  SessionEntitySnapshot,
  SessionPhysicsBodyComponent,
  SessionStunComponent,
} from '../simulationProtocolTypes';
import type { EntityRenderFrame } from './entityRendering';
import { STUN_ARC_COLOR, drawStun } from './stunRenderer';
import {
  createWorldProjection,
  projectWorldDistance,
  projectWorldPoint,
} from './worldProjection';

/**
 * These tests call `drawStun` directly rather than through `entityRendererRegistry.stun`. Whether
 * the kind is registered, and on which layer it sorts, is one fact asserted once in
 * `entityRendererRegistry.test.ts`; what is asserted here is the reading of one published window
 * against one snapshot tick, which is the part a wrong answer turns into a lie on the canvas.
 *
 * `STUN_INPUT_WINDOW` is reused rather than restated because it is the corpus the input-lock rules
 * already read: the mark on the canvas and the lock on the keyboard must agree about which ticks
 * this window covers, and they cannot disagree if they are read from the same numbers.
 */

const STUNNED_BODY: SessionPhysicsBodyComponent = {
  position: { x: 20, y: 30 },
  velocity: { x: 0, y: 0 },
  acceleration: { x: 0, y: 0 },
  radius: 10,
  mass: 4,
  collision_layer: 1,
  collision_mask: 1,
  is_static: false,
  ground_attachment: 'ground_bound',
};

/**
 * Bodies Ajv rejects, reached here through a cast because the static types forbid them too. The
 * renderer guards them anyway: the draw loop is an unguarded `useEffect` with no error boundary
 * behind it, so a projection that throws costs the whole application rather than one mark.
 */
const NON_FINITE_POSITION_BODY = {
  ...STUNNED_BODY,
  position: { x: 20, y: Number.NEGATIVE_INFINITY },
} as unknown as SessionPhysicsBodyComponent;
const NAN_RADIUS_BODY = {
  ...STUNNED_BODY,
  radius: Number.NaN,
} as unknown as SessionPhysicsBodyComponent;
const NEGATIVE_RADIUS_BODY = {
  ...STUNNED_BODY,
  radius: -1,
} as unknown as SessionPhysicsBodyComponent;

const PROJECTION = createWorldProjection(
  { x: 0, y: 0 },
  { width: 200, height: 200 },
  1,
);
const CENTER = projectWorldPoint(PROJECTION, STUNNED_BODY.position);
const RADIUS_PIXELS = projectWorldDistance(PROJECTION, STUNNED_BODY.radius);
/** Hard-coded so the assertion is the drawing rather than a copy of a private constant. */
const ARC_GAP_PIXELS = 5;
const ARC_WIDTH_PIXELS = 4;

interface StrokedArc {
  readonly x: number;
  readonly y: number;
  readonly radius: number;
  readonly start: number;
  readonly end: number;
  readonly style: string;
  readonly width: number;
}

function createSurface() {
  const arcs: StrokedArc[] = [];
  let pending: Omit<StrokedArc, 'style' | 'width'> | null = null;
  const surface = {
    arc: vi.fn<
      (x: number, y: number, radius: number, start: number, end: number) => void
    >((x, y, radius, start, end) => {
      pending = { x, y, radius, start, end };
    }),
    beginPath: vi.fn(() => {
      pending = null;
    }),
    globalAlpha: 1,
    lineWidth: 1,
    restore: vi.fn(),
    save: vi.fn(),
    setLineDash: vi.fn<(segments: readonly number[]) => void>(),
    stroke: vi.fn(() => {
      if (pending === null) {
        throw new Error('TEST.STUN_STROKE_WITHOUT_PATH');
      }
      arcs.push({
        ...pending,
        style: surface.strokeStyle,
        width: surface.lineWidth,
      });
    }),
    strokeStyle: '',
  };
  return { arcs, surface };
}

interface DrawOptions {
  /** `null` publishes an entity with no `physics_body` at all; omitted uses the stunned body. */
  readonly body?: SessionPhysicsBodyComponent | null | undefined;
  readonly isOwnEntity?: boolean;
}

function drawAt(
  stun: SessionStunComponent,
  tickSequence: number | null,
  options: DrawOptions = {},
): readonly StrokedArc[] {
  const isOwnEntity = options.isOwnEntity ?? false;
  const body = options.body === undefined ? STUNNED_BODY : options.body;
  const { arcs, surface } = createSurface();
  const entity: SessionEntitySnapshot = {
    entity_id: 7,
    components: body === null ? { stun } : { stun, physics_body: body },
  };
  const before = structuredClone(entity);
  const frame: EntityRenderFrame = {
    eliminationGraceTicks: null,
    ownEntityId: isOwnEntity ? entity.entity_id : null,
    projection: PROJECTION,
    surface: surface as unknown as CanvasRenderingContext2D,
    tickSequence,
  };

  drawStun({ component: stun, entity, frame, isOwnEntity });

  // The snapshot is shared with every other renderer in the frame and with the HUD.
  expect(entity).toEqual(before);
  // A dash already means "this object kills" and `globalAlpha` would tint every renderer that ran
  // afterwards; this renderer touches neither, so it needs no save/restore pair either.
  expect(surface.setLineDash).not.toHaveBeenCalled();
  expect(surface.globalAlpha).toBe(1);
  expect(surface.save.mock.calls.length).toBe(
    surface.restore.mock.calls.length,
  );
  return arcs;
}

/** Every state this renderer can be handed, including the ones no validated frame can produce. */
const NEVER_THROWS_CASES: readonly {
  readonly name: string;
  readonly stun: SessionStunComponent;
  readonly tick: number | null;
  readonly body?: SessionPhysicsBodyComponent | null;
}[] = [
  {
    name: 'a running stun',
    stun: STUN_INPUT_WINDOW,
    tick: STUN_INPUT_SNAPSHOT_TICK,
  },
  {
    name: 'the exact tick the window expires',
    stun: STUN_INPUT_WINDOW,
    tick: STUN_INPUT_WINDOW.expiry_tick,
  },
  { name: 'a frame with no tick', stun: STUN_INPUT_WINDOW, tick: null },
  {
    name: 'a bodyless entity',
    stun: STUN_INPUT_WINDOW,
    tick: STUN_INPUT_SNAPSHOT_TICK,
    body: null,
  },
  {
    name: 'an unprojectable position',
    stun: STUN_INPUT_WINDOW,
    tick: STUN_INPUT_SNAPSHOT_TICK,
    body: NON_FINITE_POSITION_BODY,
  },
  {
    name: 'a NaN radius',
    stun: STUN_INPUT_WINDOW,
    tick: STUN_INPUT_SNAPSHOT_TICK,
    body: NAN_RADIUS_BODY,
  },
  {
    name: 'a negative radius',
    stun: STUN_INPUT_WINDOW,
    tick: STUN_INPUT_SNAPSHOT_TICK,
    body: NEGATIVE_RADIUS_BODY,
  },
  {
    name: 'an expiry at the maximum exact integer',
    stun: {
      activation_tick: STUN_INPUT_WINDOW.activation_tick,
      expiry_tick: Number.MAX_SAFE_INTEGER,
    },
    tick: STUN_INPUT_SNAPSHOT_TICK,
  },
];

describe('stunRenderer', () => {
  it('breaks the ring into four equal arcs centred in their quarters', () => {
    const arcs = drawAt(STUN_INPUT_WINDOW, STUN_INPUT_SNAPSHOT_TICK);
    expect(arcs).toHaveLength(4);
    arcs.forEach((arc, index) => {
      expect(arc.x).toBe(CENTER.x);
      expect(arc.y).toBe(CENTER.y);
      expect(arc.radius).toBe(RADIUS_PIXELS + ARC_GAP_PIXELS);
      expect(arc.style).toBe(STUN_ARC_COLOR);
      expect(arc.width).toBe(ARC_WIDTH_PIXELS);
      // Sixty degrees of the ninety each quarter owns, centred on that quarter's bisector, so the
      // four gaps are equal and the break reads the same however the blob is approached. The
      // angles are compared by their meaning rather than by re-running the renderer's expression.
      expect(arc.end - arc.start).toBeCloseTo(Math.PI / 3, 12);
      expect((arc.start + arc.end) / 2).toBeCloseTo(
        index * (Math.PI / 2) + Math.PI / 4,
        12,
      );
    });
  });

  it.each([
    {
      // The lower bound is inclusive: the activation tick is already stunned.
      name: 'the activation tick itself',
      tick: STUN_INPUT_WINDOW.activation_tick,
    },
    {
      name: 'the last tick the window covers',
      tick: STUN_INPUT_WINDOW.expiry_tick - 1,
    },
  ])('marks a running stun at $name', ({ tick }) => {
    expect(drawAt(STUN_INPUT_WINDOW, tick)).toHaveLength(4);
  });

  it.each([
    {
      // Half-open, exactly as `selectThrustInputOptions` reads the same two members: the expiry
      // tick is the first tick the player has their controls back, not their last locked one.
      name: 'the exact tick the window expires',
      tick: STUN_INPUT_WINDOW.expiry_tick,
    },
    {
      name: 'a tick after the window expired',
      tick: STUN_INPUT_WINDOW.expiry_tick + 400,
    },
    {
      // Unreachable on a validated frame -- an activation may not exceed the snapshot tick -- and
      // asserted anyway, because it pins the lower bound as inclusive rather than exclusive.
      name: 'a tick before the activation',
      tick: STUN_INPUT_WINDOW.activation_tick - 1,
    },
  ])('draws nothing at $name', ({ tick }) => {
    expect(drawAt(STUN_INPUT_WINDOW, tick)).toEqual([]);
  });

  it('draws nothing when the frame publishes no tick, because presence is not a stun', () => {
    expect(drawAt(STUN_INPUT_WINDOW, null)).toEqual([]);
  });

  it('returns silently when the stunned entity publishes no body this frame', () => {
    // `stun` takes no `dependentRequired: ["physics_body"]` edge, so a tick in which the component
    // outlives the body is an ordinary frame rather than a client-wide 1003 close, and it must not
    // be a throw either.
    const arcs = drawAt(STUN_INPUT_WINDOW, STUN_INPUT_SNAPSHOT_TICK, {
      body: null,
    });
    expect(arcs).toEqual([]);
  });

  it.each([
    { name: 'a non-finite position', body: NON_FINITE_POSITION_BODY },
    { name: 'a NaN radius', body: NAN_RADIUS_BODY },
    { name: 'a negative radius', body: NEGATIVE_RADIUS_BODY },
  ])('never hands the projection $name', ({ body }) => {
    const arcs = drawAt(STUN_INPUT_WINDOW, STUN_INPUT_SNAPSHOT_TICK, { body });
    expect(arcs).toEqual([]);
  });

  it('keeps drawing when a merge grows the window in place', () => {
    // A merge preserves the activation and takes the maximum expiry, so the interval can get
    // longer between two frames. Nothing here remembers the old one, so the mark simply persists.
    const merged: SessionStunComponent = {
      activation_tick: STUN_INPUT_WINDOW.activation_tick,
      expiry_tick: STUN_INPUT_WINDOW.expiry_tick + 240,
    };
    expect(drawAt(merged, STUN_INPUT_WINDOW.expiry_tick)).toHaveLength(4);
    expect(drawAt(merged, STUN_INPUT_SNAPSHOT_TICK)).toEqual(
      drawAt(STUN_INPUT_WINDOW, STUN_INPUT_SNAPSHOT_TICK),
    );
  });

  it('marks the own blob and a peer identically', () => {
    // What the canvas says is "this body cannot fight back", which is the same fact about anyone.
    // Whether the local keyboard is locked is the HUD's business, not this renderer's.
    const own = drawAt(STUN_INPUT_WINDOW, STUN_INPUT_SNAPSHOT_TICK, {
      isOwnEntity: true,
    });
    expect(own).toEqual(drawAt(STUN_INPUT_WINDOW, STUN_INPUT_SNAPSHOT_TICK));
  });

  it.each(NEVER_THROWS_CASES)(
    'never throws for $name',
    ({ stun, tick, body }) => {
      // There is no error boundary anywhere in this client and the draw loop is an unguarded
      // `useEffect`, so one throw from one renderer blanks the whole application.
      expect(() => drawAt(stun, tick, { body })).not.toThrow();
    },
  );
});
