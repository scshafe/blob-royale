import type { EntityRenderInput } from './entityRendering';

/**
 * The two danger colours. A peer reads as a warning and the session's own blob reads as an alarm,
 * so a player can tell "somebody is in trouble" from "I am in trouble" without reading the HUD.
 * Exported because a test that hard-codes a hex string asserts a copy rather than the drawn colour.
 */
export const EXPOSED_PEER_RING_COLOR = '#f97316';
export const EXPOSED_OWN_RING_COLOR = '#dc2626';

const PEER_RING_WIDTH_PIXELS = 3;
const OWN_RING_WIDTH_PIXELS = 6;
const OWN_OUTER_RING_WIDTH_PIXELS = 2;
const RING_GAP_PIXELS = 3;
const OWN_OUTER_RING_GAP_PIXELS = 12;

/**
 * Draws the elimination danger of a blob whose center is outside the safe zone.
 *
 * `zone_exposure.outside_ticks` is the consecutive committed ticks a center has been outside, and
 * `zone_elimination` resets it to zero the tick a blob re-enters (ADR 0005 § "Elimination and
 * placement"). A ring is therefore drawn on exactly the ticks the server is counting against that
 * entity, and it disappears on the first snapshot after re-entry with no client-side timer.
 *
 * The ring is drawn from the entity's `physics_body`, never from the exposure component, because
 * exposure carries no geometry. It strokes and never fills: a filled disc here would repaint the
 * blob's own colour, and a stroke outside the body radius leaves the blob legible underneath.
 *
 * The rings are deliberately not scaled by how much grace is left. `elimination_grace_seconds` is
 * composition-root configuration that no accepted wire artifact publishes, so an intensity ramp
 * would be a guess at a duration; the honest signal is binary danger here plus the elapsed exposure
 * the HUD reports.
 */
export function drawZoneExposure({
  component,
  entity,
  frame,
  isOwnEntity,
}: EntityRenderInput<'zone_exposure'>): void {
  if (component.outside_ticks <= 0) {
    return;
  }

  const body = entity.components.physics_body;
  if (body === undefined) {
    // An exposure counter with no body this frame is an entity the server has already destroyed or
    // has not yet seated. There is no blob to ring, and ringing the origin would draw a threat at a
    // place nobody is standing.
    return;
  }

  const { projection, surface } = frame;
  const centerX = body.position.x * projection.horizontalScale;
  const centerY = body.position.y * projection.verticalScale;
  const radiusPixels = Math.max(
    1,
    body.radius *
      Math.min(projection.horizontalScale, projection.verticalScale),
  );

  surface.strokeStyle = isOwnEntity
    ? EXPOSED_OWN_RING_COLOR
    : EXPOSED_PEER_RING_COLOR;
  surface.lineWidth = isOwnEntity
    ? OWN_RING_WIDTH_PIXELS
    : PEER_RING_WIDTH_PIXELS;
  surface.beginPath();
  surface.arc(centerX, centerY, radiusPixels + RING_GAP_PIXELS, 0, 2 * Math.PI);
  surface.stroke();

  if (!isOwnEntity) {
    return;
  }

  // The own blob gets a second, detached ring so its danger state is unmistakable at a glance and
  // still readable when the blob is small on screen or crowded by peers.
  surface.lineWidth = OWN_OUTER_RING_WIDTH_PIXELS;
  surface.beginPath();
  surface.arc(
    centerX,
    centerY,
    radiusPixels + OWN_OUTER_RING_GAP_PIXELS,
    0,
    2 * Math.PI,
  );
  surface.stroke();
}
