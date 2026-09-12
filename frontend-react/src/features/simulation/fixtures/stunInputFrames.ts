import { validateSessionSnapshotMessage } from '../sessionProtocolValidation';
import type { SessionSetThrustCommand } from '../simulationProtocolTypes';
import type {
  SimulationCommandSender,
  SimulationConnection,
  SimulationSessionIdentity,
} from '../useSimulationConnection';
import { cursorSteeringConnection } from './cursorSteeringFrames';
import { playerEntity, snapshotDocument } from './sessionFrames';
import { THRUST_RIGHT, THRUST_ZERO } from './thrustInputFrames';

export const STUN_INPUT_GENERATION = 12900;
export const STUN_INPUT_NEXT_GENERATION = 12903;
export const STUN_INPUT_SNAPSHOT_TICK = 12904;
export const STUN_INPUT_WINDOW = Object.freeze({
  activation_tick: STUN_INPUT_NEXT_GENERATION,
  expiry_tick: 12906,
});

/** Mutable boundary specimen: undefined omits the field; null remains an invalid wire value. */
export function stunInputSnapshotDocument(
  generation: unknown,
  stun?: unknown,
  tickSequence = STUN_INPUT_SNAPSHOT_TICK,
) {
  const document = snapshotDocument();
  document.data.tick_sequence = tickSequence;
  const own = playerEntity(document);
  if (own.components.controllable === undefined)
    throw new Error('TEST.STUN_INPUT_CONTROLLABLE_MISSING');
  if (generation !== undefined)
    Reflect.set(own.components.controllable, 'input_generation', generation);
  if (stun !== undefined) Reflect.set(own.components, 'stun', stun);
  return document;
}

/** Validated cursor frame whose only changing input facts are the selected generation/window. */
export function stunInputConnection(
  sendCommand: SimulationCommandSender,
  session: SimulationSessionIdentity,
  generation: number | undefined,
  stun?: typeof STUN_INPUT_WINDOW,
  tickSequence = STUN_INPUT_SNAPSHOT_TICK,
): SimulationConnection {
  const connection = cursorSteeringConnection(sendCommand, session);
  if (connection.snapshot === null)
    throw new Error('TEST.STUN_INPUT_SNAPSHOT_MISSING');
  const document = {
    ...connection.snapshot,
    data: {
      ...connection.snapshot.data,
      tick_sequence: tickSequence,
      entities: connection.entities.map((entity) => {
        if (entity.entity_id !== connection.ownEntityId) return entity;
        const controllable = entity.components.controllable;
        if (controllable === undefined)
          throw new Error('TEST.STUN_INPUT_CONTROLLABLE_MISSING');
        return {
          ...entity,
          components: {
            ...entity.components,
            controllable: {
              ...controllable,
              ...(generation === undefined
                ? {}
                : { input_generation: generation }),
            },
            ...(stun === undefined ? {} : { stun }),
          },
        };
      }),
    },
  };
  const snapshot = validateSessionSnapshotMessage(document, {
    messageSequence: 1,
    requestId: document.meta.request_id,
    tickSequence: null,
    terrain: session.terrain,
  });
  return { ...connection, snapshot, entities: snapshot.data.entities };
}

export const INVALID_INPUT_GENERATIONS = Object.freeze([
  { name: 'zero', value: 0 },
  { name: 'negative', value: -1 },
  { name: 'fractional', value: 1.5 },
  { name: 'unsafe integer', value: Number.MAX_SAFE_INTEGER + 1 },
  { name: 'null', value: null },
  { name: 'string', value: '12900' },
  { name: 'NaN', value: Number.NaN },
  { name: 'infinity', value: Number.POSITIVE_INFINITY },
]);

export const INVALID_STUN_WINDOWS = Object.freeze([
  { name: 'missing activation', value: { expiry_tick: 12906 } },
  { name: 'missing expiry', value: { activation_tick: 12903 } },
  {
    name: 'zero activation',
    value: { activation_tick: 0, expiry_tick: 12906 },
  },
  { name: 'zero expiry', value: { activation_tick: 12903, expiry_tick: 0 } },
  {
    name: 'fractional activation',
    value: { activation_tick: 1.5, expiry_tick: 12906 },
  },
  {
    name: 'fractional expiry',
    value: { activation_tick: 12903, expiry_tick: 12906.5 },
  },
  {
    name: 'unsafe activation',
    value: {
      activation_tick: Number.MAX_SAFE_INTEGER + 1,
      expiry_tick: Number.MAX_SAFE_INTEGER,
    },
  },
  {
    name: 'unsafe expiry',
    value: { activation_tick: 12903, expiry_tick: Number.MAX_SAFE_INTEGER + 1 },
  },
  {
    name: 'empty window',
    value: { activation_tick: 12903, expiry_tick: 12903 },
  },
  {
    name: 'reversed window',
    value: { activation_tick: 12903, expiry_tick: 12902 },
  },
  {
    name: 'future activation',
    value: { activation_tick: 12905, expiry_tick: 12906 },
  },
  { name: 'null', value: null },
  {
    name: 'string activation',
    value: { activation_tick: '12903', expiry_tick: 12906 },
  },
  {
    name: 'private duration',
    value: { ...STUN_INPUT_WINDOW, duration_ticks: 3 },
  },
]);

export const THRUST_GENERATION_COMMAND_CASES: readonly {
  readonly name: string;
  readonly command: SessionSetThrustCommand;
  readonly serialized: string;
}[] = Object.freeze([
  {
    name: 'absent generation thrust',
    command: { kind: 'set_thrust', payload: THRUST_RIGHT },
    serialized: '{"kind":"set_thrust","payload":{"x":1,"y":0}}',
  },
  {
    name: 'absent generation zero release',
    command: { kind: 'set_thrust', payload: THRUST_ZERO },
    serialized: '{"kind":"set_thrust","payload":{"x":0,"y":0}}',
  },
  {
    name: 'present generation thrust',
    command: {
      kind: 'set_thrust',
      payload: { ...THRUST_RIGHT, input_generation: STUN_INPUT_GENERATION },
    },
    serialized:
      '{"kind":"set_thrust","payload":{"x":1,"y":0,"input_generation":12900}}',
  },
  {
    name: 'present generation zero release',
    command: {
      kind: 'set_thrust',
      payload: { ...THRUST_ZERO, input_generation: STUN_INPUT_GENERATION },
    },
    serialized:
      '{"kind":"set_thrust","payload":{"x":0,"y":0,"input_generation":12900}}',
  },
]);
