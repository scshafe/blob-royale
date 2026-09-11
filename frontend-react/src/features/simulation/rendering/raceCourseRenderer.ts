import type { RaceModeState } from '../sessionSelectors';
import {
  RACE_CHECKPOINT_FILL,
  RACE_CHECKPOINT_STROKE,
  RACE_FINISH_FILL,
  RACE_FINISH_STROKE,
  RACE_GATE_LABEL_FONT,
  RACE_GATE_STROKE_WIDTH,
} from '../simulationConstants';
import type { ModeStateRenderFrame } from './modeStateRendering';
import { projectWorldDistance, projectWorldPoint } from './worldProjection';

export interface RaceCourseRenderInput {
  readonly frame: ModeStateRenderFrame;
  readonly value: RaceModeState;
}

/**
 * @canonical race_course_rendering -- draws the validated race objectives below every entity.
 *
 * Welcome terrain owns all ground geometry; the race block carries only its selected road name.
 * Gate centers and radii share the canonical world projection with bodies and terrain. Gate labels
 * and rims stay readable in logical pixels. The finish remains distinguishable without color.
 */
export function drawRaceCourse({ frame, value }: RaceCourseRenderInput): void {
  const { projection, surface } = frame;
  surface.save();
  value.checkpoints.forEach((checkpoint, index) => {
    const isFinish = index === value.checkpoints.length - 1;
    const center = projectWorldPoint(projection, checkpoint);
    surface.beginPath();
    surface.arc(
      center.x,
      center.y,
      projectWorldDistance(projection, value.checkpoint_radius),
      0,
      2 * Math.PI,
    );
    surface.fillStyle = isFinish ? RACE_FINISH_FILL : RACE_CHECKPOINT_FILL;
    surface.fill();
    surface.lineWidth = isFinish
      ? 2 * RACE_GATE_STROKE_WIDTH
      : RACE_GATE_STROKE_WIDTH;
    surface.strokeStyle = isFinish
      ? RACE_FINISH_STROKE
      : RACE_CHECKPOINT_STROKE;
    surface.stroke();
    surface.font = RACE_GATE_LABEL_FONT;
    surface.textAlign = 'center';
    surface.textBaseline = 'middle';
    surface.fillStyle = surface.strokeStyle;
    surface.fillText(
      isFinish ? 'Finish' : String(index + 1),
      center.x,
      center.y,
    );
  });
  // Gate styling and label alignment must not reach entity layers sharing this surface.
  surface.restore();
}
