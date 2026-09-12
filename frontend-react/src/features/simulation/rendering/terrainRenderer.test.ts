import { describe, expect, it, vi } from 'vitest';

import goldens from '../../../../../tests/fixtures/terrain/geometry-goldens.json';
import { raceTerrain, validatedTerrain } from '../fixtures/terrainFrames';
import {
  drawTerrain,
  TERRAIN_CLIFF_RIM,
  TERRAIN_CLIFF_RIM_WORLD_UNITS,
  TERRAIN_CORRIDOR_FILL,
  TERRAIN_SOLID_FILL,
  TERRAIN_VOID_FILL,
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
  const fills: {
    x: number;
    y: number;
    width: number;
    height: number;
    style: string;
  }[] = [];
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
    // The recorded fill style separates the void underlay from the ground sheet painted over it:
    // both are the same envelope rectangle, and only the colour says which layer a call is.
    fillRect: vi.fn<
      (x: number, y: number, width: number, height: number) => void
    >((x, y, width, height) => {
      operations.push('fill');
      fills.push({ x, y, width, height, style: surface.fillStyle });
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
  return { surface, operations, strokes, fills };
}

/** The shared geometry golden of that name, admitted through the same validator the transport uses. */
function goldenTerrain(name: string) {
  const golden = goldens.cases.find((candidate) => candidate.name === name);
  if (golden === undefined) throw new Error('TEST.TERRAIN_GOLDEN_MISSING');
  return validatedTerrain(golden.terrain);
}

/**
 * An unscaled, untranslated projection over the goldens' 100x100 envelope, so a case's authored
 * world coordinates are its CSS pixels and every expectation below reads as the geometry itself.
 * The scaled and translated projections stay in the two suites above, which is where they belong.
 */
const unitProjection = createWorldProjection(
  { x: 50, y: 50 },
  { width: 100, height: 100 },
  1,
);

function unitFrame(surface: ReturnType<typeof createSurface>['surface']) {
  return {
    projection: unitProjection,
    surface: surface as unknown as CanvasRenderingContext2D,
  };
}

/** A hole rim at the unit projection: the stroke is 2 * the rim, half of it clipped into the hole. */
const UNIT_HOLE_RIM = {
  width: 2 * TERRAIN_CLIFF_RIM_WORLD_UNITS,
  cap: 'round',
  join: 'round',
  style: TERRAIN_CLIFF_RIM,
};

describe('terrainRenderer', () => {
  it.each(goldens.cases)(
    'renders shared $name through envelope and independent hole-complement clips',
    ({ terrain: document }) => {
      const terrain = validatedTerrain(document);
      const before = structuredClone(terrain);
      const { surface, operations, strokes, fills } = createSurface();
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
      const holeArcs = terrain.holes.map((hole) => {
        const center = projectWorldPoint(projection, hole.center);
        return [
          center.x,
          center.y,
          projectWorldDistance(projection, hole.radius),
          0,
          2 * Math.PI,
        ];
      });
      const corridorLineTos = terrain.corridors.flatMap((corridor) =>
        corridor.points.slice(1).map((point) => {
          const projected = projectWorldPoint(projection, point);
          return [projected.x, projected.y];
        }),
      );
      // Every rim is a world quantity through the one projection, so at this scale of 0.5 a hole
      // rim's 2 * rim stroke lands on the constant itself and a corridor's 2 * half_width + 2 * rim
      // underprint lands on half_width plus it. Neither is recomputed from the renderer's formula.
      const holeRimStrokes = terrain.holes.map(() => ({
        width: TERRAIN_CLIFF_RIM_WORLD_UNITS,
        cap: 'round',
        join: 'round',
        style: TERRAIN_CLIFF_RIM,
      }));
      const voidUnderlay = {
        x: origin.x,
        y: origin.y,
        width: 50,
        height: 50,
        style: TERRAIN_VOID_FILL,
      };

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
      // Twice per hole: once to cut its complement out of the live clip, and once more to stroke the
      // rim inside that clip, where only the ground-side half of the circle survives.
      expect(surface.arc.mock.calls).toEqual([...holeArcs, ...holeArcs]);
      // One path per complement, never one even-odd path containing multiple overlapping holes. The
      // void underlay sits between the envelope clip and the first complement.
      expect(operations.slice(0, 5)).toEqual([
        'save',
        'begin',
        'rect',
        'clip',
        'fill',
      ]);
      terrain.holes.forEach((_, i) => {
        expect(operations.slice(5 + i * 4, 9 + i * 4)).toEqual([
          'begin',
          'rect',
          'arc',
          'clip',
        ]);
      });
      // The underlay is the one paint that precedes the complements, deliberately: it has to show
      // THROUGH them. Every GROUND paint still follows every clip, which is what this guarded — no
      // later road may refill a hole.
      expect(fills[0]).toEqual(voidUnderlay);
      expect(operations.lastIndexOf('clip')).toBeLessThan(
        operations.indexOf(terrain.ground === 'solid' ? 'fill' : 'stroke', 5),
      );
      if (terrain.ground === 'solid') {
        expect(fills).toEqual([
          voidUnderlay,
          {
            x: origin.x,
            y: origin.y,
            width: 50,
            height: 50,
            style: TERRAIN_SOLID_FILL,
          },
        ]);
        expect(surface.fillStyle).toBe(TERRAIN_SOLID_FILL);
        expect(strokes).toEqual(holeRimStrokes);
      } else {
        // Still no solid sheet on a corridor map: the only fillRect is the underlay beneath it.
        expect(fills).toEqual([voidUnderlay]);
        expect(strokes).toEqual([
          ...terrain.corridors.map((corridor) => ({
            width: corridor.half_width + TERRAIN_CLIFF_RIM_WORLD_UNITS,
            cap: 'round',
            join: 'round',
            style: TERRAIN_CLIFF_RIM,
          })),
          ...terrain.corridors.map((corridor) => ({
            width: corridor.half_width,
            cap: 'round',
            join: 'round',
            style: TERRAIN_CORRIDOR_FILL,
          })),
          ...holeRimStrokes,
        ]);
        // The same points twice: the rim underprint and the surface trace one polyline each.
        expect(surface.lineTo.mock.calls).toEqual([
          ...corridorLineTos,
          ...corridorLineTos,
        ]);
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
    // One polyline traced twice from the same points: the rim underprint, then the road over it.
    expect(surface.moveTo.mock.calls).toEqual([
      [100, 75],
      [100, 75],
    ]);
    expect(surface.lineTo.mock.calls).toEqual([
      [400, 75],
      [400, 275],
      [400, 75],
      [400, 275],
    ]);
    // A 120-unit road under a 124-unit rim, each halved by the 0.5 scale: the rim keeps exactly
    // TERRAIN_CLIFF_RIM_WORLD_UNITS of world on either side of the surface, at any camera scale.
    expect(strokes).toEqual([
      { width: 62, cap: 'round', join: 'round', style: TERRAIN_CLIFF_RIM },
      { width: 60, cap: 'round', join: 'round', style: TERRAIN_CORRIDOR_FILL },
    ]);
    expect(surface.closePath).not.toHaveBeenCalled();
  });

  it('rims a hole inside its live complement clip, and rims a closed envelope not at all', () => {
    const open = createSurface();
    drawTerrain(goldenTerrain('solid_open_hole'), unitFrame(open.surface));
    expect(open.operations).toEqual([
      'save',
      'begin',
      'rect',
      'clip',
      // The void underlay, before the complement, so it shows through it.
      'fill',
      'begin',
      'rect',
      'arc',
      'clip',
      // Solid ground, over the underlay everywhere the complement still admits.
      'fill',
      'begin',
      'arc',
      // The rim, inside that clip: its inner half never reaches the void.
      'stroke',
      'restore',
    ]);
    expect(open.strokes).toEqual([UNIT_HOLE_RIM]);
    expect(open.surface.arc.mock.calls).toEqual([
      [50, 50, 10, 0, 2 * Math.PI],
      [50, 50, 10, 0, 2 * Math.PI],
    ]);

    // The arena edge is not a cliff — it folds a blob back rather than dropping it — so a closed
    // solid envelope has no rim at all, and the map border SimulationCanvas strokes from
    // configuration.world stays the only line there.
    const closed = createSurface();
    drawTerrain(
      goldenTerrain('solid_closed_envelope'),
      unitFrame(closed.surface),
    );
    expect(closed.operations).toEqual([
      'save',
      'begin',
      'rect',
      'clip',
      'fill',
      'fill',
      'restore',
    ]);
    expect(closed.strokes).toEqual([]);
    expect(closed.surface.arc).not.toHaveBeenCalled();
  });

  it('keeps both complements live while rimming overlapping holes', () => {
    const { surface, operations, strokes } = createSurface();
    drawTerrain(
      goldenTerrain('solid_overlapping_hole_union'),
      unitFrame(surface),
    );
    expect(operations).toEqual([
      'save',
      'begin',
      'rect',
      'clip',
      'fill',
      'begin',
      'rect',
      'arc',
      'clip',
      'begin',
      'rect',
      'arc',
      'clip',
      'fill',
      'begin',
      'arc',
      'stroke',
      'begin',
      'arc',
      'stroke',
      'restore',
    ]);
    // Both rims are stroked after the last complement and before the only restore. That placement
    // is what removes the arc of each circle running through its neighbour's interior, leaving the
    // outline of the union rather than two full circles crossing inside the void.
    expect(operations.lastIndexOf('clip')).toBeLessThan(
      operations.indexOf('stroke'),
    );
    expect(strokes).toEqual([UNIT_HOLE_RIM, UNIT_HOLE_RIM]);
    expect(surface.arc.mock.calls).toEqual([
      [45, 50, 15, 0, 2 * Math.PI],
      [55, 50, 15, 0, 2 * Math.PI],
      [45, 50, 15, 0, 2 * Math.PI],
      [55, 50, 15, 0, 2 * Math.PI],
    ]);
  });

  it('underprints every corridor rim before any corridor surface', () => {
    const { surface, strokes } = createSurface();
    drawTerrain(goldenTerrain('corridor_union_round_caps'), unitFrame(surface));
    // All rim passes, then all surface passes. Interleaved, the north-south rim would print over
    // the east-west surface at the crossing and cut a false cliff across supported ground.
    expect(strokes).toEqual([
      {
        width: 2 * 10 + 2 * TERRAIN_CLIFF_RIM_WORLD_UNITS,
        cap: 'round',
        join: 'round',
        style: TERRAIN_CLIFF_RIM,
      },
      {
        width: 2 * 10 + 2 * TERRAIN_CLIFF_RIM_WORLD_UNITS,
        cap: 'round',
        join: 'round',
        style: TERRAIN_CLIFF_RIM,
      },
      {
        width: 2 * 10,
        cap: 'round',
        join: 'round',
        style: TERRAIN_CORRIDOR_FILL,
      },
      {
        width: 2 * 10,
        cap: 'round',
        join: 'round',
        style: TERRAIN_CORRIDOR_FILL,
      },
    ]);
    expect(surface.moveTo.mock.calls).toEqual([
      [10, 50],
      [50, 10],
      [10, 50],
      [50, 10],
    ]);
    expect(surface.lineTo.mock.calls).toEqual([
      [90, 50],
      [50, 90],
      [90, 50],
      [50, 90],
    ]);
  });

  it('paints one void underlay beneath both ground kinds', () => {
    const solid = createSurface();
    drawTerrain(goldenTerrain('solid_open_hole'), unitFrame(solid.surface));
    // Inside a hole the underlay is what remains: the ground sheet is clipped out of it.
    expect(solid.fills).toEqual([
      { x: 0, y: 0, width: 100, height: 100, style: TERRAIN_VOID_FILL },
      { x: 0, y: 0, width: 100, height: 100, style: TERRAIN_SOLID_FILL },
    ]);

    const corridors = createSurface();
    drawTerrain(
      goldenTerrain('corridor_union_minus_overlapping_holes'),
      unitFrame(corridors.surface),
    );
    // Off-road ground is void by subtraction: the underlay covers the whole envelope and the road
    // strokes cover only what is supported, so no code decides which region is unsupported.
    expect(corridors.fills).toEqual([
      { x: 0, y: 0, width: 100, height: 100, style: TERRAIN_VOID_FILL },
    ]);
    expect(corridors.operations.indexOf('fill')).toBe(4);
    expect(corridors.operations.lastIndexOf('fill')).toBe(4);
  });
});
