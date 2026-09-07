import { describe, expect, it, vi } from 'vitest';

import { protocolV2Schemas } from '../generated/protocolV2Schemas.generated';
import type { SessionEntitySnapshot } from '../simulationProtocolTypes';
import {
  entityRendererRegistry,
  visualEntityRenderers,
} from './entityRendererRegistry';
import type { EntityRenderFrame } from './entityRendering';
import {
  EXPOSED_OWN_RING_COLOR,
  EXPOSED_PEER_RING_COLOR,
} from './zoneExposureRenderer';

function createFrame(ownEntityId: number | null = null): {
  readonly arc: ReturnType<typeof vi.fn>;
  readonly arcRadii: readonly number[];
  readonly fillText: ReturnType<typeof vi.fn>;
  readonly frame: EntityRenderFrame;
} {
  // The radii are captured through a typed implementation rather than read back out of
  // `arc.mock.calls`, whose recorded arguments are erased to `any` on an untyped spy.
  const arcRadii: number[] = [];
  const arc = vi.fn((x: number, y: number, radius: number) => {
    void x;
    void y;
    arcRadii.push(radius);
  });
  const fillText = vi.fn();
  const surface = {
    arc,
    beginPath: vi.fn(),
    fill: vi.fn(),
    fillStyle: '',
    fillText,
    font: '',
    lineWidth: 1,
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
      ownEntityId,
      projection: { horizontalScale: 1, verticalScale: 1 },
      surface,
    },
  };
}

const zoneEntity: SessionEntitySnapshot = {
  entity_id: 9,
  components: { zone: { center: { x: 10, y: 10 }, radius: 5 } },
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

  it('draws the zone beneath bodies and names above them', () => {
    // `zone_exposure` joins between the body and the label: a danger ring painted under the disc
    // would be hidden by it, and one painted over the name would strike the name through.
    expect(visualEntityRenderers().map((renderer) => renderer.kind)).toEqual([
      'zone',
      'physics_body',
      'zone_exposure',
      'controllable',
    ]);
  });

  it('draws a component only for the entities that carry it', () => {
    const { arc, frame } = createFrame();

    entityRendererRegistry.zone.drawEntity(zoneEntity, frame);
    expect(arc).toHaveBeenCalledTimes(1);

    entityRendererRegistry.zone.drawEntity(bodilessControllerEntity, frame);
    entityRendererRegistry.physics_body.drawEntity(zoneEntity, frame);
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

  it('does not ring an exposure whose body is absent this frame', () => {
    const { arc, frame } = createFrame();

    entityRendererRegistry.zone_exposure.drawEntity(
      bodilessExposureEntity,
      frame,
    );

    expect(arc).not.toHaveBeenCalled();
  });
});
