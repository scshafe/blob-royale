import type { EntityRenderInput } from './entityRendering';
import {
  projectWorldDistance,
  projectWorldPoint,
  type WorldPoint,
} from './worldProjection';

/**
 * The two shield colours. Exported because a test that hard-codes a hex string asserts a copy
 * rather than the drawn colour.
 *
 * Cool sky for ordinary protection and a hot gold for the perfect opening. The pair separates on
 * lightness as well as hue, so it survives the red/amber confusion `drawLethalOnContact` names, and
 * neither collides with the danger vocabulary already painted on a body: solid orange and red
 * exposure rings, and the dashed red hazard ring. Protection is not danger, and a shielded blob
 * that read as an endangered one would be worse than no mark at all.
 */
export const SHIELD_PROTECTION_RING_COLOR = '#38bdf8';
export const SHIELD_PERFECT_RING_COLOR = '#facc15';

/**
 * Ring widths and gaps outside the body radius, in logical CSS pixels rather than world units.
 *
 * A terrain cliff rim is authored in world units because it is terrain geometry and must scale with
 * the camera like the ground it rims. A status ring is not geometry: it is a readability affordance
 * hung on a body, and it has to stay legible when the blob is three pixels across on a zoomed-out
 * edge view. Every existing body-status ring in this directory -- `zoneExposureRenderer`,
 * `lethalOnContactRenderer` -- is pixel-sized for that reason, and these follow them.
 *
 * The perfect pair straddles the radius the ordinary ring occupies, so when the opening closes the
 * flash collapses onto the steady ring rather than jumping somewhere else on the blob.
 */
const PROTECTION_RING_WIDTH_PIXELS = 3;
const PROTECTION_RING_GAP_PIXELS = 7;
const PERFECT_RING_WIDTH_PIXELS = 3;
const PERFECT_INNER_RING_GAP_PIXELS = 4;
const PERFECT_OUTER_RING_GAP_PIXELS = 10;

/**
 * The half-open interval every ability window on this wire publishes: `[activation, expiry)`.
 * `tick == expiry` is the first tick the window does not cover, never its last covered tick, and a
 * window whose expiry equals its activation covers nothing at all -- which is exactly what a
 * cancelled shield publishes.
 */
function windowHolds(
  activationTick: number,
  expiryTick: number,
  tick: number,
): boolean {
  return activationTick <= tick && tick < expiryTick;
}

function strokeRing(
  surface: CanvasRenderingContext2D,
  center: WorldPoint,
  radiusPixels: number,
): void {
  surface.beginPath();
  surface.arc(center.x, center.y, radiusPixels, 0, 2 * Math.PI);
  surface.stroke();
}

/**
 * @canonical shield_rendering -- protection is a window the snapshot tick falls inside, and never
 * the presence of a `shield` component.
 *
 * `shield` publishes one `activation_tick` and three absolute endpoints over it
 * (`docs/protocol/schema/v3/shield-component.schema.json`), and the shared ability system erases
 * the component only once protection *and* cooldown have both expired (`docs/protocol/v3.md`, the
 * `shield` component section). A pulse at the tuning ADR 0008 authors protects for 160 ticks inside
 * a component that lives at least 360, and a stun cancels protection by shortening it to the
 * cancelling tick while leaving the cooldown running. So the majority of published shield frames by
 * duration -- and every cancelled one -- carry a live component with no protection whatsoever.
 * Keying the drawing on presence is the obvious reading and the one this renderer rejects: it would
 * paint a protection ring on a player who has none, which is a confident lie told at the exact
 * moment a player is deciding whether to take a hit.
 *
 * Three states, resolved in this order:
 *   - `activation_tick <= tick < perfect_expiry_tick` -> the perfect opening;
 *   - else `activation_tick <= tick < shield_expiry_tick` -> ordinary protection;
 *   - else -> nothing is drawn. A live cooldown is not protection. The cooldown readout belongs to
 *     the HUD, because a world-space renderer pans and scales with the camera and a timer must not.
 * Every window is half-open, so `tick == perfect_expiry_tick` is the first ordinary tick and
 * `tick == shield_expiry_tick` is the first unprotected one.
 *
 * **The perfect mark is truthful whenever it is drawn, and it can be missed entirely.** The opening
 * is 32 ticks at the shipped `ticks_per_second=400` -- 80 ms -- against the 50 ms interval the
 * shipped `snapshots_per_second=20` publishes, so at that cadence one or two snapshots land inside
 * it. It is therefore a STATE mark: it says "this tick is inside the opening", never "an opening
 * happened, here is a flourish of some designed length". A room configured to a slower snapshot
 * cadence can step straight over the window and show no perfect mark at all for a perfect parry
 * that really occurred. That is the honest limit of what this visual can promise, and it is not
 * repairable here: the alternative is a client-side event treatment with a duration of its own,
 * which needs a wall clock this directory deliberately does not have and would go on drawing a
 * state the server has already ended.
 *
 * **No wall clock.** `frame.tickSequence` is the only time this renderer knows. There is no
 * `requestAnimationFrame`, `Date.now` or `performance.now` anywhere under `rendering/`, and there
 * is deliberately none here: elapsed browser time protects nothing, exactly as it unlocks nothing
 * in `sessionSelectors.selectThrustInputOptions`. A frame that publishes no tick draws nothing,
 * because presence is not protection and a guessed tick would be a guessed state.
 *
 * Geometry comes from the entity's own `physics_body` and from nowhere else, as `drawZoneExposure`,
 * `drawLethalOnContact` and `drawControllableLabel` all do, because shield carries no geometry. The
 * last position is never cached: a remembered centre would draw protection where the blob used to
 * be, and the frame a body is missing is the frame a player most needs to be told the truth.
 *
 * Own and peer read identically, unlike exposure. Exposure splits its colour by owner because the
 * question it answers is "am I in trouble". Protection is information about a target you are
 * deciding whether to ram, so it has to say the same thing on every blob on the canvas.
 *
 * Colour and weight rather than `globalAlpha` or a dash. Alpha is surface state that would have to
 * be restored or it would tint every renderer that ran after this one in the same frame -- the
 * reason `drawZoneExposure` refuses an alpha ramp -- and a dash already means "this object kills"
 * in `drawLethalOnContact`'s vocabulary. This renderer therefore sets only `strokeStyle` and
 * `lineWidth`, both of which every renderer that strokes sets unconditionally before it strokes, so
 * it leaves no sticky state behind and needs no `save()`/`restore()` pair.
 */
export function drawShield({
  component,
  entity,
  frame,
}: EntityRenderInput<'shield'>): void {
  const { projection, surface, tickSequence } = frame;
  if (tickSequence === null) {
    // Presence is not protection, so a frame with no tick has nothing to say about this shield.
    // Drawing the ordinary ring here would be the presence reading arriving by the back door.
    return;
  }

  const perfect = windowHolds(
    component.activation_tick,
    component.perfect_expiry_tick,
    tickSequence,
  );
  const protecting =
    perfect ||
    windowHolds(
      component.activation_tick,
      component.shield_expiry_tick,
      tickSequence,
    );
  if (!protecting) {
    // The third state, and the common one: protection over or cancelled while the cooldown still
    // runs. Returning before the body is read also keeps the majority case off the projection.
    return;
  }

  const body = entity.components.physics_body;
  if (body === undefined) {
    // A shield with no body this frame is a legal frame, not a broken one. Step 18 deliberately
    // declined a `dependentRequired: ["physics_body"]` edge on `shield` (`docs/protocol/v3.md`)
    // because cleanup runs on the shared respawn sweep rather than at every commit, so a tick in
    // which an entity holds the component without a body is reachable; the edge would have turned
    // that ordinary tick into a client-wide 1003 close. Returning silently is the whole of what
    // this renderer may do -- there is no blob to ring, and ringing the origin would draw
    // protection at a place nobody is standing.
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
    // application rather than dropping one ring. Ajv already rejects a non-finite or negative
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

  if (perfect) {
    surface.strokeStyle = SHIELD_PERFECT_RING_COLOR;
    surface.lineWidth = PERFECT_RING_WIDTH_PIXELS;
    strokeRing(surface, center, radiusPixels + PERFECT_INNER_RING_GAP_PIXELS);
    strokeRing(surface, center, radiusPixels + PERFECT_OUTER_RING_GAP_PIXELS);
    return;
  }

  surface.strokeStyle = SHIELD_PROTECTION_RING_COLOR;
  surface.lineWidth = PROTECTION_RING_WIDTH_PIXELS;
  strokeRing(surface, center, radiusPixels + PROTECTION_RING_GAP_PIXELS);
}
