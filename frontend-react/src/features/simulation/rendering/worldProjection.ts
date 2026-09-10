import { SimulationApiError } from '../SimulationApiError';

/** A finite world-space position or displacement; camera centres may be outside map bounds. */
export interface WorldPoint {
  readonly x: number;
  readonly y: number;
}

/** Visible dimensions in logical CSS pixels, independent of backing-buffer device-pixel ratio. */
export interface CanvasViewport {
  readonly width: number;
  readonly height: number;
}

/** One uniform world-to-view transform, expressed entirely in logical CSS pixels. */
export interface WorldProjection {
  readonly scale: number;
  readonly offsetX: number;
  readonly offsetY: number;
}

function requireFinite(
  value: number,
  field: string,
  operation: string,
  strictlyPositive = false,
): void {
  if (!Number.isFinite(value) || (strictlyPositive && value <= 0)) {
    throw new SimulationApiError(
      'SIMULATION.CAMERA_PROJECTION_INVALID',
      `Cannot ${operation}: ${field} must be finite${strictlyPositive ? ' and positive' : ''}.`,
      { context: { field, operation, value } },
    );
  }
}

function requireProjection(
  projection: WorldProjection,
  operation: string,
): void {
  requireFinite(projection.scale, 'scale', operation, true);
  requireFinite(projection.offsetX, 'offset_x', operation);
  requireFinite(projection.offsetY, 'offset_y', operation);
}

/**
 * @canonical world_projection -- the single world-to-view transform for every geometry renderer.
 *
 * A finite centre, positive logical viewport, and positive CSS-pixels-per-world-unit scale produce
 * an immutable transform. Map bounds and device-pixel ratio do not select the visible world extent.
 * Invalid inputs or arithmetic overflow raise SIMULATION.CAMERA_PROJECTION_INVALID; no view is
 * clamped or silently substituted. Related: projectWorldPoint, unprojectCanvasOffset.
 */
export function createWorldProjection(
  center: WorldPoint,
  viewport: CanvasViewport,
  scale: number,
): WorldProjection {
  const operation = 'create world projection';
  requireFinite(center.x, 'center_x', operation);
  requireFinite(center.y, 'center_y', operation);
  requireFinite(viewport.width, 'viewport_width', operation, true);
  requireFinite(viewport.height, 'viewport_height', operation, true);
  requireFinite(scale, 'scale', operation, true);
  const projection = {
    scale,
    offsetX: viewport.width / 2 - center.x * scale,
    offsetY: viewport.height / 2 - center.y * scale,
  };
  requireProjection(projection, operation);
  return Object.freeze(projection);
}

/**
 * Project a finite world point to immutable logical CSS-pixel coordinates, without clipping.
 * Invalid projections, coordinates, or overflow raise SIMULATION.CAMERA_PROJECTION_INVALID.
 */
export function projectWorldPoint(
  projection: WorldProjection,
  point: WorldPoint,
): WorldPoint {
  const operation = 'project world point';
  requireProjection(projection, operation);
  requireFinite(point.x, 'point_x', operation);
  requireFinite(point.y, 'point_y', operation);
  const x = point.x * projection.scale + projection.offsetX;
  const y = point.y * projection.scale + projection.offsetY;
  requireFinite(x, 'projected_x', operation);
  requireFinite(y, 'projected_y', operation);
  return Object.freeze({ x, y });
}

/**
 * Project a finite nonnegative world distance to CSS pixels with the same scale on both axes.
 * Invalid projections, distances, or overflow raise SIMULATION.CAMERA_PROJECTION_INVALID.
 */
export function projectWorldDistance(
  projection: WorldProjection,
  distance: number,
): number {
  const operation = 'project world distance';
  requireProjection(projection, operation);
  requireFinite(distance, 'distance', operation);
  if (distance < 0) {
    throw new SimulationApiError(
      'SIMULATION.CAMERA_PROJECTION_INVALID',
      'Cannot project world distance: distance must be nonnegative.',
      { context: { distance, operation } },
    );
  }
  const projectedDistance = distance * projection.scale;
  requireFinite(projectedDistance, 'projected_distance', operation);
  return projectedDistance;
}

/**
 * Convert a finite signed CSS-pixel displacement to immutable world units, ignoring translation.
 * Pan policy chooses the sign; this inverse never moves a camera or emits a gameplay command.
 * Invalid projections, displacements, or overflow raise SIMULATION.CAMERA_PROJECTION_INVALID.
 */
export function unprojectCanvasOffset(
  projection: WorldProjection,
  offset: WorldPoint,
): WorldPoint {
  const operation = 'unproject canvas offset';
  requireProjection(projection, operation);
  requireFinite(offset.x, 'offset_x', operation);
  requireFinite(offset.y, 'offset_y', operation);
  const x = offset.x / projection.scale;
  const y = offset.y / projection.scale;
  requireFinite(x, 'world_offset_x', operation);
  requireFinite(y, 'world_offset_y', operation);
  return Object.freeze({ x, y });
}
