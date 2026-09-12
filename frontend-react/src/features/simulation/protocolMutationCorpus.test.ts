import { solidTerrain } from './fixtures/terrainFrames';
import { afterEach, describe, expect, it, vi } from 'vitest';

import {
  configurationResponseExample,
  errorResponseExample,
} from './fixtures/protocolV1Examples';
import {
  legacyNpcCatalogue,
  type MutableSnapshotDocument,
  type MutableWelcomeDocument,
  firstEntity,
  playerEntity,
  snapshotDocument,
  welcomeDocument,
} from './fixtures/sessionFrames';
import { CHARGE_COOLDOWN } from './fixtures/chargeFrames';
import { SHIELD_WINDOWS } from './fixtures/shieldFrames';
import {
  type SessionSequenceState,
  validateSessionCommand,
  validateSessionSnapshotMessage,
  validateSessionWelcomeMessage,
} from './sessionProtocolValidation';
import {
  validateSimulationConfigurationResponse,
  validateSimulationHttpErrorResponse,
} from './simulationProtocolValidation';
import type { SessionCommand } from './simulationProtocolTypes';

interface MutationCase<T> {
  readonly name: string;
  readonly mutate: (document: T) => void;
}

function requireBody(document: MutableSnapshotDocument) {
  const body = playerEntity(document).components.physics_body;
  if (body === undefined) {
    throw new Error('TEST.SNAPSHOT_FIXTURE_BODY_MISSING');
  }
  return body;
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

const welcomeMutations: readonly MutationCase<MutableWelcomeDocument>[] = [
  {
    name: 'profile catalogue kind also declared plain',
    mutate: (document) => {
      Reflect.set(document.data, 'npc_profiles', [
        { npc_kind: 'wanderer', profile_name: 'steady' },
      ]);
    },
  },
  {
    name: 'null profile catalogue',
    mutate: (document) => {
      Reflect.set(document.data, 'npc_profiles', null);
    },
  },
  {
    name: 'missing controller identifier',
    mutate: (document) => {
      Reflect.deleteProperty(document.data, 'controller_id');
    },
  },
  {
    name: 'welcome renumbered off message one',
    mutate: (document) => {
      document.meta.message_sequence = 2;
    },
  },
  {
    name: 'display name carrying a control byte',
    mutate: (document) => {
      document.data.display_name = 'Cole\u0007Shaffer';
    },
  },
  {
    name: 'server-issued command kind advertised',
    mutate: (document) => {
      Reflect.set(document.data, 'accepted_command_kinds', ['spawn']);
    },
  },
  {
    name: 'missing NPC controller kinds',
    mutate: (document) => {
      Reflect.deleteProperty(document.data, 'npc_controller_kinds');
    },
  },
  {
    name: 'NPC controller kind outside the published kind-name grammar',
    mutate: (document) => {
      // The array's *members* are not a schema enum -- they are read from the server's registry so a
      // new bot costs no client change -- but the grammar still is, and a name this client could
      // never render as a seat label must not reach the seat menu.
      Reflect.set(document.data, 'npc_controller_kinds', ['Wanderer']);
    },
  },
  {
    name: 'lobby id outside the directory bound',
    mutate: (document) => {
      // Rooms are numbered 1..8; zero names no room and nine names one no directory could list.
      Reflect.set(document.data, 'lobby_id', 0);
    },
  },
  {
    name: 'seat count maximum above the seat bound',
    mutate: (document) => {
      Reflect.set(document.data, 'seat_count_maximum', 65);
    },
  },
];

const snapshotMutations: readonly MutationCase<MutableSnapshotDocument>[] = [
  {
    name: 'missing seat roster',
    mutate: (document) => {
      Reflect.deleteProperty(document.data.match, 'seats');
    },
  },
  {
    name: 'missing start request',
    mutate: (document) => {
      Reflect.deleteProperty(document.data.match, 'start_requested');
    },
  },
  {
    name: 'empty seat carrying an occupant',
    mutate: (document) => {
      // The paired-member rule the seat schema states: `kind` and the two nullable members agree, or
      // the frame is a lie about who is in the lobby.
      Reflect.set(document.data.match, 'seats', [
        { kind: 'empty', controller_id: 3, npc_kind: null },
      ]);
    },
  },
  {
    name: 'NPC seat with no kind',
    mutate: (document) => {
      Reflect.set(document.data.match, 'seats', [
        { kind: 'npc', controller_id: null, npc_kind: null },
      ]);
    },
  },
  {
    name: 'unregistered seat kind',
    mutate: (document) => {
      Reflect.set(document.data.match, 'seats', [
        { kind: 'spectator', controller_id: null, npc_kind: null },
      ]);
    },
  },
  {
    name: 'missing tick sequence',
    mutate: (document) => {
      Reflect.deleteProperty(document.data, 'tick_sequence');
    },
  },
  {
    name: 'entities changed to an object',
    mutate: (document) => {
      Reflect.set(document.data, 'entities', {});
    },
  },
  {
    name: 'unknown physics body field',
    mutate: (document) => {
      Reflect.set(requireBody(document), 'friction', 1);
    },
  },
  {
    name: 'unknown component kind',
    mutate: (document) => {
      Reflect.set(firstEntity(document).components, 'gravity_well', {
        strength: 1,
      });
    },
  },
  {
    name: 'entity carrying no components at all',
    mutate: (document) => {
      Reflect.set(firstEntity(document), 'components', {});
    },
  },
  {
    name: 'fractional entity identifier',
    mutate: (document) => {
      firstEntity(document).entity_id = 1.5;
    },
  },
  {
    name: 'non-finite velocity',
    mutate: (document) => {
      requireBody(document).velocity.x = Number.NaN;
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
      document.meta.sent_at_utc = '2026-09-06 18:04:17';
    },
  },
  {
    name: 'winner identifier that contradicts the outcome kind',
    mutate: (document) => {
      Reflect.set(document.data.match.outcome, 'winner_entity_id', 7);
    },
  },
  {
    name: 'placement rank outside the protocol bound',
    mutate: (document) => {
      const placement = document.data.match.placements[0];
      if (placement === undefined) {
        throw new Error('TEST.SNAPSHOT_FIXTURE_PLACEMENT_MISSING');
      }
      placement.placement = 0;
    },
  },
  {
    name: 'mode state value that does not match its schema id',
    mutate: (document) => {
      document.data.match.mode_state.schema_id =
        'blob-royale://protocol/v3/mode-state/none';
    },
  },
  {
    name: 'shield perfect opening outliving the protection that contains it',
    mutate: (document) => {
      // Each endpoint is a valid tick on its own, so only the semantic pass can catch this: the
      // perfect opening is a sub-interval of the protection, and a reader that accepted the
      // inversion would report a parry window still open after the shield that granted it ended.
      Reflect.set(playerEntity(document).components, 'shield', {
        ...SHIELD_WINDOWS,
        perfect_expiry_tick: SHIELD_WINDOWS.shield_expiry_tick + 1,
      });
    },
  },
  {
    name: 'shield carrying a charge member the component does not publish',
    mutate: (document) => {
      // The published shield is closed at exactly five members. Charge is a separate ability with
      // its own component and its own two endpoints, so a charge field riding on a shield is a
      // server this client does not know, not a field to ignore.
      Reflect.set(playerEntity(document).components, 'shield', {
        ...SHIELD_WINDOWS,
        charge_expiry_tick: 13000,
      });
    },
  },
  {
    name: 'charge cooldown that expires on the tick it was activated',
    mutate: (document) => {
      // Both endpoints are valid ticks on their own, so only the semantic pass can catch this, and
      // only for charge: shield accepts an expiry equal to its activation because a cancelled
      // protection window is a real published state. A charge cooldown is validated strictly
      // positive at load, because it is the only gate a one-shot has -- a reader that accepted this
      // would report an ability ready to fire again on the tick the same frame says it fired.
      Reflect.set(playerEntity(document).components, 'charge', {
        ...CHARGE_COOLDOWN,
        cooldown_expiry_tick: CHARGE_COOLDOWN.activation_tick,
      });
    },
  },
  {
    name: 'charge carrying the protection window a one-shot never opens',
    mutate: (document) => {
      // The published charge is closed at exactly two members. There is no active window to report:
      // the burst is applied once, on the activation tick, and what outlives it is velocity the
      // physics body already publishes. A shield-shaped endpoint here would invite a reader to draw
      // a protection the server never granted.
      Reflect.set(playerEntity(document).components, 'charge', {
        ...CHARGE_COOLDOWN,
        shield_expiry_tick: 12960,
      });
    },
  },
];

const commandMutations: readonly MutationCase<{
  kind: string;
  payload: Record<string, unknown>;
}>[] = [
  {
    name: 'thrust component above the unit interval',
    mutate: (command) => {
      command.payload.x = 1.000001;
    },
  },
  {
    name: 'non-finite thrust component',
    mutate: (command) => {
      command.payload.y = Number.NaN;
    },
  },
  {
    name: 'entity identifier smuggled into the payload',
    mutate: (command) => {
      command.payload.entity_id = 7;
    },
  },
  {
    name: 'missing thrust component',
    mutate: (command) => {
      Reflect.deleteProperty(command.payload, 'y');
    },
  },
];

const welcomeSequence: SessionSequenceState = Object.freeze({
  messageSequence: 1,
  requestId: '018f47a4-9c21-7f10-8a55-4b7d1e0c33a2',
  tickSequence: null,
  npcCatalogue: legacyNpcCatalogue,
  terrain: solidTerrain,
});

afterEach(() => {
  vi.restoreAllMocks();
});

describe('protocol v1 configuration deterministic mutation corpus', () => {
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
});

describe('protocol v3 session deterministic mutation corpus', () => {
  it.each(welcomeMutations)('rejects welcome mutation: $name', ({ mutate }) => {
    vi.spyOn(console, 'warn').mockImplementation(() => undefined);
    const document = welcomeDocument();
    mutate(document);

    expect(() => validateSessionWelcomeMessage(document, null)).toThrow();
  });

  it.each(snapshotMutations)(
    'rejects snapshot mutation: $name',
    ({ mutate }) => {
      vi.spyOn(console, 'warn').mockImplementation(() => undefined);
      const document = snapshotDocument();
      mutate(document);

      expect(() =>
        validateSessionSnapshotMessage(document, welcomeSequence),
      ).toThrow();
    },
  );

  it.each(commandMutations)('rejects command mutation: $name', ({ mutate }) => {
    const command = { kind: 'set_thrust', payload: { x: 0.5, y: -0.5 } };
    mutate(command);

    expect(() => validateSessionCommand(command as SessionCommand)).toThrow();
  });
});
