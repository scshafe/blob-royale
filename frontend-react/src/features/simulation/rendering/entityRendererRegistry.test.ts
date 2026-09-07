import { describe, expect, it, vi } from 'vitest';

import { protocolV2Schemas } from '../generated/protocolV2Schemas.generated';
import type { SessionEntitySnapshot } from '../simulationProtocolTypes';
import {
  entityRendererRegistry,
  visualEntityRenderers,
} from './entityRendererRegistry';
import type { EntityRenderFrame } from './entityRendering';

function createFrame(ownEntityId: number | null = null): {
  readonly arc: ReturnType<typeof vi.fn>;
  readonly fillText: ReturnType<typeof vi.fn>;
  readonly frame: EntityRenderFrame;
} {
  const arc = vi.fn();
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
    expect(visualEntityRenderers().map((renderer) => renderer.kind)).toEqual([
      'zone',
      'physics_body',
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
});
