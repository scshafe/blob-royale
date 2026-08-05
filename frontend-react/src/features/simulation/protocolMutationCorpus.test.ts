import { describe, expect, it } from 'vitest';

import {
  configurationResponseExample,
  errorResponseExample,
  snapshotMessageExample,
} from './fixtures/protocolV1Examples';
import {
  validateSimulationConfigurationResponse,
  validateSimulationHttpErrorResponse,
  validateSimulationSnapshotMessage,
} from './simulationProtocolValidation';

interface MutationCase<T> {
  readonly name: string;
  readonly mutate: (document: T) => void;
}

function requireFirstPlayer(document: typeof snapshotMessageExample) {
  const player = document.data.players[0];
  if (player === undefined) {
    throw new Error('TEST.SNAPSHOT_FIXTURE_EMPTY');
  }
  return player;
}

const configurationMutations: readonly MutationCase<
  typeof configurationResponseExample
>[] = [
  {
    name: 'missing schema identifier',
    mutate: (document) => {
      Reflect.deleteProperty(document.meta, 'schema_id');
    },
  },
  {
    name: 'unknown envelope field',
    mutate: (document) => {
      Reflect.set(document, 'debug', true);
    },
  },
  {
    name: 'coercible numeric string',
    mutate: (document) => {
      Reflect.set(document.data.world, 'width_world_units', '960');
    },
  },
  {
    name: 'non-finite world scalar',
    mutate: (document) => {
      document.data.world.width_world_units = Number.POSITIVE_INFINITY;
    },
  },
  {
    name: 'zero presentation rate',
    mutate: (document) => {
      document.data.presentation.snapshots_per_second = 0;
    },
  },
  {
    name: 'unregistered protocol version',
    mutate: (document) => {
      document.meta.protocol_version = '2.0';
    },
  },
];

const errorMutations: readonly MutationCase<typeof errorResponseExample>[] = [
  {
    name: 'unknown error code',
    mutate: (document) => {
      document.error.code = 'PROTOCOL.UNKNOWN';
    },
  },
  {
    name: 'missing retryability',
    mutate: (document) => {
      Reflect.deleteProperty(document.error, 'retryable');
    },
  },
  {
    name: 'oversized human message',
    mutate: (document) => {
      document.error.message = 'x'.repeat(1_025);
    },
  },
  {
    name: 'unknown error detail',
    mutate: (document) => {
      Reflect.set(document.error.details, 'implementation_detail', true);
    },
  },
  {
    name: 'malformed request identifier',
    mutate: (document) => {
      document.meta.request_id = '../not-an-id';
    },
  },
];

const snapshotMutations: readonly MutationCase<
  typeof snapshotMessageExample
>[] = [
  {
    name: 'missing tick sequence',
    mutate: (document) => {
      Reflect.deleteProperty(document.data, 'tick_sequence');
    },
  },
  {
    name: 'players changed to an object',
    mutate: (document) => {
      Reflect.set(document.data, 'players', {});
    },
  },
  {
    name: 'unknown player field',
    mutate: (document) => {
      Reflect.set(requireFirstPlayer(document), 'mass', 1);
    },
  },
  {
    name: 'fractional entity identifier',
    mutate: (document) => {
      requireFirstPlayer(document).entity_id = 1.5;
    },
  },
  {
    name: 'non-finite velocity',
    mutate: (document) => {
      requireFirstPlayer(document).velocity.x = Number.NaN;
    },
  },
  {
    name: 'zero transport sequence',
    mutate: (document) => {
      document.meta.message_sequence = 0;
    },
  },
  {
    name: 'invalid UTC timestamp',
    mutate: (document) => {
      document.meta.sent_at_utc = '2026-08-04 12:00:00';
    },
  },
];

describe('protocol v1 deterministic mutation corpus', () => {
  it.each(configurationMutations)(
    'rejects configuration mutation: $name',
    ({ mutate }) => {
      const document = structuredClone(configurationResponseExample);
      mutate(document);

      expect(() =>
        validateSimulationConfigurationResponse(
          document,
          configurationResponseExample.meta.request_id,
        ),
      ).toThrow();
    },
  );

  it.each(errorMutations)('rejects error mutation: $name', ({ mutate }) => {
    const document = structuredClone(errorResponseExample);
    mutate(document);

    expect(() =>
      validateSimulationHttpErrorResponse(
        document,
        405,
        errorResponseExample.meta.request_id,
      ),
    ).toThrow();
  });

  const configuration = validateSimulationConfigurationResponse(
    structuredClone(configurationResponseExample),
    configurationResponseExample.meta.request_id,
  ).data;

  it.each(snapshotMutations)(
    'rejects snapshot mutation: $name',
    ({ mutate }) => {
      const document = structuredClone(snapshotMessageExample);
      document.meta.message_sequence = 1;
      mutate(document);

      expect(() =>
        validateSimulationSnapshotMessage(document, configuration, null),
      ).toThrow();
    },
  );
});
