import { describe, expect, it } from 'vitest';

import {
  createWorldProjection,
  projectWorldDistance,
  projectWorldPoint,
  unprojectCanvasOffset,
} from './worldProjection';

const cameraCenter = Object.freeze({ x: 1_300, y: 850 });
const logicalViewport = Object.freeze({ width: 997, height: 563 });
const localScale = 0.75;

describe('worldProjection', () => {
  it('puts the camera centre at the exact viewport centre without rounding either axis', () => {
    const projection = createWorldProjection(
      cameraCenter,
      logicalViewport,
      localScale,
    );

    expect(projectWorldPoint(projection, cameraCenter)).toEqual({
      x: 498.5,
      y: 281.5,
    });
    expect(projection).toEqual({
      scale: 0.75,
      offsetX: -476.5,
      offsetY: -356,
    });
  });

  it('uses one distance scale for both axes even with an awkward viewport aspect ratio', () => {
    const projection = createWorldProjection(
      cameraCenter,
      logicalViewport,
      localScale,
    );
    const center = projectWorldPoint(projection, cameraCenter);
    const horizontal = projectWorldPoint(projection, {
      x: cameraCenter.x + 40,
      y: cameraCenter.y,
    });
    const vertical = projectWorldPoint(projection, {
      x: cameraCenter.x,
      y: cameraCenter.y + 40,
    });

    expect(horizontal.x - center.x).toBe(30);
    expect(vertical.y - center.y).toBe(30);
    expect(projectWorldDistance(projection, 40)).toBe(30);
    expect(projectWorldDistance(projection, 0)).toBe(0);
  });

  it('changes visible extent on resize without changing scale or the centred world point', () => {
    const narrowViewport = { width: 240, height: 180 };
    const projection = createWorldProjection(
      cameraCenter,
      narrowViewport,
      localScale,
    );

    expect(projectWorldPoint(projection, cameraCenter)).toEqual({
      x: 120,
      y: 90,
    });
    expect(projectWorldDistance(projection, 40)).toBe(30);
  });

  it('leaves offscreen points and outside-map camera centres unclamped', () => {
    const projection = createWorldProjection(
      { x: -40, y: 20 },
      { width: 200, height: 100 },
      1,
    );

    expect(projectWorldPoint(projection, { x: 0, y: 0 })).toEqual({
      x: 140,
      y: 30,
    });
    expect(projectWorldPoint(projection, { x: 1_920, y: 1_280 })).toEqual({
      x: 2_060,
      y: 1_310,
    });
  });

  it('inverts signed pixel offsets without including camera translation', () => {
    const projection = createWorldProjection(
      cameraCenter,
      logicalViewport,
      localScale,
    );
    const displacement = Object.freeze({ x: 120, y: -60 });
    const worldOffset = unprojectCanvasOffset(projection, displacement);

    expect(worldOffset).toEqual({ x: 160, y: -80 });
    expect(worldOffset.x * projection.scale).toBe(displacement.x);
    expect(worldOffset.y * projection.scale).toBe(displacement.y);
    expect(Object.isFrozen(worldOffset)).toBe(true);
  });

  it('projects one immutable world point through independent views without changing inputs', () => {
    const point = Object.freeze({ x: 1_500, y: 900 });
    const first = createWorldProjection(
      cameraCenter,
      logicalViewport,
      localScale,
    );
    const second = createWorldProjection(point, logicalViewport, localScale);
    const firstPoint = projectWorldPoint(first, point);
    const secondPoint = projectWorldPoint(second, point);

    expect(firstPoint).toEqual({ x: 648.5, y: 319 });
    expect(secondPoint).toEqual({ x: 498.5, y: 281.5 });
    expect(point).toEqual({ x: 1_500, y: 900 });
    expect(cameraCenter).toEqual({ x: 1_300, y: 850 });
    expect(logicalViewport).toEqual({ width: 997, height: 563 });
    expect(Object.isFrozen(first)).toBe(true);
    expect(Object.isFrozen(firstPoint)).toBe(true);
  });

  it.each([
    { name: 'nonfinite centre x', center: { x: NaN, y: 0 }, field: 'center_x' },
    {
      name: 'nonfinite centre y',
      center: { x: 0, y: Infinity },
      field: 'center_y',
    },
  ])(
    'rejects $name with a structured projection error',
    ({ center, field }) => {
      expect(() =>
        createWorldProjection(center, logicalViewport, localScale),
      ).toThrow(
        expect.objectContaining({
          code: 'SIMULATION.CAMERA_PROJECTION_INVALID',
          context: {
            field,
            operation: 'create world projection',
            value: field === 'center_x' ? center.x : center.y,
          },
        }),
      );
    },
  );

  it.each([0, -1, NaN, Infinity])(
    'rejects invalid scale %s instead of choosing a fallback',
    (scale) => {
      expect(() =>
        createWorldProjection(cameraCenter, logicalViewport, scale),
      ).toThrow(
        expect.objectContaining({
          code: 'SIMULATION.CAMERA_PROJECTION_INVALID',
        }),
      );
    },
  );

  it.each([
    { width: 0, height: 640 },
    { width: -1, height: 640 },
    { width: Infinity, height: 640 },
    { width: 960, height: 0 },
    { width: 960, height: -1 },
    { width: 960, height: NaN },
  ])('rejects invalid logical viewport $width by $height', (viewport) => {
    expect(() =>
      createWorldProjection(cameraCenter, viewport, localScale),
    ).toThrow(
      expect.objectContaining({ code: 'SIMULATION.CAMERA_PROJECTION_INVALID' }),
    );
  });

  it('rejects finite inputs whose projection arithmetic overflows', () => {
    expect(() =>
      createWorldProjection({ x: Number.MAX_VALUE, y: 0 }, logicalViewport, 2),
    ).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.CAMERA_PROJECTION_INVALID',
        context: {
          field: 'offset_x',
          operation: 'create world projection',
          value: -Infinity,
        },
      }),
    );
  });

  it('rejects invalid distances and coordinates at the projection boundary', () => {
    const projection = createWorldProjection(
      cameraCenter,
      logicalViewport,
      localScale,
    );

    expect(() => projectWorldDistance(projection, -1)).toThrow(
      expect.objectContaining({ code: 'SIMULATION.CAMERA_PROJECTION_INVALID' }),
    );
    expect(() => projectWorldPoint(projection, { x: NaN, y: 0 })).toThrow(
      expect.objectContaining({ code: 'SIMULATION.CAMERA_PROJECTION_INVALID' }),
    );
    expect(() =>
      unprojectCanvasOffset(projection, { x: 0, y: Infinity }),
    ).toThrow(
      expect.objectContaining({ code: 'SIMULATION.CAMERA_PROJECTION_INVALID' }),
    );
  });

  it('rejects coordinate, distance, and inverse arithmetic overflow instead of returning infinity', () => {
    const magnified = createWorldProjection({ x: 0, y: 0 }, logicalViewport, 2);
    const tinyScale = createWorldProjection(
      { x: 0, y: 0 },
      logicalViewport,
      Number.MIN_VALUE,
    );

    expect(() =>
      projectWorldPoint(magnified, { x: Number.MAX_VALUE, y: 0 }),
    ).toThrow(
      expect.objectContaining({ code: 'SIMULATION.CAMERA_PROJECTION_INVALID' }),
    );
    expect(() => projectWorldDistance(magnified, Number.MAX_VALUE)).toThrow(
      expect.objectContaining({ code: 'SIMULATION.CAMERA_PROJECTION_INVALID' }),
    );
    expect(() => unprojectCanvasOffset(tinyScale, { x: 1, y: 0 })).toThrow(
      expect.objectContaining({ code: 'SIMULATION.CAMERA_PROJECTION_INVALID' }),
    );
  });

  it('rejects an invalid manually constructed transform at every projection entry point', () => {
    const invalidProjection = { scale: 0, offsetX: 0, offsetY: 0 };

    expect(() => projectWorldPoint(invalidProjection, cameraCenter)).toThrow(
      expect.objectContaining({ code: 'SIMULATION.CAMERA_PROJECTION_INVALID' }),
    );
    expect(() => projectWorldDistance(invalidProjection, 10)).toThrow(
      expect.objectContaining({ code: 'SIMULATION.CAMERA_PROJECTION_INVALID' }),
    );
    expect(() =>
      unprojectCanvasOffset(invalidProjection, cameraCenter),
    ).toThrow(
      expect.objectContaining({ code: 'SIMULATION.CAMERA_PROJECTION_INVALID' }),
    );
  });
});
