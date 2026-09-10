import type { EntityRenderInput } from './entityRendering';
import { projectWorldDistance, projectWorldPoint } from './worldProjection';

const OWN_BODY_STROKE = '#f8fafc';
const OWN_BODY_STROKE_WIDTH = 4;
const OBSTACLE_FILL = '#475569';
const OBSTACLE_STROKE = '#1e293b';
const PEER_BODY_STROKE = '#0f172a';
const BODY_STROKE_WIDTH = 1;

/** Distinct, stable, and derived only from the id, so two peers agree on every blob's color. */
function bodyFillColor(entityId: number): string {
  return `hsl(${(entityId * 137.508) % 360} 72% 48%)`;
}

/**
 * Draws one body: a dynamic body is a disc in its own color, a static body is a slate obstacle, and
 * the session's own body carries a light ring so a player can find themselves in a crowd.
 */
export function drawPhysicsBody({
  component,
  entity,
  frame,
  isOwnEntity,
}: EntityRenderInput<'physics_body'>): void {
  const { projection, surface } = frame;
  const center = projectWorldPoint(projection, component.position);
  const radiusPixels = Math.max(
    1,
    projectWorldDistance(projection, component.radius),
  );

  surface.beginPath();
  surface.arc(center.x, center.y, radiusPixels, 0, 2 * Math.PI);
  surface.fillStyle = component.is_static
    ? OBSTACLE_FILL
    : bodyFillColor(entity.entity_id);
  surface.fill();
  surface.lineWidth = isOwnEntity ? OWN_BODY_STROKE_WIDTH : BODY_STROKE_WIDTH;
  if (isOwnEntity) {
    surface.strokeStyle = OWN_BODY_STROKE;
  } else {
    surface.strokeStyle = component.is_static
      ? OBSTACLE_STROKE
      : PEER_BODY_STROKE;
  }
  surface.stroke();
}
