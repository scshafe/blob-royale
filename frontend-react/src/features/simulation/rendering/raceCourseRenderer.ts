import type { RaceModeState } from '../sessionSelectors';
import {
  RACE_CHECKPOINT_FILL,
  RACE_CHECKPOINT_STROKE,
  RACE_COURSE_FILL,
  RACE_FINISH_FILL,
  RACE_FINISH_STROKE,
  RACE_GATE_LABEL_FONT,
  RACE_GATE_STROKE_WIDTH,
} from '../simulationConstants';
import type { ModeStateRenderFrame } from './modeStateRendering';

export interface RaceCourseRenderInput {
  readonly frame: ModeStateRenderFrame;
  readonly value: RaceModeState;
}

/**
 * @canonical race_course_rendering -- draws the validated course once, below every entity.
 *
 * A round stroke is the server's union of segment capsules: no closing segment, square ends, or
 * miter extensions. Projection transforms the whole world-space stroke, including its width, so a
 * rounded backing-buffer dimension cannot turn the visible road into a different boundary. The
 * finish has its own fill, heavy rim, and label; it remains distinguishable without color.
 */
export function drawRaceCourse({ frame, value }: RaceCourseRenderInput): void {
  const { projection, surface } = frame;
  surface.save();
  surface.scale(projection.horizontalScale, projection.verticalScale);
  surface.lineCap = 'round';
  surface.lineJoin = 'round';
  surface.lineWidth = 2 * value.track_half_width;
  surface.strokeStyle = RACE_COURSE_FILL;
  surface.beginPath();
  value.track.forEach((node, index) => {
    if (index === 0) {
      surface.moveTo(node.x, node.y);
    } else {
      surface.lineTo(node.x, node.y);
    }
  });
  surface.stroke();

  value.checkpoints.forEach((checkpoint, index) => {
    const isFinish = index === value.checkpoints.length - 1;
    surface.beginPath();
    surface.arc(
      checkpoint.x,
      checkpoint.y,
      value.checkpoint_radius,
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
      checkpoint.x,
      checkpoint.y,
    );
  });
  // In particular the scale, round joins, and label alignment must not reach the entity layers.
  surface.restore();
}
