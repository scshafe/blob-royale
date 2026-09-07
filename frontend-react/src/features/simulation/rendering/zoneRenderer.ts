import type { EntityRenderInput } from './entityRendering';

const ZONE_FILL = 'rgba(56, 189, 248, 0.12)';
const ZONE_STROKE = '#0284c7';
const ZONE_STROKE_WIDTH = 2;

/**
 * Draws the safe zone from the zone entity's own component. The radius is published exactly once,
 * here, and is never read from the match section: two sources for one circle is a way to disagree.
 */
export function drawZone({
  component,
  frame,
}: EntityRenderInput<'zone'>): void {
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
  surface.fillStyle = ZONE_FILL;
  surface.fill();
  surface.lineWidth = ZONE_STROKE_WIDTH;
  surface.strokeStyle = ZONE_STROKE;
  surface.stroke();
}
