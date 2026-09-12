import { raceTerrain } from '../fixtures/terrainFrames';
import { describe, expect, it, vi } from 'vitest';

import {
  legacyNpcCatalogue,
  raceSnapshotDocument,
} from '../fixtures/sessionFrames';
import { validateSessionSnapshotMessage } from '../sessionProtocolValidation';
import {
  RACE_CHECKPOINT_FILL,
  RACE_CHECKPOINT_STROKE,
  RACE_FINISH_FILL,
  RACE_FINISH_STROKE,
} from '../simulationConstants';
import type { SessionMatchSection } from '../simulationProtocolTypes';
import { modeStateRendererRegistry } from './modeStateRendererRegistry';
import { entityRendererRegistry } from './entityRendererRegistry';
import { createWorldProjection } from './worldProjection';

function validatedRaceMatch(): SessionMatchSection {
  const document = raceSnapshotDocument();
  return validateSessionSnapshotMessage(document, {
    messageSequence: 1,
    requestId: document.meta.request_id,
    tickSequence: null,
    npcCatalogue: legacyNpcCatalogue,
    terrain: raceTerrain,
  }).data.match;
}

function createSurface() {
  const strokes: {
    readonly cap: CanvasLineCap;
    readonly join: CanvasLineJoin;
    readonly style: string | CanvasGradient | CanvasPattern;
    readonly width: number;
  }[] = [];
  const fills: (string | CanvasGradient | CanvasPattern)[] = [];
  const surface = {
    arc: vi.fn<
      (x: number, y: number, radius: number, start: number, end: number) => void
    >(),
    beginPath: vi.fn(),
    closePath: vi.fn(),
    fill: vi.fn(() => fills.push(surface.fillStyle)),
    fillStyle: '',
    fillText: vi.fn<(text: string, x: number, y: number) => void>(),
    font: '',
    lineCap: 'butt' as CanvasLineCap,
    lineJoin: 'miter' as CanvasLineJoin,
    lineTo: vi.fn<(x: number, y: number) => void>(),
    lineWidth: 1,
    moveTo: vi.fn<(x: number, y: number) => void>(),
    restore: vi.fn(),
    save: vi.fn(),
    scale: vi.fn<(x: number, y: number) => void>(),
    stroke: vi.fn(() => {
      strokes.push({
        cap: surface.lineCap,
        join: surface.lineJoin,
        style: surface.strokeStyle,
        width: surface.lineWidth,
      });
    }),
    strokeStyle: '',
    textAlign: '',
    textBaseline: '',
  };
  return { fills, strokes, surface };
}

describe('raceCourseRenderer', () => {
  it('projects only race objectives, never the retained road mirror', () => {
    const { strokes, surface } = createSurface();
    modeStateRendererRegistry[
      'blob-royale://protocol/v3/mode-state/race'
    ].drawModeState(validatedRaceMatch(), {
      projection: createWorldProjection(
        { x: 200, y: 150 },
        { width: 300, height: 200 },
        0.5,
      ),
      surface: surface as unknown as CanvasRenderingContext2D,
    });

    expect(surface.moveTo).not.toHaveBeenCalled();
    expect(surface.lineTo).not.toHaveBeenCalled();
    expect(surface.closePath).not.toHaveBeenCalled();
    expect(surface.scale).not.toHaveBeenCalled();
    expect(strokes).toHaveLength(3);
    expect(surface.arc.mock.calls).toEqual([
      [200, 75, 10, 0, 2 * Math.PI],
      [400, 125, 10, 0, 2 * Math.PI],
      [400, 275, 10, 0, 2 * Math.PI],
    ]);
    expect(surface.save).toHaveBeenCalledTimes(1);
    expect(surface.restore).toHaveBeenCalledTimes(1);
  });

  it('draws gates in declared order and marks the terminal gate with a label and heavier rim', () => {
    const { fills, strokes, surface } = createSurface();
    modeStateRendererRegistry[
      'blob-royale://protocol/v3/mode-state/race'
    ].drawModeState(validatedRaceMatch(), {
      projection: createWorldProjection(
        { x: 1, y: 1 },
        { width: 2, height: 2 },
        1,
      ),
      surface: surface as unknown as CanvasRenderingContext2D,
    });

    expect(surface.arc.mock.calls).toEqual([
      [300, 100, 20, 0, 2 * Math.PI],
      [700, 200, 20, 0, 2 * Math.PI],
      [700, 500, 20, 0, 2 * Math.PI],
    ]);
    expect(surface.fillText.mock.calls).toEqual([
      ['1', 300, 100],
      ['2', 700, 200],
      ['Finish', 700, 500],
    ]);
    expect(fills).toEqual([
      RACE_CHECKPOINT_FILL,
      RACE_CHECKPOINT_FILL,
      RACE_FINISH_FILL,
    ]);
    expect(strokes.map(({ style, width }) => ({ style, width }))).toEqual([
      { style: RACE_CHECKPOINT_STROKE, width: 2 },
      { style: RACE_CHECKPOINT_STROKE, width: 2 },
      { style: RACE_FINISH_STROKE, width: 4 },
    ]);
  });

  it('aligns a translated finish gate with the body at that world position without mutating the course', () => {
    const { surface, strokes } = createSurface();
    const match = validatedRaceMatch();
    const before = structuredClone(match);
    const frame = {
      projection: createWorldProjection(
        { x: 200, y: 150 },
        { width: 300, height: 200 },
        0.5,
      ),
      surface: surface as unknown as CanvasRenderingContext2D,
    };
    modeStateRendererRegistry[
      'blob-royale://protocol/v3/mode-state/race'
    ].drawModeState(match, frame);
    entityRendererRegistry.physics_body.drawEntity(
      {
        entity_id: 21,
        components: {
          physics_body: {
            acceleration: { x: 0, y: 0 },
            collision_layer: 1,
            collision_mask: 3,
            is_static: false,
            ground_attachment: 'ground_bound',
            mass: 1,
            position: { x: 700, y: 500 },
            radius: 20,
            velocity: { x: 0, y: 0 },
          },
        },
      },
      {
        ...frame,
        eliminationGraceTicks: null,
        ownEntityId: 21,
        // A body is drawn from its published position alone; nothing here reads a window, so the
        // alignment this case pins holds with or without a tick on the frame.
        tickSequence: null,
      },
    );

    expect(surface.arc.mock.calls).toEqual([
      [200, 75, 10, 0, 2 * Math.PI],
      [400, 125, 10, 0, 2 * Math.PI],
      [400, 275, 10, 0, 2 * Math.PI],
      [400, 275, 10, 0, 2 * Math.PI],
    ]);
    expect(surface.fillText.mock.calls).toEqual([
      ['1', 200, 75],
      ['2', 400, 125],
      ['Finish', 400, 275],
    ]);
    expect(strokes.slice(0, 3).map(({ width }) => width)).toEqual([2, 2, 4]);
    expect(surface.font).toBe('bold 12px system-ui');
    expect(surface.scale).not.toHaveBeenCalled();
    expect(match).toEqual(before);
  });
});
