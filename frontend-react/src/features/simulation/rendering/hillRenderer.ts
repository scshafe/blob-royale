import type { EntityRenderInput } from './entityRendering';

export const HILL_FILL = 'rgba(251, 191, 36, 0.22)';
export const HILL_STROKE = '#b45309';
const HILL_STROKE_WIDTH = 3;

/**
 * Draws the hill from the hill entity's own component: a filled amber disc with a heavy warm rim,
 * which is the opposite reading of the zone's cool ring -- inside a hill is where you score, inside
 * a zone is where you are safe. The radius is published exactly once, here, and is never read from
 * the match section.
 */
export function drawHill({
  component,
  frame,
}: EntityRenderInput<'hill'>): void {
  const { projection, surface } = frame;
  const radiusPixels =
    component.radius *
    Math.min(projection.horizontalScale, projection.verticalScale);
  if (radiusPixels <= 0) {
    return;
  }

  surface.beginPath();
  surface.arc(
    component.center.x * projection.horizontalScale,
    component.center.y * projection.verticalScale,
    radiusPixels,
    0,
    2 * Math.PI,
  );
  surface.fillStyle = HILL_FILL;
  surface.fill();
  surface.lineWidth = HILL_STROKE_WIDTH;
  surface.strokeStyle = HILL_STROKE;
  surface.stroke();
}
