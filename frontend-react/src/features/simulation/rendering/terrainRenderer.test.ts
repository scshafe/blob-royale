import { describe, expect, it, vi } from 'vitest';

import goldens from '../../../../../tests/fixtures/terrain/geometry-goldens.json';
import { raceTerrain, validatedTerrain } from '../fixtures/terrainFrames';
import {
  drawTerrain,
  TERRAIN_CORRIDOR_FILL,
  TERRAIN_SOLID_FILL,
} from './terrainRenderer';
import {
  createWorldProjection,
  projectWorldDistance,
  projectWorldPoint,
} from './worldProjection';

function createSurface() {
  const operations: string[] = [];
  const strokes: { width: number; cap: string; join: string; style: string }[] =
    [];
  const surface = {
    arc: vi.fn<
      (x: number, y: number, radius: number, start: number, end: number) => void
    >(() => {
      operations.push('arc');
    }),
    beginPath: vi.fn(() => {
      operations.push('begin');
    }),
    clip: vi.fn<(rule?: CanvasFillRule) => void>(() => {
      operations.push('clip');
    }),
    closePath: vi.fn(),
    fillRect: vi.fn<
      (x: number, y: number, width: number, height: number) => void
    >(() => {
      operations.push('fill');
    }),
    fillStyle: '',
    lineCap: '',
    lineJoin: '',
    lineWidth: 0,
    lineTo: vi.fn<(x: number, y: number) => void>(),
    moveTo: vi.fn<(x: number, y: number) => void>(),
    rect: vi.fn<(x: number, y: number, width: number, height: number) => void>(
      () => {
        operations.push('rect');
      },
    ),
    restore: vi.fn(() => {
      operations.push('restore');
    }),
    save: vi.fn(() => {
      operations.push('save');
    }),
    scale: vi.fn(),
    stroke: vi.fn(() => {
      operations.push('stroke');
      strokes.push({
        width: surface.lineWidth,
        cap: surface.lineCap,
        join: surface.lineJoin,
        style: surface.strokeStyle,
      });
    }),
    strokeStyle: '',
  };
  return { surface, operations, strokes };
}

describe('terrainRenderer', () => {
  it.each(goldens.cases)(
    'renders shared $name through envelope and independent hole-complement clips',
    ({ terrain: document }) => {
      const terrain = validatedTerrain(document);
      const before = structuredClone(terrain);
      const { surface, operations, strokes } = createSurface();
      const projection = createWorldProjection(
        { x: 40, y: 70 },
        { width: 300, height: 200 },
        0.5,
      );
      drawTerrain(terrain, {
        projection,
        surface: surface as unknown as CanvasRenderingContext2D,
      });

      const origin = projectWorldPoint(projection, { x: 0, y: 0 });
      expect(surface.rect.mock.calls).toEqual(
        Array.from({ length: 1 + terrain.holes.length }, () => [
          origin.x,
          origin.y,
          50,
          50,
        ]),
      );
      expect(surface.clip.mock.calls).toEqual([
        [],
        ...terrain.holes.map(() => ['evenodd']),
      ]);
      expect(surface.arc.mock.calls).toEqual(
        terrain.holes.map((hole) => {
          const center = projectWorldPoint(projection, hole.center);
          return [
            center.x,
            center.y,
            projectWorldDistance(projection, hole.radius),
            0,
            2 * Math.PI,
          ];
        }),
      );
      // One path per complement, never one even-odd path containing multiple overlapping holes.
      expect(operations.slice(0, 4)).toEqual(['save', 'begin', 'rect', 'clip']);
      terrain.holes.forEach((_, i) => {
        expect(operations.slice(4 + i * 4, 8 + i * 4)).toEqual([
          'begin',
          'rect',
          'arc',
          'clip',
        ]);
      });
      expect(operations.lastIndexOf('clip')).toBeLessThan(
        operations.findIndex((op) => op === 'stroke' || op === 'fill'),
      );
      if (terrain.ground === 'solid') {
        expect(surface.fillRect).toHaveBeenCalledTimes(1);
        expect(surface.fillRect).toHaveBeenCalledWith(
          origin.x,
          origin.y,
          50,
          50,
        );
        expect(surface.fillStyle).toBe(TERRAIN_SOLID_FILL);
        expect(strokes).toEqual([]);
      } else {
        expect(surface.fillRect).not.toHaveBeenCalled();
        expect(strokes).toEqual(
          terrain.corridors.map((corridor) => ({
            width: corridor.half_width,
            cap: 'round',
            join: 'round',
            style: TERRAIN_CORRIDOR_FILL,
          })),
        );
        expect(surface.lineTo.mock.calls).toEqual(
          terrain.corridors.flatMap((corridor) =>
            corridor.points.slice(1).map((point) => {
              const projected = projectWorldPoint(projection, point);
              return [projected.x, projected.y];
            }),
          ),
        );
      }
      expect(surface.closePath).not.toHaveBeenCalled();
      expect(surface.scale).not.toHaveBeenCalled();
      expect(surface.save).toHaveBeenCalledTimes(1);
      expect(surface.restore).toHaveBeenCalledTimes(1);
      expect(operations.at(-1)).toBe('restore');
      expect(terrain).toEqual(before);
    },
  );

  it('preserves the open bent road and width under the existing translated uniform projection', () => {
    const { surface, strokes } = createSurface();
    drawTerrain(raceTerrain, {
      projection: createWorldProjection(
        { x: 200, y: 150 },
        { width: 300, height: 200 },
        0.5,
      ),
      surface: surface as unknown as CanvasRenderingContext2D,
    });
    expect(surface.moveTo.mock.calls).toEqual([[100, 75]]);
    expect(surface.lineTo.mock.calls).toEqual([
      [400, 75],
      [400, 275],
    ]);
    expect(strokes).toEqual([
      { width: 60, cap: 'round', join: 'round', style: TERRAIN_CORRIDOR_FILL },
    ]);
    expect(surface.closePath).not.toHaveBeenCalled();
  });
});
