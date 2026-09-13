import { chargeComponentExample } from './protocolV3Examples';
import { playerEntity, snapshotDocument } from './sessionFrames';

/** Authored initial charge windows at 400 Hz, spanning the golden snapshot tick. */
export const CHARGE_ACTIVATION_TICK = 12800;
export const CHARGE_SNAPSHOT_TICK = 12904;
export const CHARGE_COOLDOWN_EXPIRY_TICK = 13280;
export const CHARGE_ACTIVE_EXPIRY_TICK = 13000;
export const CHARGE_HIT_STUN_DURATION_TICKS = 240;

/** Published member order follows the component encoder, with independently timed active contact. */
export const CHARGE_COOLDOWN = Object.freeze({
  activation_tick: CHARGE_ACTIVATION_TICK,
  cooldown_expiry_tick: CHARGE_COOLDOWN_EXPIRY_TICK,
  active_expiry_tick: CHARGE_ACTIVE_EXPIRY_TICK,
  hit_stun_duration_ticks: CHARGE_HIT_STUN_DURATION_TICKS,
});

/** Unknown specimens reach validation verbatim; undefined means no published charge. */
export function chargeSnapshotDocument(
  charge?: unknown,
  tickSequence = CHARGE_SNAPSHOT_TICK,
) {
  const document = snapshotDocument();
  document.data.tick_sequence = tickSequence;
  if (charge !== undefined)
    Reflect.set(playerEntity(document).components, 'charge', charge);
  return document;
}

/** Cooldown remains strictly positive; active time may end early, be empty, or outlive cooldown. */
export const ACCEPTED_CHARGE_COMPONENTS = Object.freeze([
  {
    name: 'a live cooldown and active contact attempt',
    value: CHARGE_COOLDOWN,
  },
  {
    name: 'activation on the snapshot tick itself',
    value: {
      ...CHARGE_COOLDOWN,
      activation_tick: CHARGE_SNAPSHOT_TICK,
      cooldown_expiry_tick: 13384,
    },
  },
  {
    name: 'a one-tick cooldown with longer active time',
    value: {
      ...CHARGE_COOLDOWN,
      activation_tick: CHARGE_SNAPSHOT_TICK,
      cooldown_expiry_tick: CHARGE_SNAPSHOT_TICK + 1,
    },
  },
  {
    name: 'a cooldown expiring on the tick after this snapshot',
    value: {
      ...CHARGE_COOLDOWN,
      activation_tick: 12425,
      cooldown_expiry_tick: CHARGE_SNAPSHOT_TICK + 1,
    },
  },
  { name: 'the published golden example', value: chargeComponentExample },
  {
    name: 'a cooldown endpoint at the maximum exact integer',
    value: {
      ...CHARGE_COOLDOWN,
      cooldown_expiry_tick: Number.MAX_SAFE_INTEGER,
    },
  },
  {
    name: 'active time retained after natural cooldown expiry',
    value: { ...CHARGE_COOLDOWN, cooldown_expiry_tick: CHARGE_SNAPSHOT_TICK },
  },
  {
    name: 'a canceled active window preserving captured stun and cooldown',
    value: { ...CHARGE_COOLDOWN, active_expiry_tick: CHARGE_ACTIVATION_TICK },
  },
  {
    name: 'an explicit cooldown-only component with no contact stun',
    value: {
      ...CHARGE_COOLDOWN,
      active_expiry_tick: CHARGE_ACTIVATION_TICK,
      hit_stun_duration_ticks: 0,
    },
  },
  {
    name: 'a contact attempt canceled exactly at the covering tick',
    value: { ...CHARGE_COOLDOWN, active_expiry_tick: CHARGE_SNAPSHOT_TICK },
  },
]);

/** Each invalid specimen varies only the condition named, leaving all other required facts valid. */
export const INVALID_CHARGE_COMPONENTS: readonly {
  readonly name: string;
  readonly value: unknown;
}[] = [
  ...Object.keys(CHARGE_COOLDOWN).map((member) => {
    const value: Record<string, number> = { ...CHARGE_COOLDOWN };
    Reflect.deleteProperty(value, member);
    return { name: `missing ${member}`, value };
  }),
  {
    name: 'a cooldown that expires on the tick it was activated',
    value: { ...CHARGE_COOLDOWN, cooldown_expiry_tick: CHARGE_ACTIVATION_TICK },
  },
  {
    name: 'a cooldown expiring before its activation',
    value: { ...CHARGE_COOLDOWN, cooldown_expiry_tick: 12799 },
  },
  {
    name: 'activation after the covering snapshot tick',
    value: {
      ...CHARGE_COOLDOWN,
      activation_tick: CHARGE_SNAPSHOT_TICK + 1,
      cooldown_expiry_tick: 13385,
    },
  },
  {
    name: 'active expiry before activation',
    value: {
      ...CHARGE_COOLDOWN,
      active_expiry_tick: CHARGE_ACTIVATION_TICK - 1,
    },
  },
  {
    name: 'a nonempty active window without captured stun',
    value: { ...CHARGE_COOLDOWN, hit_stun_duration_ticks: 0 },
  },
  {
    name: 'zero activation',
    value: { ...CHARGE_COOLDOWN, activation_tick: 0 },
  },
  {
    name: 'zero cooldown expiry',
    value: { ...CHARGE_COOLDOWN, cooldown_expiry_tick: 0 },
  },
  {
    name: 'fractional activation',
    value: { ...CHARGE_COOLDOWN, activation_tick: 12800.5 },
  },
  {
    name: 'fractional active expiry',
    value: { ...CHARGE_COOLDOWN, active_expiry_tick: 13000.5 },
  },
  {
    name: 'negative captured stun',
    value: { ...CHARGE_COOLDOWN, hit_stun_duration_ticks: -1 },
  },
  {
    name: 'unsafe cooldown expiry',
    value: {
      ...CHARGE_COOLDOWN,
      cooldown_expiry_tick: Number.MAX_SAFE_INTEGER + 1,
    },
  },
  {
    name: 'unsafe active expiry',
    value: {
      ...CHARGE_COOLDOWN,
      active_expiry_tick: Number.MAX_SAFE_INTEGER + 1,
    },
  },
  {
    name: 'unsafe captured stun',
    value: {
      ...CHARGE_COOLDOWN,
      hit_stun_duration_ticks: Number.MAX_SAFE_INTEGER + 1,
    },
  },
  {
    name: 'string activation',
    value: { ...CHARGE_COOLDOWN, activation_tick: '12800' },
  },
  { name: 'null component', value: null },
  { name: 'array component', value: [CHARGE_ACTIVATION_TICK] },
  {
    name: 'a shield protection endpoint charge does not publish',
    value: { ...CHARGE_COOLDOWN, perfect_expiry_tick: 12832 },
  },
  {
    name: 'an unknown charge expiry alias',
    value: { ...CHARGE_COOLDOWN, charge_expiry_tick: 12810 },
  },
  {
    name: 'the private cooldown duration the server converts at load',
    value: { ...CHARGE_COOLDOWN, cooldown_duration_ticks: 480 },
  },
  {
    name: 'an unregistered speed fraction alias',
    value: { ...CHARGE_COOLDOWN, speed_fraction: 0.75 },
  },
];
