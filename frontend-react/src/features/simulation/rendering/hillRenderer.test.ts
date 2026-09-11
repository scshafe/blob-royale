import { describe, expect, it, vi } from 'vitest';

import {
  hillRoamingRenderCases,
  hillRoamingTerrain,
} from '../fixtures/hillRoamingFrames';
import { entityRendererRegistry } from './entityRendererRegistry';
import { HILL_FILL } from './hillRenderer';
import { drawTerrain } from './terrainRenderer';
import {
  createWorldProjection,
  projectWorldDistance,
  projectWorldPoint,
} from './worldProjection';

/** Captures whether a hill arc inherits the terrain renderer's saved clip scope. */
function createHillSurface() {
  let savedDepth = 0;
  const arcScopes: number[] = [];
  const surface = {
    arc: vi.fn<
      (x: number, y: number, radius: number, start: number, end: number) => void
    >(() => arcScopes.push(savedDepth)),
    beginPath: vi.fn(),
    clip: vi.fn(),
    fill: vi.fn(),
    fillRect: vi.fn(),
    fillStyle: '',
    lineWidth: 1,
    moveTo: vi.fn(),
    rect: vi.fn(),
    restore: vi.fn(() => {
      savedDepth -= 1;
    }),
    save: vi.fn(() => {
      savedDepth += 1;
    }),
    stroke: vi.fn(),
    strokeStyle: '',
  };
  return { surface, arcScopes };
}

describe('hillRenderer', () => {
  it.each(hillRoamingRenderCases)(
    'draws the full published circle when $name without terrain clipping or prediction',
    ({ entity }) => {
      const hill = entity.components.hill;
      if (hill === undefined) throw new Error('TEST.HILL_FIXTURE_MISSING');
      const before = structuredClone(hillRoamingTerrain);
      const { surface, arcScopes } = createHillSurface();
      const projection = createWorldProjection(
        { x: 300, y: 150 },
        { width: 600, height: 400 },
        0.5,
      );
      const frame = {
        eliminationGraceTicks: null,
        ownEntityId: null,
        projection,
        surface: surface as unknown as CanvasRenderingContext2D,
      };
      drawTerrain(hillRoamingTerrain, frame);
      entityRendererRegistry.hill.drawEntity(entity, frame);

      const center = projectWorldPoint(projection, hill.center);
      expect(surface.arc).toHaveBeenLastCalledWith(
        center.x,
        center.y,
        projectWorldDistance(projection, hill.radius),
        0,
        2 * Math.PI,
      );
      expect(arcScopes).toEqual([1, 0]);
      expect(surface.clip).toHaveBeenCalledTimes(2);
      expect(surface.fill).toHaveBeenCalledTimes(1);
      expect(surface.fillStyle).toBe(HILL_FILL);
      expect(hillRoamingTerrain).toEqual(before);
    },
  );
});
