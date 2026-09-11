import { validateSessionWelcomeMessage } from '../sessionProtocolValidation';
import type { SessionTerrain } from '../simulationProtocolTypes';
import { welcomeDocument } from './sessionFrames';

/** Test-only admission through the same schema and semantics used by the transport. */
export function validatedTerrain(terrain: unknown): SessionTerrain {
  const document = welcomeDocument();
  return validateSessionWelcomeMessage(
    {
      ...document,
      data: { ...document.data, terrain },
    },
    null,
  ).data.terrain;
}

export const solidTerrain = validatedTerrain(welcomeDocument().data.terrain);

/** Authored test road matching the golden race frame; it is not inferred by a renderer. */
export const raceTerrain = validatedTerrain({
  bounds: { width_world_units: 960, height_world_units: 640 },
  ground: 'corridors',
  corridors: [
    {
      name: 'road',
      half_width: 60,
      points: [
        { x: 100, y: 100 },
        { x: 700, y: 100 },
        { x: 700, y: 500 },
      ],
    },
  ],
  holes: [],
});
