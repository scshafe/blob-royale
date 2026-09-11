import type { SessionEntitySnapshot } from '../simulationProtocolTypes';
import { validatedTerrain } from './terrainFrames';

/** A capture circle may overlap this hole; the circle does not fill or remove it. */
export const hillRoamingTerrain = validatedTerrain({
  bounds: { width_world_units: 960, height_world_units: 640 },
  ground: 'solid',
  corridors: [],
  holes: [{ name: 'cliff', center: { x: 300, y: 150 }, radius: 40 }],
});

export const hillRoamingRenderCases: readonly {
  readonly name: string;
  readonly entity: SessionEntitySnapshot;
}[] = [
  {
    name: 'moving across a hole',
    entity: {
      entity_id: 90,
      components: {
        hill: { center: { x: 300, y: 150 }, radius: 90 },
        hill_motion: { velocity: { x: 20, y: 30 } },
      },
    },
  },
  {
    name: 'stopped at an outer boundary with circle overhang',
    entity: {
      entity_id: 90,
      components: {
        hill: { center: { x: 960, y: 640 }, radius: 90 },
        hill_motion: { velocity: { x: 0, y: 0 } },
      },
    },
  },
];
