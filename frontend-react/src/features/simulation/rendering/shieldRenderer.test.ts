import { describe, expect, it, vi } from 'vitest';

import {
  CANCELLED_SHIELD_WINDOWS,
  SHIELD_ACTIVATION_TICK,
  SHIELD_COOLDOWN_EXPIRY_TICK,
  SHIELD_SNAPSHOT_TICK,
  SHIELD_WINDOWS,
} from '../fixtures/shieldFrames';
import type {
  SessionEntitySnapshot,
  SessionPhysicsBodyComponent,
  SessionShieldComponent,
} from '../simulationProtocolTypes';
import type { EntityRenderFrame } from './entityRendering';
import {
  SHIELD_PERFECT_RING_COLOR,
  SHIELD_PROTECTION_RING_COLOR,
  drawShield,
} from './shieldRenderer';
import {
  createWorldProjection,
  projectWorldDistance,
  projectWorldPoint,
} from './worldProjection';

/**
 * These tests call `drawShield` directly rather than through `entityRendererRegistry.shield`.
 * Whether the kind is registered, and on which layer it sorts, is one fact asserted once in
 * `entityRendererRegistry.test.ts`; what is asserted here is the reading of three published windows
 * against one snapshot tick, which is the part a wrong answer turns into a lie on the canvas.
 */

const SHIELDED_BODY: SessionPhysicsBodyComponent = {
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
 * behind it, so a projection that throws costs the whole application rather than one ring.
 */
const NON_FINITE_POSITION_BODY = {
  ...SHIELDED_BODY,
  position: { x: Number.POSITIVE_INFINITY, y: 30 },
} as unknown as SessionPhysicsBodyComponent;
const NAN_RADIUS_BODY = {
  ...SHIELDED_BODY,
  radius: Number.NaN,
} as unknown as SessionPhysicsBodyComponent;
const NEGATIVE_RADIUS_BODY = {
  ...SHIELDED_BODY,
  radius: -1,
} as unknown as SessionPhysicsBodyComponent;

const PROJECTION = createWorldProjection(
  { x: 0, y: 0 },
  { width: 200, height: 200 },
  1,
);
const CENTER = projectWorldPoint(PROJECTION, SHIELDED_BODY.position);
const RADIUS_PIXELS = projectWorldDistance(PROJECTION, SHIELDED_BODY.radius);

interface StrokedRing {
  readonly x: number;
  readonly y: number;
  readonly radius: number;
  readonly start: number;
  readonly end: number;
  readonly style: string;
  readonly width: number;
}

/** The gaps and widths are hard-coded so the assertion is the drawing, not a copy of a constant. */
function ring(gapPixels: number, style: string, width: number): StrokedRing {
  return {
    x: CENTER.x,
    y: CENTER.y,
    radius: RADIUS_PIXELS + gapPixels,
    start: 0,
    end: 2 * Math.PI,
    style,
    width,
  };
}

/** The perfect pair straddles the single radius the ordinary ring occupies. */
const PERFECT_RINGS: readonly StrokedRing[] = [
  ring(4, SHIELD_PERFECT_RING_COLOR, 3),
  ring(10, SHIELD_PERFECT_RING_COLOR, 3),
];
const PROTECTION_RINGS: readonly StrokedRing[] = [
  ring(7, SHIELD_PROTECTION_RING_COLOR, 3),
];

function createSurface() {
  const rings: StrokedRing[] = [];
  let pending: Omit<StrokedRing, 'style' | 'width'> | null = null;
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
        throw new Error('TEST.SHIELD_STROKE_WITHOUT_PATH');
      }
      rings.push({
        ...pending,
        style: surface.strokeStyle,
        width: surface.lineWidth,
      });
    }),
    strokeStyle: '',
  };
  return { rings, surface };
}

interface DrawOptions {
  /** `null` publishes an entity with no `physics_body` at all; omitted uses the shielded body. */
  readonly body?: SessionPhysicsBodyComponent | null | undefined;
  readonly isOwnEntity?: boolean;
}

function drawAt(
  shield: SessionShieldComponent,
  tickSequence: number | null,
  options: DrawOptions = {},
): readonly StrokedRing[] {
  const isOwnEntity = options.isOwnEntity ?? false;
  const body = options.body === undefined ? SHIELDED_BODY : options.body;
  const { rings, surface } = createSurface();
  const entity: SessionEntitySnapshot = {
    entity_id: 7,
    components: body === null ? { shield } : { shield, physics_body: body },
  };
  const before = structuredClone(entity);
  const frame: EntityRenderFrame = {
    eliminationGraceTicks: null,
    ownEntityId: isOwnEntity ? entity.entity_id : null,
    projection: PROJECTION,
    surface: surface as unknown as CanvasRenderingContext2D,
    tickSequence,
  };

  drawShield({ component: shield, entity, frame, isOwnEntity });

  // The snapshot is shared with every other renderer in the frame and with the HUD.
  expect(entity).toEqual(before);
  // The dash pattern is `drawLethalOnContact`'s vocabulary and `globalAlpha` would tint every
  // renderer that ran afterwards; this renderer touches neither, so it needs no save/restore pair.
  expect(surface.setLineDash).not.toHaveBeenCalled();
  expect(surface.globalAlpha).toBe(1);
  expect(surface.save.mock.calls.length).toBe(
    surface.restore.mock.calls.length,
  );
  return rings;
}

/** Every state this renderer can be handed, including the ones no validated frame can produce. */
const NEVER_THROWS_CASES: readonly {
  readonly name: string;
  readonly shield: SessionShieldComponent;
  readonly tick: number | null;
  readonly body?: SessionPhysicsBodyComponent | null;
}[] = [
  {
    name: 'the perfect opening',
    shield: SHIELD_WINDOWS,
    tick: SHIELD_ACTIVATION_TICK,
  },
  {
    name: 'ordinary protection',
    shield: SHIELD_WINDOWS,
    tick: SHIELD_SNAPSHOT_TICK,
  },
  {
    name: 'an expired window under a live cooldown',
    shield: SHIELD_WINDOWS,
    tick: SHIELD_WINDOWS.shield_expiry_tick,
  },
  {
    name: 'a cancelled window',
    shield: CANCELLED_SHIELD_WINDOWS,
    tick: SHIELD_ACTIVATION_TICK,
  },
  { name: 'a frame with no tick', shield: SHIELD_WINDOWS, tick: null },
  {
    name: 'a bodyless entity',
    shield: SHIELD_WINDOWS,
    tick: SHIELD_SNAPSHOT_TICK,
    body: null,
  },
  {
    name: 'an unprojectable position',
    shield: SHIELD_WINDOWS,
    tick: SHIELD_SNAPSHOT_TICK,
    body: NON_FINITE_POSITION_BODY,
  },
  {
    name: 'a NaN radius',
    shield: SHIELD_WINDOWS,
    tick: SHIELD_SNAPSHOT_TICK,
    body: NAN_RADIUS_BODY,
  },
  {
    name: 'a negative radius',
    shield: SHIELD_WINDOWS,
    tick: SHIELD_SNAPSHOT_TICK,
    body: NEGATIVE_RADIUS_BODY,
  },
  {
    name: 'endpoints at the maximum exact integer',
    shield: {
      ...SHIELD_WINDOWS,
      shield_expiry_tick: Number.MAX_SAFE_INTEGER,
      perfect_expiry_tick: Number.MAX_SAFE_INTEGER,
      cooldown_expiry_tick: Number.MAX_SAFE_INTEGER,
    },
    tick: SHIELD_SNAPSHOT_TICK,
  },
];

describe('shieldRenderer', () => {
  it.each([
    { name: 'the activation tick itself', tick: SHIELD_ACTIVATION_TICK },
    {
      name: 'the last tick the opening covers',
      tick: SHIELD_WINDOWS.perfect_expiry_tick - 1,
    },
  ])('marks the perfect opening at $name', ({ tick }) => {
    expect(drawAt(SHIELD_WINDOWS, tick)).toEqual(PERFECT_RINGS);
  });

  it.each([
    {
      // Half-open: the tick the opening expires on is the first ordinary tick, not its last
      // perfect one. Drawing the flash here would overstate a parry window by a whole tick.
      name: 'the exact tick the perfect opening expires',
      tick: SHIELD_WINDOWS.perfect_expiry_tick,
    },
    { name: "the golden snapshot's own tick", tick: SHIELD_SNAPSHOT_TICK },
    {
      name: 'the last tick protection covers',
      tick: SHIELD_WINDOWS.shield_expiry_tick - 1,
    },
  ])('marks ordinary protection at $name', ({ tick }) => {
    expect(drawAt(SHIELD_WINDOWS, tick)).toEqual(PROTECTION_RINGS);
  });

  it.each([
    {
      // The third state, and the majority of published shield frames by duration: 160 protected
      // ticks inside a component that lives at least 360. A live cooldown is not protection.
      name: 'the exact tick protection expires, with the cooldown still live',
      shield: SHIELD_WINDOWS,
      tick: SHIELD_WINDOWS.shield_expiry_tick,
    },
    {
      name: 'the last tick of a live cooldown that outlived its protection',
      shield: SHIELD_WINDOWS,
      tick: SHIELD_COOLDOWN_EXPIRY_TICK - 1,
    },
    {
      // A stun cancelled this pulse on the tick it began. Zero-length protection with a live
      // cooldown is a frame the server is required to be able to send, and it protects nobody.
      name: 'a cancelled shield on its own activation tick',
      shield: CANCELLED_SHIELD_WINDOWS,
      tick: SHIELD_ACTIVATION_TICK,
    },
    {
      name: 'a cancelled shield later inside its surviving cooldown',
      shield: CANCELLED_SHIELD_WINDOWS,
      tick: SHIELD_SNAPSHOT_TICK,
    },
    {
      // Unreachable on a validated frame -- `sessionProtocolValidation` rejects an activation past
      // the snapshot tick -- and asserted anyway, because it pins the lower bound as inclusive.
      name: 'a tick before the activation',
      shield: SHIELD_WINDOWS,
      tick: SHIELD_ACTIVATION_TICK - 1,
    },
  ])('draws nothing at $name', ({ shield, tick }) => {
    expect(drawAt(shield, tick)).toEqual([]);
  });

  it('draws nothing when the frame publishes no tick, because presence is not protection', () => {
    expect(drawAt(SHIELD_WINDOWS, null)).toEqual([]);
  });

  it('returns silently when the shielded entity publishes no body this frame', () => {
    // Step 18 declined a `dependentRequired: ["physics_body"]` edge on `shield` so that this tick
    // is an ordinary frame rather than a client-wide 1003 close. It must also not be a throw.
    const rings = drawAt(SHIELD_WINDOWS, SHIELD_SNAPSHOT_TICK, { body: null });
    expect(rings).toEqual([]);
  });

  it.each([
    { name: 'a non-finite position', body: NON_FINITE_POSITION_BODY },
    { name: 'a NaN radius', body: NAN_RADIUS_BODY },
    { name: 'a negative radius', body: NEGATIVE_RADIUS_BODY },
  ])('never hands the projection $name', ({ body }) => {
    expect(drawAt(SHIELD_WINDOWS, SHIELD_SNAPSHOT_TICK, { body })).toEqual([]);
  });

  it('draws protection identically on the own blob and on a peer', () => {
    // Exposure splits its colour by owner because it answers "am I in trouble". Protection is
    // information about a target you are deciding whether to ram, so it reads the same on everyone.
    const own = drawAt(SHIELD_WINDOWS, SHIELD_SNAPSHOT_TICK, {
      isOwnEntity: true,
    });
    expect(own).toEqual(drawAt(SHIELD_WINDOWS, SHIELD_SNAPSHOT_TICK));
    const ownPerfect = drawAt(SHIELD_WINDOWS, SHIELD_ACTIVATION_TICK, {
      isOwnEntity: true,
    });
    expect(ownPerfect).toEqual(drawAt(SHIELD_WINDOWS, SHIELD_ACTIVATION_TICK));
  });

  it.each(NEVER_THROWS_CASES)(
    'never throws for $name',
    ({ shield, tick, body }) => {
      // There is no error boundary anywhere in this client and the draw loop is an unguarded
      // `useEffect`, so one throw from one renderer blanks the whole application.
      expect(() => drawAt(shield, tick, { body })).not.toThrow();
    },
  );
});
