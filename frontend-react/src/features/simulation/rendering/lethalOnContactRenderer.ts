import type { EntityRenderInput } from './entityRendering';
import { projectWorldDistance, projectWorldPoint } from './worldProjection';

/**
 * The hazard warning colour, and the dash pattern that separates it from a zone-exposure ring.
 *
 * Exported because a test that hard-codes a hex string asserts a copy rather than the drawn colour.
 */
export const LETHAL_HAZARD_RING_COLOR = '#ef4444';

const RING_WIDTH_PIXELS = 4;
const RING_GAP_PIXELS = 2;
const DASH_PATTERN_PIXELS = Object.freeze([9, 6]);

/**
 * Draws the danger of a body that eliminates on contact.
 *
 * `lethal_on_contact` is a marker: it publishes an empty object, and its **presence** is the whole
 * message (`docs/protocol/schema/v2/lethal-on-contact-component.schema.json`). So this renderer has
 * no field to read and no state to threshold -- if the component is on the entity this frame, the
 * body is lethal this frame.
 *
 * The ring is drawn from the entity's `physics_body`, never from the marker, because a marker
 * carries no geometry. It strokes outside the body radius so the hazard itself stays legible
 * underneath, exactly as `drawZoneExposure` does.
 *
 * **It is dashed, and that is the point.** A zone-exposure ring is solid and means "this blob is
 * being counted against"; a hazard ring is dashed and means "this object kills". Two different
 * warnings on the same canvas have to be told apart at a glance and while moving, and colour alone
 * fails that for a player who cannot distinguish red from amber. The two never appear on the same
 * entity today -- a hazard carries no `ZoneExposure` -- but they appear in the same frame
 * constantly, which is where the confusion would live.
 *
 * A heavy but non-lethal hazard deliberately gets no ring. It is dangerous to your position, not to
 * your life, and marking both the same way would teach a player to ignore the marking.
 */
export function drawLethalOnContact({
  entity,
  frame,
}: EntityRenderInput<'lethal_on_contact'>): void {
  const body = entity.components.physics_body;
  if (body === undefined) {
    // A lethal marker with no body this frame is an entity the server has destroyed or has not yet
    // placed. Ringing the origin would draw a threat where nothing is standing.
    return;
  }

  const { projection, surface } = frame;
  const center = projectWorldPoint(projection, body.position);
  const radiusPixels = Math.max(
    1,
    projectWorldDistance(projection, body.radius),
  );

  surface.save();
  surface.strokeStyle = LETHAL_HAZARD_RING_COLOR;
  surface.lineWidth = RING_WIDTH_PIXELS;
  surface.setLineDash([...DASH_PATTERN_PIXELS]);
  surface.beginPath();
  surface.arc(
    center.x,
    center.y,
    radiusPixels + RING_GAP_PIXELS,
    0,
    2 * Math.PI,
  );
  surface.stroke();
  // The dash pattern is restored rather than left set, because the surface is shared with every
  // later renderer in the frame and a leaked dash would turn the next solid ring into a dotted one.
  surface.restore();
}
