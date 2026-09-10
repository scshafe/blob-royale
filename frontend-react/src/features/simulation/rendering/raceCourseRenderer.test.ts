import { describe, expect, it, vi } from 'vitest';

import { raceSnapshotDocument } from '../fixtures/sessionFrames';
import { validateSessionSnapshotMessage } from '../sessionProtocolValidation';
import {
  RACE_CHECKPOINT_FILL,
  RACE_CHECKPOINT_STROKE,
  RACE_COURSE_FILL,
  RACE_FINISH_FILL,
  RACE_FINISH_STROKE,
} from '../simulationConstants';
import type { SessionMatchSection } from '../simulationProtocolTypes';
import { modeStateRendererRegistry } from './modeStateRendererRegistry';

function validatedRaceMatch(): SessionMatchSection {
  const document = raceSnapshotDocument();
  return validateSessionSnapshotMessage(document, {
    messageSequence: 1,
    requestId: document.meta.request_id,
    tickSequence: null,
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
  it('draws the open corridor as segment capsules in world space and restores projection', () => {
    const { strokes, surface } = createSurface();
    modeStateRendererRegistry[
      'blob-royale://protocol/v2/mode-state/race'
    ].drawModeState(validatedRaceMatch(), {
      projection: { horizontalScale: 0.5, verticalScale: 0.75 },
      surface: surface as unknown as CanvasRenderingContext2D,
    });

    // Golden course: a horizontal segment then a bend, with no last-to-first segment.
    expect(surface.moveTo.mock.calls).toEqual([[100, 100]]);
    expect(surface.lineTo.mock.calls).toEqual([
      [700, 100],
      [700, 500],
    ]);
    expect(surface.closePath).not.toHaveBeenCalled();
    expect(surface.scale.mock.calls).toEqual([[0.5, 0.75]]);
    expect(strokes[0]).toEqual({
      cap: 'round',
      join: 'round',
      style: RACE_COURSE_FILL,
      width: 120,
    });
    expect(surface.save).toHaveBeenCalledTimes(1);
    expect(surface.restore).toHaveBeenCalledTimes(1);
  });

  it('draws gates in declared order and marks the terminal gate with a label and heavier rim', () => {
    const { fills, strokes, surface } = createSurface();
    modeStateRendererRegistry[
      'blob-royale://protocol/v2/mode-state/race'
    ].drawModeState(validatedRaceMatch(), {
      projection: { horizontalScale: 1, verticalScale: 1 },
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
    expect(
      strokes.slice(1).map(({ style, width }) => ({ style, width })),
    ).toEqual([
      { style: RACE_CHECKPOINT_STROKE, width: 2 },
      { style: RACE_CHECKPOINT_STROKE, width: 2 },
      { style: RACE_FINISH_STROKE, width: 4 },
    ]);
  });
});
