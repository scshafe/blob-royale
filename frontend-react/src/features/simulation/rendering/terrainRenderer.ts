import type { SessionTerrain } from '../simulationProtocolTypes';
import type { ModeStateRenderFrame } from './modeStateRendering';
import { projectWorldDistance, projectWorldPoint } from './worldProjection';

export const TERRAIN_SOLID_FILL = '#f8fafc';
export const TERRAIN_CORRIDOR_FILL = '#dbeafe';

/**
 * @canonical terrain_rendering -- bounded positive ground minus the UNION of all open holes.
 * All geometry uses the existing world projection in logical CSS pixels. Each hole contributes
 * its own complement clip, so their intersection subtracts the union (never an overlapping-hole
 * XOR). The clips precede every opaque capsule stroke, preventing a later road from refilling a
 * hole. Canvas rasterization only approximates the mathematical boundary at pixel resolution.
 * No map is inferred from a mode, a snapshot mirror, or configuration when welcome is absent.
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
  for (const hole of terrain.holes) {
    const center = projectWorldPoint(projection, hole.center);
    const radius = projectWorldDistance(projection, hole.radius);
    surface.beginPath();
    surface.rect(origin.x, origin.y, width, height);
    surface.moveTo(center.x + radius, center.y);
    surface.arc(center.x, center.y, radius, 0, 2 * Math.PI);
    surface.clip('evenodd');
  }
  if (terrain.ground === 'solid') {
    surface.fillStyle = TERRAIN_SOLID_FILL;
    surface.fillRect(origin.x, origin.y, width, height);
  } else {
    surface.strokeStyle = TERRAIN_CORRIDOR_FILL;
    surface.lineCap = 'round';
    surface.lineJoin = 'round';
    for (const corridor of terrain.corridors) {
      surface.lineWidth = projectWorldDistance(
        projection,
        2 * corridor.half_width,
      );
      surface.beginPath();
      corridor.points.forEach((node, index) => {
        const point = projectWorldPoint(projection, node);
        if (index === 0) surface.moveTo(point.x, point.y);
        else surface.lineTo(point.x, point.y);
      });
      surface.stroke();
    }
  }
  surface.restore();
}
