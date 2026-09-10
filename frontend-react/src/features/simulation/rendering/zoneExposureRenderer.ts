import { graceSpentFraction } from '../sessionSelectors';
import type { EntityRenderInput } from './entityRendering';
import { projectWorldDistance, projectWorldPoint } from './worldProjection';

/**
 * The two danger colours. A peer reads as a warning and the session's own blob reads as an alarm,
 * so a player can tell "somebody is in trouble" from "I am in trouble" without reading the HUD.
 * Exported because a test that hard-codes a hex string asserts a copy rather than the drawn colour.
 */
export const EXPOSED_PEER_RING_COLOR = '#f97316';
export const EXPOSED_OWN_RING_COLOR = '#dc2626';

/**
 * Each ring's width at the instant a blob leaves the zone, and its width on the last frame before
 * the grace runs out. The base values are the ones drawn before the grace was on the wire, so a
 * frame that publishes no grace draws exactly what it always did.
 */
const PEER_RING_WIDTH_PIXELS = 3;
const PEER_RING_SPENT_WIDTH_PIXELS = 7;
const OWN_RING_WIDTH_PIXELS = 6;
const OWN_RING_SPENT_WIDTH_PIXELS = 13;
const OWN_OUTER_RING_WIDTH_PIXELS = 2;
const OWN_OUTER_RING_SPENT_WIDTH_PIXELS = 5;
const RING_GAP_PIXELS = 3;
const OWN_OUTER_RING_GAP_PIXELS = 12;

/**
 * Linear between the two widths as the grace is spent. `null` is not zero: it means this frame
 * published no grace, and the honest drawing then is the fixed base width rather than the thinnest
 * point of a ramp, which would read as "you have plenty of time" on a frame that says nothing.
 */
function ringWidth(
  basePixels: number,
  spentPixels: number,
  spentFraction: number | null,
): number {
  if (spentFraction === null) {
    return basePixels;
  }
  return basePixels + (spentPixels - basePixels) * spentFraction;
}

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
 * **The rings thicken as the grace is spent**, which they did not before protocol 2.2. The refusal
 * to ramp was never about taste: the denominator was not on the wire, so an intensity ramp would
 * have been a guess at a duration. `elimination_grace_ticks` is now published in the royale
 * mode-state block and reaches this renderer on the frame, so the ramp measures against the same
 * number `zone_elimination` enforces. A mode that publishes no grace — `sandbox` publishes the
 * `none` block — still gets the flat base widths, because "somebody is in danger" is the whole of
 * what such a frame supports.
 *
 * Width rather than colour, deliberately. The two exposure colours already carry "whose blob is
 * this", and the hazard ring already owns a third colour and the dash pattern; a ramp that moved
 * hue would collide with both, and a ramp on `globalAlpha` would have to be restored or it would
 * tint every renderer that ran afterwards in the same frame.
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

  const { eliminationGraceTicks, projection, surface } = frame;
  // The same guard the HUD counts down through, so a zero grace saturates instead of dividing and
  // a counter past its bound clamps instead of overshooting the ramp.
  const spentFraction = graceSpentFraction(
    component.outside_ticks,
    eliminationGraceTicks,
  );
  const center = projectWorldPoint(projection, body.position);
  const radiusPixels = Math.max(
    1,
    projectWorldDistance(projection, body.radius),
  );

  surface.strokeStyle = isOwnEntity
    ? EXPOSED_OWN_RING_COLOR
    : EXPOSED_PEER_RING_COLOR;
  surface.lineWidth = isOwnEntity
    ? ringWidth(
        OWN_RING_WIDTH_PIXELS,
        OWN_RING_SPENT_WIDTH_PIXELS,
        spentFraction,
      )
    : ringWidth(
        PEER_RING_WIDTH_PIXELS,
        PEER_RING_SPENT_WIDTH_PIXELS,
        spentFraction,
      );
  surface.beginPath();
  surface.arc(
    center.x,
    center.y,
    radiusPixels + RING_GAP_PIXELS,
    0,
    2 * Math.PI,
  );
  surface.stroke();

  if (!isOwnEntity) {
    return;
  }

  // The own blob gets a second, detached ring so its danger state is unmistakable at a glance and
  // still readable when the blob is small on screen or crowded by peers.
  surface.lineWidth = ringWidth(
    OWN_OUTER_RING_WIDTH_PIXELS,
    OWN_OUTER_RING_SPENT_WIDTH_PIXELS,
    spentFraction,
  );
  surface.beginPath();
  surface.arc(
    center.x,
    center.y,
    radiusPixels + OWN_OUTER_RING_GAP_PIXELS,
    0,
    2 * Math.PI,
  );
  surface.stroke();
}
