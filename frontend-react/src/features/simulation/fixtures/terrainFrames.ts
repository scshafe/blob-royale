import { validateSessionWelcomeMessage } from '../sessionProtocolValidation';
import type { SessionTerrain } from '../simulationProtocolTypes';
import { MAXIMUM_TERRAIN_WORLD_SCALAR, welcomeDocument } from './sessionFrames';

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

/** Exact canonical width ceiling, for the corresponding maximum gate fixture. */
export const maximumRaceTerrain = validatedTerrain({
  ...raceTerrain,
  corridors: raceTerrain.corridors.map((corridor) => ({
    ...corridor,
    half_width: MAXIMUM_TERRAIN_WORLD_SCALAR,
  })),
});

/** Two explicitly named corridors; the unselected one is deliberately too narrow for the gates. */
export function namedRaceTerrain(
  roadName: string,
  selectedFirst = false,
): SessionTerrain {
  const selected = raceTerrain.corridors.find(
    (corridor) => corridor.name === 'road',
  );
  if (selected === undefined) throw new Error('TEST.RACE_TERRAIN_ROAD_MISSING');
  const corridors = [
    { ...selected, name: 'unselected', half_width: 10 },
    { ...selected, name: roadName },
  ];
  return validatedTerrain({
    ...raceTerrain,
    corridors: selectedFirst ? corridors.reverse() : corridors,
  });
}
