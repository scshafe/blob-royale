import { describe, expect, it } from 'vitest';

import goldens from '../../../../tests/fixtures/terrain/geometry-goldens.json';
import { configurationResponseExample } from './fixtures/protocolV1Examples';
import { validatedTerrain } from './fixtures/terrainFrames';
import { snapshotDocument, welcomeDocument } from './fixtures/sessionFrames';
import {
  validateSessionSnapshotMessage,
  validateSessionWelcomeMessage,
} from './sessionProtocolValidation';
import { validateSimulationConfigurationResponse } from './simulationProtocolValidation';
import { assertTerrainConfiguration } from './terrainValidation';

function terrainDocument() {
  return {
    bounds: { width_world_units: 960, height_world_units: 640 },
    ground: 'corridors',
    corridors: [
      {
        name: 'road',
        half_width: 40,
        points: [
          { x: 0, y: 0 },
          { x: 960, y: 640 },
        ],
      },
    ],
    holes: [{ name: 'pit', center: { x: 480, y: 320 }, radius: 10 }],
  };
}

describe('welcome terrain admission', () => {
  it.each(goldens.cases)(
    'admits the shared $name geometry and freezes authored values',
    ({ terrain }) => {
      const accepted = validatedTerrain(terrain);
      expect(accepted).toEqual(terrain);
      expect(Object.isFrozen(accepted)).toBe(true);
      expect(Object.isFrozen(accepted.bounds)).toBe(true);
      expect(Object.isFrozen(accepted.corridors)).toBe(true);
      expect(Object.isFrozen(accepted.holes)).toBe(true);
      const configurationDocument = structuredClone(
        configurationResponseExample,
      );
      configurationDocument.data.world = {
        ...configurationDocument.data.world,
        ...terrain.bounds,
      };
      const configuration = validateSimulationConfigurationResponse(
        configurationDocument,
        configurationDocument.meta.request_id,
      ).data;
      expect(() =>
        assertTerrainConfiguration(accepted, configuration),
      ).not.toThrow();
    },
  );

  const mutations: readonly {
    name: string;
    mutate: (terrain: ReturnType<typeof terrainDocument>) => void;
  }[] = [
    {
      name: 'missing ground',
      mutate: (t) => {
        Reflect.deleteProperty(t, 'ground');
      },
    },
    {
      name: 'unknown terrain field',
      mutate: (t) => {
        Reflect.set(t, 'compiled', []);
      },
    },
    {
      name: 'unknown bounds field',
      mutate: (t) => {
        Reflect.set(t.bounds, 'x', 0);
      },
    },
    {
      name: 'unknown corridor field',
      mutate: (t) => {
        Reflect.set(t.corridors[0]!, 'closed', false);
      },
    },
    {
      name: 'unknown point field',
      mutate: (t) => {
        Reflect.set(t.corridors[0]!.points[0]!, 'z', 0);
      },
    },
    {
      name: 'unknown hole field',
      mutate: (t) => {
        Reflect.set(t.holes[0]!, 'depth', 1);
      },
    },
    {
      name: 'unknown center field',
      mutate: (t) => {
        Reflect.set(t.holes[0]!.center, 'z', 0);
      },
    },
    {
      name: 'solid with corridors',
      mutate: (t) => {
        t.ground = 'solid';
      },
    },
    {
      name: 'corridor ground without corridors',
      mutate: (t) => {
        t.corridors = [];
      },
    },
    {
      name: 'unknown ground',
      mutate: (t) => {
        t.ground = 'water';
      },
    },
    {
      name: 'zero width',
      mutate: (t) => {
        t.corridors[0]!.half_width = 0;
      },
    },
    {
      name: 'zero radius',
      mutate: (t) => {
        t.holes[0]!.radius = 0;
      },
    },
    {
      name: 'negative width',
      mutate: (t) => {
        t.bounds.width_world_units = -1;
      },
    },
    {
      name: 'infinite width',
      mutate: (t) => {
        t.corridors[0]!.half_width = Infinity;
      },
    },
    {
      name: 'NaN radius',
      mutate: (t) => {
        t.holes[0]!.radius = NaN;
      },
    },
    {
      name: 'infinite bound',
      mutate: (t) => {
        t.bounds.height_world_units = Infinity;
      },
    },
    {
      name: 'width above ceiling',
      mutate: (t) => {
        t.corridors[0]!.half_width = 1_000_000_001;
      },
    },
    {
      name: 'radius above ceiling',
      mutate: (t) => {
        t.holes[0]!.radius = 1_000_000_001;
      },
    },
    {
      name: 'bound above ceiling',
      mutate: (t) => {
        t.bounds.height_world_units = 1_000_000_001;
      },
    },
    {
      name: 'point outside envelope',
      mutate: (t) => {
        t.corridors[0]!.points[1]!.x = 961;
      },
    },
    {
      name: 'negative point',
      mutate: (t) => {
        t.corridors[0]!.points[0]!.y = -1;
      },
    },
    {
      name: 'negative-zero point x',
      mutate: (t) => {
        t.corridors[0]!.points[0]!.x = -0;
      },
    },
    {
      name: 'negative-zero point y',
      mutate: (t) => {
        t.corridors[0]!.points[0]!.y = -0;
      },
    },
    {
      name: 'negative-zero center x',
      mutate: (t) => {
        t.holes[0]!.center.x = -0;
      },
    },
    {
      name: 'negative-zero center y',
      mutate: (t) => {
        t.holes[0]!.center.y = -0;
      },
    },
    {
      name: 'nonfinite point',
      mutate: (t) => {
        t.corridors[0]!.points[0]!.y = NaN;
      },
    },
    {
      name: 'center outside envelope',
      mutate: (t) => {
        t.holes[0]!.center.y = 641;
      },
    },
    {
      name: 'nonfinite center',
      mutate: (t) => {
        t.holes[0]!.center.x = Infinity;
      },
    },
    {
      name: 'duplicate corridor name',
      mutate: (t) => {
        t.corridors.push(structuredClone(t.corridors[0]!));
      },
    },
    {
      name: 'duplicate hole name',
      mutate: (t) => {
        t.holes.push(structuredClone(t.holes[0]!));
      },
    },
    {
      name: 'invalid corridor name',
      mutate: (t) => {
        t.corridors[0]!.name = 'Road';
      },
    },
    {
      name: 'long hole name',
      mutate: (t) => {
        t.holes[0]!.name = 'a'.repeat(65);
      },
    },
    {
      name: 'empty corridor name',
      mutate: (t) => {
        t.corridors[0]!.name = '';
      },
    },
    {
      name: 'zero length',
      mutate: (t) => {
        t.corridors[0]!.points[1] = { x: 0, y: 0 };
      },
    },
    {
      name: 'underflowed squared length',
      mutate: (t) => {
        t.corridors[0]!.points[1] = { x: 1e-170, y: 0 };
      },
    },
    {
      name: 'one point',
      mutate: (t) => {
        t.corridors[0]!.points.pop();
      },
    },
    {
      name: 'nine corridors',
      mutate: (t) => {
        t.corridors = Array.from({ length: 9 }, (_, i) => ({
          ...t.corridors[0]!,
          name: `road_${i}`,
        }));
      },
    },
    {
      name: 'thirty-three holes',
      mutate: (t) => {
        t.holes = Array.from({ length: 33 }, (_, i) => ({
          ...t.holes[0]!,
          name: `pit_${i}`,
        }));
      },
    },
    {
      name: 'aggregate segments overflow',
      mutate: (t) => {
        t.corridors = Array.from({ length: 2 }, (_, i) => ({
          ...t.corridors[0]!,
          name: `road_${i}`,
          points: Array.from({ length: 18 }, (_, x) => ({ x, y: i })),
        }));
      },
    },
    {
      name: 'aggregate points overflow',
      mutate: (t) => {
        t.corridors = Array.from({ length: 8 }, (_, i) => ({
          ...t.corridors[0]!,
          name: `road_${i}`,
          points: Array.from({ length: i === 0 ? 6 : 5 }, (_, x) => ({
            x,
            y: i,
          })),
        }));
      },
    },
  ];
  it.each(mutations)('rejects $name without repair', ({ mutate }) => {
    const terrain = terrainDocument();
    mutate(terrain);
    expect(() => validatedTerrain(terrain)).toThrow();
  });

  it('admits all aggregate ceilings, closed bounds, and independent name families', () => {
    const terrain = terrainDocument();
    terrain.bounds = { width_world_units: 1e9, height_world_units: 1e9 };
    terrain.corridors = Array.from({ length: 8 }, (_, i) => ({
      name: i === 0 ? 'a'.repeat(64) : `shape_${i}`,
      half_width: 1e9,
      points: Array.from({ length: 5 }, (_, x) => ({
        x: x * 250_000_000,
        y: i * 100_000_000,
      })),
    }));
    terrain.holes = Array.from({ length: 32 }, (_, i) => ({
      name: i === 0 ? 'a'.repeat(64) : `shape_${i}`,
      center: { x: 1e9, y: 1e9 },
      radius: 1e9,
    }));
    expect(validatedTerrain(terrain)).toEqual(terrain);
  });

  it('requires terrain in welcome and rejects terrain on snapshots', () => {
    const welcome = welcomeDocument();
    Reflect.deleteProperty(welcome.data, 'terrain');
    expect(() => validateSessionWelcomeMessage(welcome, null)).toThrow();
    const snapshot = snapshotDocument();
    Reflect.set(snapshot.data, 'terrain', terrainDocument());
    expect(() =>
      validateSessionSnapshotMessage(snapshot, {
        messageSequence: 1,
        requestId: snapshot.meta.request_id,
        tickSequence: null,
      }),
    ).toThrow();
  });
});
