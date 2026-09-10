import { describe, expect, it, vi } from 'vitest';

import { protocolV2Schemas } from '../generated/protocolV2Schemas.generated';
import type { SessionEntitySnapshot } from '../simulationProtocolTypes';
import {
  entityRendererRegistry,
  visualEntityRenderers,
} from './entityRendererRegistry';
import type { EntityRenderFrame } from './entityRendering';
import { HILL_FILL, HILL_STROKE } from './hillRenderer';
import { LETHAL_HAZARD_RING_COLOR } from './lethalOnContactRenderer';
import {
  EXPOSED_OWN_RING_COLOR,
  EXPOSED_PEER_RING_COLOR,
} from './zoneExposureRenderer';

function createFrame(
  ownEntityId: number | null = null,
  eliminationGraceTicks: number | null = null,
): {
  readonly arc: ReturnType<typeof vi.fn>;
  readonly arcRadii: readonly number[];
  readonly fillText: ReturnType<typeof vi.fn>;
  readonly frame: EntityRenderFrame;
  readonly lineDashCalls: readonly (readonly number[])[];
  readonly restore: ReturnType<typeof vi.fn>;
  readonly surface: CanvasRenderingContext2D;
} {
  // The radii are captured through a typed implementation rather than read back out of
  // `arc.mock.calls`, whose recorded arguments are erased to `any` on an untyped spy.
  const arcRadii: number[] = [];
  const lineDashCalls: number[][] = [];
  const arc = vi.fn((x: number, y: number, radius: number) => {
    void x;
    void y;
    arcRadii.push(radius);
  });
  const fillText = vi.fn();
  // `setLineDash`, `save` and `restore` are on the mock because a renderer that dashes must be able
  // to put the surface back: the canvas is shared with every later renderer in the frame, and a
  // leaked dash pattern would turn the next solid ring into a dotted one.
  const setLineDash = vi.fn((segments: readonly number[]) => {
    lineDashCalls.push([...segments]);
  });
  const restore = vi.fn();
  const surface = {
    arc,
    beginPath: vi.fn(),
    fill: vi.fn(),
    fillStyle: '',
    fillText,
    font: '',
    lineWidth: 1,
    restore,
    save: vi.fn(),
    setLineDash,
    stroke: vi.fn(),
    strokeStyle: '',
    textAlign: '',
    textBaseline: '',
  } as unknown as CanvasRenderingContext2D;

  return {
    arc,
    arcRadii,
    fillText,
    frame: {
      // Defaults to `null`, which is the "this frame published no grace" case, so every test
      // written before the grace reached the wire keeps asserting the drawing it always asserted.
      eliminationGraceTicks,
      ownEntityId,
      projection: { horizontalScale: 1, verticalScale: 1 },
      surface,
    },
    lineDashCalls,
    restore,
    surface,
  };
}

/**
 * Draws through the registry entry rather than importing the renderer directly, so these tests fail
 * if the kind is ever unregistered or demoted to non-visual -- which is the failure that would
 * actually reach a player, not a broken drawing function.
 */
function drawHazard(
  entity: SessionEntitySnapshot,
  surface: CanvasRenderingContext2D,
): void {
  const registration = entityRendererRegistry.lethal_on_contact;
  if (!registration.renders) {
    throw new Error(
      'lethal_on_contact must be registered as a visual renderer',
    );
  }
  registration.drawEntity(entity, {
    eliminationGraceTicks: null,
    ownEntityId: null,
    projection: { horizontalScale: 1, verticalScale: 1 },
    surface,
  });
}

const zoneEntity: SessionEntitySnapshot = {
  entity_id: 9,
  components: { zone: { center: { x: 10, y: 10 }, radius: 5 } },
};

const hillEntity: SessionEntitySnapshot = {
  entity_id: 12,
  components: { hill: { center: { x: 200, y: 150 }, radius: 90 } },
};

const bodilessControllerEntity: SessionEntitySnapshot = {
  entity_id: 11,
  components: {
    controllable: {
      controller_id: 4,
      controller_kind: 'session',
      display_name: 'player-4',
    },
  },
};

function blobEntity(
  entityId: number,
  outsideTicks: number,
): SessionEntitySnapshot {
  return {
    entity_id: entityId,
    components: {
      physics_body: {
        acceleration: { x: 0, y: 0 },
        collision_layer: 1,
        collision_mask: 3,
        is_static: false,
        mass: 1,
        position: { x: 120, y: 80 },
        radius: 10,
        velocity: { x: 0, y: 0 },
      },
      zone_exposure: { outside_ticks: outsideTicks },
    },
  };
}

const bodilessExposureEntity: SessionEntitySnapshot = {
  entity_id: 12,
  components: { zone_exposure: { outside_ticks: 400 } },
};

describe('entityRendererRegistry', () => {
  it('registers every component kind the accepted schema set names, and no other', () => {
    expect(Object.keys(entityRendererRegistry).sort()).toEqual(
      [...protocolV2Schemas.common.$defs.component_kind.enum].sort(),
    );
  });

  it('states a reason for every component kind it deliberately does not draw', () => {
    for (const registration of Object.values(entityRendererRegistry)) {
      if (registration.renders) {
        expect(typeof registration.layer).toBe('number');
        continue;
      }
      expect(registration.reason.length).toBeGreaterThan(20);
    }
  });

  it('registers race progress as non-visual because its gate index has no geometry', () => {
    expect(entityRendererRegistry.race_progress.renders).toBe(false);
  });

  it('draws the zone beneath bodies and names above them', () => {
    // Both danger rings join between the body and the label: one painted under the disc would be
    // hidden by it, and one painted over the name would strike the name through.
    //
    // `lethal_on_contact` sorts before `zone_exposure` because a hazard's ring is the warning a
    // player has least time to act on, so it must never be the one that gets overdrawn. The two
    // never land on one entity today -- a hazard carries no exposure counter -- but the order is
    // pinned here so that stops being an accident if one ever does.
    expect(visualEntityRenderers().map((renderer) => renderer.kind)).toEqual([
      'hill',
      'zone',
      'physics_body',
      'lethal_on_contact',
      'zone_exposure',
      'controllable',
    ]);
  });

  it('rings a lethal hazard outside its own radius, dashed, and restores the surface', () => {
    // A hazard is a body like any other, so the ring has to come from `physics_body`: the marker
    // publishes `{}` and carries no geometry at all.
    const hazard: SessionEntitySnapshot = {
      entity_id: 21,
      components: {
        lethal_on_contact: {},
        physics_body: {
          position: { x: 40, y: 40 },
          velocity: { x: -90, y: 0 },
          acceleration: { x: 0, y: 0 },
          radius: 26,
          mass: 40,
          collision_layer: 1,
          collision_mask: 1,
          is_static: false,
        },
      },
    };
    const { arcRadii, lineDashCalls, restore, surface } = createFrame();

    drawHazard(hazard, surface);

    // Outside the body, so the hazard stays legible underneath rather than being repainted.
    expect(arcRadii.every((radius) => radius > 26)).toBe(true);
    expect(surface.strokeStyle).toBe(LETHAL_HAZARD_RING_COLOR);
    // Dashed, which is what distinguishes "this kills you" from the solid zone-exposure ring. The
    // two never land on one entity today but they share a frame constantly.
    expect(lineDashCalls.length).toBeGreaterThan(0);
    expect(lineDashCalls[0]?.length).toBeGreaterThan(0);
    // Restored, so the dash cannot leak into the next renderer's solid ring.
    expect(restore).toHaveBeenCalled();
  });

  it('draws no hazard ring for a lethal marker whose body is gone', () => {
    // The server destroyed the entity, or has not placed it yet. Ringing the origin would paint a
    // threat where nothing is standing.
    const bodiless: SessionEntitySnapshot = {
      entity_id: 22,
      components: { lethal_on_contact: {} },
    };
    const { arc, surface } = createFrame();

    drawHazard(bodiless, surface);

    expect(arc).not.toHaveBeenCalled();
  });

  it('draws a component only for the entities that carry it', () => {
    const { arc, frame } = createFrame();

    entityRendererRegistry.zone.drawEntity(zoneEntity, frame);
    expect(arc).toHaveBeenCalledTimes(1);

    entityRendererRegistry.zone.drawEntity(bodilessControllerEntity, frame);
    entityRendererRegistry.physics_body.drawEntity(zoneEntity, frame);
    expect(arc).toHaveBeenCalledTimes(1);
  });

  it('draws the hill as one filled disc at its published centre and radius', () => {
    const { arc, arcRadii, frame, surface } = createFrame();

    entityRendererRegistry.hill.drawEntity(hillEntity, frame);

    expect(arc).toHaveBeenCalledTimes(1);
    expect(arcRadii).toEqual([90]);
    expect(surface.fillStyle).toBe(HILL_FILL);
    expect(surface.strokeStyle).toBe(HILL_STROKE);
    // The hill and the zone share a layer and are told apart by kind, never by geometry: a hill
    // entity carries no zone and draws nothing through the zone renderer.
    entityRendererRegistry.zone.drawEntity(hillEntity, frame);
    expect(arc).toHaveBeenCalledTimes(1);
  });

  it('does not label a controller whose body is absent this frame', () => {
    const { fillText, frame } = createFrame();

    entityRendererRegistry.controllable.drawEntity(
      bodilessControllerEntity,
      frame,
    );

    expect(fillText).not.toHaveBeenCalled();
  });

  it('leaves a blob inside the zone unmarked and rings one that is outside', () => {
    const safe = createFrame();
    entityRendererRegistry.zone_exposure.drawEntity(
      blobEntity(21, 0),
      safe.frame,
    );
    expect(safe.arc).not.toHaveBeenCalled();

    const exposed = createFrame();
    entityRendererRegistry.zone_exposure.drawEntity(
      blobEntity(21, 1),
      exposed.frame,
    );
    expect(exposed.arc).toHaveBeenCalledTimes(1);
    expect(exposed.frame.surface.strokeStyle).toBe(EXPOSED_PEER_RING_COLOR);
    // Outside the 10 wu body at unit scale, so the blob itself stays legible under the warning.
    expect(exposed.arcRadii).toEqual([13]);
  });

  it('marks the session own exposed blob differently from an exposed peer', () => {
    const peer = createFrame(99);
    const own = createFrame(21);

    entityRendererRegistry.zone_exposure.drawEntity(
      blobEntity(21, 240),
      peer.frame,
    );
    entityRendererRegistry.zone_exposure.drawEntity(
      blobEntity(21, 240),
      own.frame,
    );

    // Two concentric rings in the alarm colour, against one warning ring for a peer.
    expect(own.arc).toHaveBeenCalledTimes(2);
    expect(peer.arc).toHaveBeenCalledTimes(1);
    expect(own.frame.surface.strokeStyle).toBe(EXPOSED_OWN_RING_COLOR);
    expect(own.frame.surface.strokeStyle).not.toBe(
      peer.frame.surface.strokeStyle,
    );
    expect(new Set(own.arcRadii).size).toBe(2);
    expect(Math.max(...own.arcRadii)).toBeGreaterThan(
      Math.max(...peer.arcRadii),
    );
  });

  it('thickens the exposure ring as the published grace is spent', () => {
    // The denominator is `elimination_grace_ticks` from the frame's mode-state block. Before
    // protocol 2.2 published it there was none, and the ring deliberately did not ramp rather than
    // ramping against a guessed duration.
    const widths = [1, 600, 1_200].map((outsideTicks) => {
      const drawn = createFrame(null, 1_200);
      entityRendererRegistry.zone_exposure.drawEntity(
        blobEntity(21, outsideTicks),
        drawn.frame,
      );
      return drawn.frame.surface.lineWidth;
    });

    expect(widths[0]).toBeLessThan(widths[1] ?? 0);
    expect(widths[1]).toBeLessThan(widths[2] ?? 0);
  });

  it('draws the flat base ring when the frame publishes no grace to spend', () => {
    // `sandbox` publishes the `none` mode-state block, so its frames carry no grace at all. A ramp
    // there would be a guess; the honest drawing is the same fixed ring for every exposure.
    const brief = createFrame();
    const long = createFrame();
    entityRendererRegistry.zone_exposure.drawEntity(
      blobEntity(21, 1),
      brief.frame,
    );
    entityRendererRegistry.zone_exposure.drawEntity(
      blobEntity(21, 100_000),
      long.frame,
    );

    expect(brief.frame.surface.lineWidth).toBe(long.frame.surface.lineWidth);
  });

  it('saturates the ramp rather than dividing by a grace of zero or passing full', () => {
    // A grace of zero is a legal `[royale]` value: elimination on the first outside tick. And a
    // counter past its bound must clamp, not scale past the widest ring.
    const graceless = createFrame(null, 0);
    entityRendererRegistry.zone_exposure.drawEntity(
      blobEntity(21, 1),
      graceless.frame,
    );
    const spent = createFrame(null, 1_200);
    entityRendererRegistry.zone_exposure.drawEntity(
      blobEntity(21, 1_200),
      spent.frame,
    );
    const overshot = createFrame(null, 1_200);
    entityRendererRegistry.zone_exposure.drawEntity(
      blobEntity(21, 99_999),
      overshot.frame,
    );

    expect(Number.isFinite(graceless.frame.surface.lineWidth)).toBe(true);
    expect(graceless.frame.surface.lineWidth).toBe(
      spent.frame.surface.lineWidth,
    );
    expect(overshot.frame.surface.lineWidth).toBe(
      spent.frame.surface.lineWidth,
    );
  });

  it('does not ring an exposure whose body is absent this frame', () => {
    const { arc, frame } = createFrame();

    entityRendererRegistry.zone_exposure.drawEntity(
      bodilessExposureEntity,
      frame,
    );

    expect(arc).not.toHaveBeenCalled();
  });
});
