import type { EntityRenderInput } from './entityRendering';
import { projectWorldDistance, projectWorldPoint } from './worldProjection';

/**
 * The stun colour. Exported because a test that hard-codes a hex string asserts a copy rather than
 * the drawn colour.
 *
 * Violet is the one hue no other body mark uses: the danger vocabulary owns orange and red
 * (`zoneExposureRenderer`, `lethalOnContactRenderer`), protection owns sky and gold
 * (`shieldRenderer`), and the arena owns slate and amber. A stunned blob is neither in danger nor
 * protected -- it is a body whose controller has been taken away -- so it gets its own hue.
 */
export const STUN_ARC_COLOR = '#a855f7';

/**
 * A broken ring: four equal arcs, each centred in its quarter of the circle, in logical CSS pixels
 * outside the body radius for the reason `shieldRenderer` states -- a status ring is a readability
 * affordance on a body, not terrain geometry that must scale with the camera.
 *
 * The break is the message, and it is a shape rather than a colour so it survives colour-vision
 * deficiency and a crowded frame. Every other body mark in this directory is an unbroken ring and
 * means "this state is intact and running"; a stun is the opposite, a controller interrupted, and
 * an interrupted ring says that without a legend. The two rejected alternatives are worth naming: a
 * line dash already means "this object kills" in `drawLethalOnContact`'s vocabulary and must not be
 * borrowed, and a shake or flicker would need a wall clock this directory deliberately does not
 * have and would invent motion the server never published.
 *
 * The inset is derived from the sweep rather than authored beside it, so the arcs stay centred in
 * their quarters if the sweep is ever retuned.
 */
const STUN_ARC_COUNT = 4;
const STUN_ARC_SPACING_RADIANS = (2 * Math.PI) / STUN_ARC_COUNT;
const STUN_ARC_SWEEP_RADIANS = Math.PI / 3;
const STUN_ARC_INSET_RADIANS =
  (STUN_ARC_SPACING_RADIANS - STUN_ARC_SWEEP_RADIANS) / 2;
const STUN_ARC_WIDTH_PIXELS = 4;
const STUN_ARC_GAP_PIXELS = 5;

/**
 * The half-open interval every ability window on this wire publishes: `[activation, expiry)`.
 * `tick == expiry_tick` is the first tick the stun does not cover, never its last covered tick.
 */
function windowHolds(
  activationTick: number,
  expiryTick: number,
  tick: number,
): boolean {
  return activationTick <= tick && tick < expiryTick;
}

/**
 * @canonical stun_rendering -- a stun is a window the snapshot tick falls inside, and never the
 * presence of a `stun` component.
 *
 * `stun` publishes exactly `activation_tick` and `expiry_tick`
 * (`docs/protocol/schema/v3/stun-component.schema.json`), absolute and half-open, and the shared
 * status system erases the component once the window has expired. This renderer draws while
 * `activation_tick <= tick < expiry_tick` and at no other time, on the same rules `drawShield`
 * follows and for the same reason: a component read as a state is a state a peer can trust, and a
 * component read as a presence is a claim about a tick nobody checked.
 *
 * Two differences from shield are load-bearing rather than incidental. **A stun window is never
 * zero-length**: `expiry_tick` must strictly exceed `activation_tick`, where every shield endpoint
 * may equal its activation, so there is no cancelled-stun analogue of the cancelled shield that
 * draws nothing. And **a merge grows the window in place**, preserving the activation and taking
 * the maximum expiry, so the interval this renderer reads can get longer between two frames. That
 * costs nothing here precisely because the drawing is a pure function of the published window and
 * the snapshot tick: a grown window simply keeps drawing, with no ramp to run backwards and no
 * remembered duration to contradict. The HUD, which does divide by `expiry - activation`, is where
 * that growth has to be handled.
 *
 * **No wall clock.** `frame.tickSequence` is the only time this renderer knows, exactly as in
 * `shieldRenderer`: elapsed browser time unlocks nothing, which is the rule
 * `sessionSelectors.selectThrustInputOptions` already enforces for the same component. A frame that
 * publishes no tick draws nothing rather than assuming the stun is still running.
 *
 * Geometry comes from the entity's own `physics_body` and from nowhere else, as `drawZoneExposure`,
 * `drawLethalOnContact` and `drawControllableLabel` all do, because stun carries no geometry. The
 * last position is never cached: a stunned body still slides under external impulses -- status
 * application never restores or repeatedly zeroes velocity -- so a remembered centre would mark a
 * place the blob has already left.
 *
 * Own and peer read identically. Whether input is locked is the HUD's business for the own blob;
 * what is on the canvas is "this body cannot fight back", which is the same fact about anyone.
 *
 * On the server's current rules a stunned entity carries no live shield protection -- a stun
 * cancels protection to the cancelling tick, and admission refuses a pulse while a stun is
 * active -- so the violet break and a shield ring are not expected in one frame on one body. This
 * renderer does not depend on that and does not coordinate with `drawShield`: the registry binds
 * one draw function per component kind and gives neither renderer a way to see the other, which is
 * the property that keeps a new kind from having to be threaded through every existing one.
 *
 * Like `drawShield`, this sets only `strokeStyle` and `lineWidth` -- state every renderer that
 * strokes sets unconditionally before it strokes -- so it leaves nothing sticky on the shared
 * surface and needs no `save()`/`restore()` pair, and it composes no `globalAlpha`, which would
 * tint every renderer that ran after it in the same frame.
 */
export function drawStun({
  component,
  entity,
  frame,
}: EntityRenderInput<'stun'>): void {
  const { projection, surface, tickSequence } = frame;
  if (tickSequence === null) {
    // Presence is not a running stun. A frame with no tick cannot say whether this window still
    // covers the world, and drawing anyway would be the presence reading arriving by the back door.
    return;
  }

  const stunned = windowHolds(
    component.activation_tick,
    component.expiry_tick,
    tickSequence,
  );
  if (!stunned) {
    // Expired, or not yet begun. Returning before the body is read also keeps the case the status
    // system is about to clean up off the projection.
    return;
  }

  const body = entity.components.physics_body;
  if (body === undefined) {
    // A stun with no body this frame is a legal frame, not a broken one. `stun` takes no
    // `dependentRequired: ["physics_body"]` edge, and Step 18 declined the same edge on `shield`
    // for the reason `docs/protocol/v3.md` states about both: cleanup runs on the shared respawn
    // sweep rather than at every commit, so a tick in which an entity holds the component without
    // a body is reachable, and the edge would have turned that ordinary tick into a client-wide
    // 1003 close. Returning silently is the whole of what this renderer may do -- there is no blob
    // to mark, and marking the origin would show a stunned body where nobody is standing.
    return;
  }

  if (
    !Number.isFinite(body.position.x) ||
    !Number.isFinite(body.position.y) ||
    !Number.isFinite(body.radius) ||
    body.radius < 0
  ) {
    // `projectWorldPoint` and `projectWorldDistance` raise SIMULATION.CAMERA_PROJECTION_INVALID on
    // an input they cannot project, the draw loop is an unguarded `useEffect`, and there is no
    // error boundary anywhere in this client -- so one throw from here blanks the whole
    // application rather than dropping one mark. Ajv already rejects a non-finite or negative
    // scalar, which makes this guard redundant against a validated frame; it is here because the
    // cost of being wrong about that is the entire canvas. Never construct a projection input that
    // has not been checked.
    return;
  }

  const center = projectWorldPoint(projection, body.position);
  const radiusPixels = Math.max(
    1,
    projectWorldDistance(projection, body.radius),
  );
  const arcRadiusPixels = radiusPixels + STUN_ARC_GAP_PIXELS;

  surface.strokeStyle = STUN_ARC_COLOR;
  surface.lineWidth = STUN_ARC_WIDTH_PIXELS;
  for (let index = 0; index < STUN_ARC_COUNT; index += 1) {
    const start = index * STUN_ARC_SPACING_RADIANS + STUN_ARC_INSET_RADIANS;
    surface.beginPath();
    surface.arc(
      center.x,
      center.y,
      arcRadiusPixels,
      start,
      start + STUN_ARC_SWEEP_RADIANS,
    );
    surface.stroke();
  }
}
