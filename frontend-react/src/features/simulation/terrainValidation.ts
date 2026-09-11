import { SimulationApiError } from './SimulationApiError';
import { assertNoNegativeZero } from './protocolValidationSupport';
import type {
  SessionTerrain,
  SimulationConfiguration,
} from './simulationProtocolTypes';

function invalidTerrain(field: string, reason: string): never {
  throw new SimulationApiError(
    'SIMULATION.SESSION_INVARIANT_VIOLATION',
    `Session terrain ${reason}.`,
    { context: { field } },
  );
}

/**
 * @canonical session_terrain_semantics -- semantic admission after the generated welcome schema.
 * The schema owns shape, scalar ranges, name grammar, and individual collection limits; this
 * check owns aggregate limits, identity, containment, and representable nonzero segment lengths.
 * Invalid authored geometry fails visibly, without clipping, repair, or a substitute map.
 */
export function assertTerrainSemantics(terrain: SessionTerrain): void {
  const names = new Set<string>();
  let pointCount = 0;
  let segmentCount = 0;
  function requireContained(
    point: { readonly x: number; readonly y: number },
    field: string,
  ) {
    assertNoNegativeZero(point.x, `${field}.x`);
    assertNoNegativeZero(point.y, `${field}.y`);
    if (
      point.x > terrain.bounds.width_world_units ||
      point.y > terrain.bounds.height_world_units
    ) {
      invalidTerrain(field, 'has a point outside its closed bounds');
    }
  }
  for (const corridor of terrain.corridors) {
    if (names.has(corridor.name))
      invalidTerrain('corridors.name', 'has duplicate corridor names');
    names.add(corridor.name);
    pointCount += corridor.points.length;
    segmentCount += corridor.points.length - 1;
    let previous: { readonly x: number; readonly y: number } | null = null;
    for (const point of corridor.points) {
      requireContained(point, 'corridors.points');
      if (previous !== null) {
        const dx = point.x - previous.x;
        const dy = point.y - previous.y;
        const squaredLength = dx * dx + dy * dy;
        if (!Number.isFinite(squaredLength) || squaredLength <= 0) {
          invalidTerrain(
            'corridors.points',
            'has a nonrepresentable or zero-length segment',
          );
        }
      }
      previous = point;
    }
  }
  if (pointCount > 40 || segmentCount > 32) {
    invalidTerrain(
      'corridors.points',
      'exceeds the aggregate 40-point or 32-segment limit',
    );
  }
  names.clear();
  for (const hole of terrain.holes) {
    if (names.has(hole.name))
      invalidTerrain('holes.name', 'has duplicate hole names');
    names.add(hole.name);
    requireContained(hole.center, 'holes.center');
  }
}

/** Checked at the transport boundary before welcome sequence state or callbacks are published. */
export function assertTerrainConfiguration(
  terrain: SessionTerrain,
  configuration: SimulationConfiguration,
): void {
  if (
    terrain.bounds.width_world_units !==
      configuration.world.width_world_units ||
    terrain.bounds.height_world_units !== configuration.world.height_world_units
  ) {
    invalidTerrain(
      'bounds',
      'disagrees with the validated v1 configuration bounds',
    );
  }
}
