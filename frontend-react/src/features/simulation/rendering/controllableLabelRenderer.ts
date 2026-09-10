import type { EntityRenderInput } from './entityRendering';
import { projectWorldDistance, projectWorldPoint } from './worldProjection';

const LABEL_FONT = '12px system-ui, sans-serif';
const LABEL_GAP_PIXELS = 4;
const OWN_LABEL_COLOR = '#0f172a';
const PEER_LABEL_COLOR = '#334155';

/**
 * Draws the display name of a controlled entity beneath its body. The name is a label the server
 * copied byte-exactly or generated; it is never an identifier, so it is drawn and never compared.
 */
export function drawControllableLabel({
  component,
  entity,
  frame,
  isOwnEntity,
}: EntityRenderInput<'controllable'>): void {
  const body = entity.components.physics_body;
  if (body === undefined) {
    // A controller with no body this frame is the ordinary state of an eliminated or deferred
    // player. There is no position to label, and inventing one would draw a blob that is not there.
    return;
  }

  const { projection, surface } = frame;
  const center = projectWorldPoint(projection, body.position);
  const radiusPixels = Math.max(
    1,
    projectWorldDistance(projection, body.radius),
  );
  surface.font = LABEL_FONT;
  surface.textAlign = 'center';
  surface.textBaseline = 'top';
  surface.fillStyle = isOwnEntity ? OWN_LABEL_COLOR : PEER_LABEL_COLOR;
  surface.fillText(
    component.display_name,
    center.x,
    center.y + radiusPixels + LABEL_GAP_PIXELS,
  );
}
