import type { SessionTerrain } from '../simulationProtocolTypes';
import type { ModeStateRenderFrame } from './modeStateRendering';
import {
  projectWorldDistance,
  projectWorldPoint,
  type WorldProjection,
} from './worldProjection';

export const TERRAIN_SOLID_FILL = '#f8fafc';
export const TERRAIN_CORRIDOR_FILL = '#dbeafe';

/**
 * The unsupported region, and the lip that bounds it.
 *
 * The void is a mid tone under a near-black rim: light enough that the map border `SimulationCanvas`
 * strokes from `configuration.world` in `#334155` and an obstacle disc (`physicsBodyRenderer`'s
 * `#475569`) both stay legible over it, dark enough against `#f8fafc` ground and `#dbeafe` road that
 * the drop reads as depth rather than as a second surface. Neither value is any entity layer's
 * warning colour: the hazard ring, the two exposure rings and the race gates all keep their meaning
 * over a pit. Exported because a test that hard-codes a hex string asserts a copy rather than the
 * drawn colour.
 */
export const TERRAIN_VOID_FILL = '#94a3b8';
export const TERRAIN_CLIFF_RIM = '#0f172a';

/**
 * The rim's visible thickness on the ground side of a cliff, in WORLD units.
 *
 * It is projected through `projectWorldDistance` like every other world quantity, so it scales with
 * the camera exactly as bodies, gates and road widths do. A rim authored in logical pixels would eat
 * further into a narrow road every time the view zoomed out, and vanish as it zoomed in — the rim
 * would then describe the camera rather than the map.
 *
 * Both techniques below spend it twice about the boundary and keep half, so the two measure the same
 * rim: a hole rim strokes `2 *` this width centred on the hole circle and loses its inner half to
 * the complement clip, and a corridor underprint adds `2 *` this width to the road's own width and
 * loses the middle to the surface pass.
 */
export const TERRAIN_CLIFF_RIM_WORLD_UNITS = 2;

/**
 * Traces one corridor polyline into the current path, projected but not stroked.
 *
 * The rim underprint and the road surface are the same polyline at two widths, so the trace is
 * shared: a second copy of this loop is how the two passes would come to disagree about a point.
 */
function traceCorridor(
  surface: CanvasRenderingContext2D,
  projection: WorldProjection,
  points: SessionTerrain['corridors'][number]['points'],
): void {
  surface.beginPath();
  points.forEach((node, index) => {
    const point = projectWorldPoint(projection, node);
    if (index === 0) surface.moveTo(point.x, point.y);
    else surface.lineTo(point.x, point.y);
  });
}

/**
 * @canonical terrain_rendering -- bounded positive ground minus the UNION of all open holes, the
 * cliff rim that bounds that set, and the void underneath it.
 *
 * All geometry uses the existing world projection in logical CSS pixels. Each hole contributes
 * its own complement clip, so their intersection subtracts the union (never an overlapping-hole
 * XOR). The clips precede every opaque capsule stroke, preventing a later road from refilling a
 * hole. Canvas rasterization only approximates the mathematical boundary at pixel resolution.
 * No map is inferred from a mode, a snapshot mirror, or configuration when welcome is absent.
 *
 * **The void is painted, not the bodies over it.** The obvious reading of "void feedback beneath
 * entities" is a mark under a body whose centre is unsupported, and the client must not draw that.
 * Deciding it means a second implementation of `terrain_supports_point`
 * (`src/simulation/terrain_queries.hpp`) including its asymmetric tolerances — supported roads
 * include their edge plus `kPositionTolerance` while the envelope stays exact — which that
 * function's own canonical note ("no consumer owns another support predicate") and ADR 0008 both
 * forbid. It would also mark a state no snapshot contains: a
 * ground-bound body terminates at its last supported point and loses its body on the same tick, so
 * the only bodies ever over void are `ground_attachment: floating` hazards, which never fall — a
 * mark on one would be a confident lie. Painting the region instead needs no per-body verdict and no
 * second predicate, and it satisfies "beneath every entity layer" in the z-order sense the layer
 * stack already means, because terrain is the bottom layer `SimulationCanvas` draws.
 *
 * **The void underlay is the one paint that precedes the hole complements**, and that is deliberate:
 * it has to show THROUGH them. The envelope is filled with the void colour first, then the ground
 * passes paint over everything that is supported — inside the clip stack for holes, at road width
 * for corridors — so the region still showing the underlay is exactly *envelope minus supported
 * ground*. No region is computed; it is what the ground passes leave behind. Every ground paint
 * still follows every clip, so no later road can refill a hole.
 *
 * **Hole rims are stroked inside the live clip stack**, after the ground pass and before the
 * `restore()` that pops it. The placement is load-bearing, not tidiness: while those complements are
 * live the region is precisely ground minus the union, so the inner half of each circle's stroke is
 * clipped away and any arc interior to an overlapping neighbour disappears with it. What survives is
 * the outline of the union, correct by construction rather than by extra logic. Stroking before the
 * clips, or after the `restore()`, would paint rims straight through the interior of the void.
 *
 * **Corridor rims are an underprint.** Canvas exposes no outline operation for a wide stroke, and
 * the compiled corridor boundary is `simulation::detail`, deliberately not wire data — so every
 * polyline is stroked in the rim colour at `2 * half_width + 2 * rim` and then in the road colour at
 * `2 * half_width` over it. ALL rim passes precede ALL surface passes: interleaved, one road's rim
 * would overprint a crossing road's surface. The union of the wide strokes minus the union of the
 * narrow ones is exactly the road-union outline, so overlapping corridors need no special case.
 *
 * **The arena border is not a cliff and is not drawn here.** A hole edge kills; the map edge folds a
 * blob back — ADR 0008's outer-map decision cancels outward displacement and velocity at the
 * envelope rather than dropping anything through it. `SimulationCanvas` strokes that border from
 * `configuration.world` and keeps that stroke: restyling it as a cliff would both introduce the
 * second geometry source this renderer exists to avoid and tell the player the wrong thing about
 * what happens there.
 *
 * A hole authored entirely off-road on a corridor map still receives a rim, because the clip stack
 * knows the hole union but not the road union, and deriving the road union as a path would be that
 * same second geometry implementation. It marks a pit inside the void rather than a false surface,
 * and no shipped map or fixture authors one.
 */
export function drawTerrain(
  terrain: SessionTerrain,
  { projection, surface }: ModeStateRenderFrame,
): void {
  const origin = projectWorldPoint(projection, { x: 0, y: 0 });
  const width = projectWorldDistance(
    projection,
    terrain.bounds.width_world_units,
  );
  const height = projectWorldDistance(
    projection,
    terrain.bounds.height_world_units,
  );
  surface.save();
  surface.beginPath();
  surface.rect(origin.x, origin.y, width, height);
  surface.clip();
  surface.fillStyle = TERRAIN_VOID_FILL;
  surface.fillRect(origin.x, origin.y, width, height);
  for (const hole of terrain.holes) {
    const center = projectWorldPoint(projection, hole.center);
    const radius = projectWorldDistance(projection, hole.radius);
    surface.beginPath();
    surface.rect(origin.x, origin.y, width, height);
    surface.moveTo(center.x + radius, center.y);
    surface.arc(center.x, center.y, radius, 0, 2 * Math.PI);
    surface.clip('evenodd');
  }
  // One cap and join convention for the whole pass: the canonical positive ground of a corridor map
  // is a union of capsules, so a road's ends and bends are round and the rim offsetting that shape
  // must be too. A hole rim is one closed circle with neither a cap nor a join, so settling the
  // state here decides nothing about it and leaves no branch to keep in step.
  surface.lineCap = 'round';
  surface.lineJoin = 'round';
  if (terrain.ground === 'solid') {
    surface.fillStyle = TERRAIN_SOLID_FILL;
    surface.fillRect(origin.x, origin.y, width, height);
  } else {
    surface.strokeStyle = TERRAIN_CLIFF_RIM;
    for (const corridor of terrain.corridors) {
      surface.lineWidth = projectWorldDistance(
        projection,
        2 * corridor.half_width + 2 * TERRAIN_CLIFF_RIM_WORLD_UNITS,
      );
      traceCorridor(surface, projection, corridor.points);
      surface.stroke();
    }
    surface.strokeStyle = TERRAIN_CORRIDOR_FILL;
    for (const corridor of terrain.corridors) {
      surface.lineWidth = projectWorldDistance(
        projection,
        2 * corridor.half_width,
      );
      traceCorridor(surface, projection, corridor.points);
      surface.stroke();
    }
  }
  surface.strokeStyle = TERRAIN_CLIFF_RIM;
  surface.lineWidth = projectWorldDistance(
    projection,
    2 * TERRAIN_CLIFF_RIM_WORLD_UNITS,
  );
  for (const hole of terrain.holes) {
    const center = projectWorldPoint(projection, hole.center);
    surface.beginPath();
    surface.arc(
      center.x,
      center.y,
      projectWorldDistance(projection, hole.radius),
      0,
      2 * Math.PI,
    );
    surface.stroke();
  }
  surface.restore();
}
