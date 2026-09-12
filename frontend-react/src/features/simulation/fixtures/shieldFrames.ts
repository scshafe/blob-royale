import { shieldComponentExample } from './protocolV3Examples';
import { playerEntity, snapshotDocument } from './sessionFrames';

/**
 * The published shield of the `[abilities]` tuning ADR 0008 authors, read at the golden snapshot's
 * own tick: 160 shield ticks, a 32-tick perfect opening inside them, a 360-tick cooldown that
 * starts on activation, and the 240-tick parry stun the defender captured when it activated.
 *
 * Every constant is an absolute tick rather than a remaining duration, because absolute ticks are
 * what the wire publishes and the client owns no clock it could subtract a duration from. This
 * module is its own file rather than a growth of `stunInputFrames.ts` for the reason
 * `contactEffectAdmissionFrames.ts` and `hillMotionFrames.ts` are: one component kind's boundary
 * corpus is read as a whole, and a shared module makes two corpora look like one.
 */
export const SHIELD_ACTIVATION_TICK = 12800;
export const SHIELD_SNAPSHOT_TICK = 12904;
export const SHIELD_COOLDOWN_EXPIRY_TICK = 13160;
export const SHIELD_PARRY_STUN_DURATION_TICKS = 240;

/**
 * Protection still live at the snapshot tick with its perfect opening already closed, written in
 * the encoder's published member order so a key-order assertion reads as the wire does.
 */
export const SHIELD_WINDOWS = Object.freeze({
  activation_tick: SHIELD_ACTIVATION_TICK,
  shield_expiry_tick: 12960,
  perfect_expiry_tick: 12832,
  cooldown_expiry_tick: SHIELD_COOLDOWN_EXPIRY_TICK,
  parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
});

/**
 * A shield the status system cancelled at the activation tick itself. Zero-length protection is a
 * committed activation whose protection never covered a tick, not a malformed frame: the cooldown
 * it already started is still live and is the only thing refusing the next pulse, so a client that
 * rejected this frame would close the connection over the exact state a stunned player is in.
 */
export const CANCELLED_SHIELD_WINDOWS = Object.freeze({
  activation_tick: SHIELD_ACTIVATION_TICK,
  shield_expiry_tick: SHIELD_ACTIVATION_TICK,
  perfect_expiry_tick: SHIELD_ACTIVATION_TICK,
  cooldown_expiry_tick: SHIELD_COOLDOWN_EXPIRY_TICK,
  parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
});

/**
 * Mutable boundary specimen: `undefined` publishes no shield at all, and every other value is
 * written verbatim so a shape the static types forbid still reaches the validator.
 */
export function shieldSnapshotDocument(
  shield?: unknown,
  tickSequence = SHIELD_SNAPSHOT_TICK,
) {
  const document = snapshotDocument();
  document.data.tick_sequence = tickSequence;
  if (shield !== undefined) {
    Reflect.set(playerEntity(document).components, 'shield', shield);
  }
  return document;
}

/**
 * Frames the server is required to be able to send. Cancellation cases keep their activation, their
 * cooldown and their captured stun duration, and shorten only a protection window that had not
 * already closed -- an opening still open when the stun landed closes with the protection, which is
 * why no case here leaves a perfect expiry beyond its shield expiry.
 */
export const ACCEPTED_SHIELD_COMPONENTS = Object.freeze([
  {
    name: 'live protection past its perfect opening',
    value: SHIELD_WINDOWS,
  },
  {
    name: 'live protection still inside its perfect opening',
    value: {
      activation_tick: 12900,
      shield_expiry_tick: 13060,
      perfect_expiry_tick: 12932,
      cooldown_expiry_tick: 13260,
      parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
    },
  },
  {
    name: 'activation on the snapshot tick itself',
    value: {
      activation_tick: SHIELD_SNAPSHOT_TICK,
      shield_expiry_tick: 13064,
      perfect_expiry_tick: 12936,
      cooldown_expiry_tick: 13264,
      parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
    },
  },
  {
    name: 'protection cancelled to zero length with a live cooldown',
    value: CANCELLED_SHIELD_WINDOWS,
  },
  {
    name: 'protection cancelled inside its perfect opening',
    value: {
      activation_tick: SHIELD_ACTIVATION_TICK,
      shield_expiry_tick: 12810,
      perfect_expiry_tick: 12810,
      cooldown_expiry_tick: SHIELD_COOLDOWN_EXPIRY_TICK,
      parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
    },
  },
  {
    name: 'protection cancelled after its perfect opening elapsed',
    value: {
      activation_tick: SHIELD_ACTIVATION_TICK,
      shield_expiry_tick: 12850,
      perfect_expiry_tick: 12832,
      cooldown_expiry_tick: SHIELD_COOLDOWN_EXPIRY_TICK,
      parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
    },
  },
  {
    name: 'expired protection the live cooldown keeps published',
    value: {
      activation_tick: 12700,
      shield_expiry_tick: 12860,
      perfect_expiry_tick: 12732,
      cooldown_expiry_tick: 13060,
      parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
    },
  },
  {
    // A zero cooldown is authored tuning the configuration explicitly permits, so the cooldown
    // endpoint equals the activation and the endpoint comparison must be `<=`, never `<`.
    name: 'cooldown that expires on the activation tick',
    value: {
      activation_tick: SHIELD_ACTIVATION_TICK,
      shield_expiry_tick: 12960,
      perfect_expiry_tick: 12832,
      cooldown_expiry_tick: SHIELD_ACTIVATION_TICK,
      parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
    },
  },
  {
    // The example the protocol publishes, validated here by the semantic pass the JSON Schema
    // conformance script cannot run: a golden that the closed schema accepts but the ordering rule
    // would reject is a golden that teaches a wrong shape.
    name: 'the published golden example',
    value: shieldComponentExample,
  },
  {
    name: 'endpoints at the maximum exact integer',
    value: {
      activation_tick: SHIELD_ACTIVATION_TICK,
      shield_expiry_tick: Number.MAX_SAFE_INTEGER,
      perfect_expiry_tick: Number.MAX_SAFE_INTEGER,
      cooldown_expiry_tick: Number.MAX_SAFE_INTEGER,
      parry_stun_duration_ticks: Number.MAX_SAFE_INTEGER,
    },
  },
]);

/**
 * Frames no server may send. Five of them delete one required member each, because a shield missing
 * one endpoint is a window a reader would silently complete with a guess; the rest violate an
 * ordering JSON Schema cannot state, exceed the exact-integer domain, or smuggle in a field the
 * closed component does not publish -- including the charge members Step 19 owns and the private
 * durations the server converts once at load and never puts on the wire.
 */
export const INVALID_SHIELD_COMPONENTS: readonly {
  readonly name: string;
  readonly value: unknown;
}[] = [
  {
    name: 'missing activation',
    value: {
      shield_expiry_tick: 12960,
      perfect_expiry_tick: 12832,
      cooldown_expiry_tick: SHIELD_COOLDOWN_EXPIRY_TICK,
      parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
    },
  },
  {
    name: 'missing shield expiry',
    value: {
      activation_tick: SHIELD_ACTIVATION_TICK,
      perfect_expiry_tick: 12832,
      cooldown_expiry_tick: SHIELD_COOLDOWN_EXPIRY_TICK,
      parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
    },
  },
  {
    name: 'missing perfect expiry',
    value: {
      activation_tick: SHIELD_ACTIVATION_TICK,
      shield_expiry_tick: 12960,
      cooldown_expiry_tick: SHIELD_COOLDOWN_EXPIRY_TICK,
      parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
    },
  },
  {
    name: 'missing cooldown expiry',
    value: {
      activation_tick: SHIELD_ACTIVATION_TICK,
      shield_expiry_tick: 12960,
      perfect_expiry_tick: 12832,
      parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
    },
  },
  {
    name: 'missing parry stun duration',
    value: {
      activation_tick: SHIELD_ACTIVATION_TICK,
      shield_expiry_tick: 12960,
      perfect_expiry_tick: 12832,
      cooldown_expiry_tick: SHIELD_COOLDOWN_EXPIRY_TICK,
    },
  },
  {
    name: 'perfect opening outliving its protection',
    value: { ...SHIELD_WINDOWS, perfect_expiry_tick: 12961 },
  },
  {
    name: 'perfect opening closing before its activation',
    value: { ...SHIELD_WINDOWS, perfect_expiry_tick: 12799 },
  },
  {
    name: 'cooldown expiring before its activation',
    value: { ...SHIELD_WINDOWS, cooldown_expiry_tick: 12799 },
  },
  {
    name: 'activation after the covering snapshot tick',
    value: {
      activation_tick: SHIELD_SNAPSHOT_TICK + 1,
      shield_expiry_tick: 13065,
      perfect_expiry_tick: 12937,
      cooldown_expiry_tick: 13265,
      parry_stun_duration_ticks: SHIELD_PARRY_STUN_DURATION_TICKS,
    },
  },
  {
    name: 'zero parry stun duration',
    value: { ...SHIELD_WINDOWS, parry_stun_duration_ticks: 0 },
  },
  {
    name: 'negative parry stun duration',
    value: { ...SHIELD_WINDOWS, parry_stun_duration_ticks: -1 },
  },
  {
    name: 'zero activation',
    value: { ...SHIELD_WINDOWS, activation_tick: 0 },
  },
  {
    name: 'fractional activation',
    value: { ...SHIELD_WINDOWS, activation_tick: 12800.5 },
  },
  {
    name: 'unsafe shield expiry',
    value: {
      ...SHIELD_WINDOWS,
      shield_expiry_tick: Number.MAX_SAFE_INTEGER + 1,
    },
  },
  {
    name: 'string activation',
    value: { ...SHIELD_WINDOWS, activation_tick: '12800' },
  },
  {
    name: 'null component',
    value: null,
  },
  {
    name: 'array component',
    value: [SHIELD_ACTIVATION_TICK],
  },
  {
    name: 'charge member Step 19 owns',
    value: { ...SHIELD_WINDOWS, charge_expiry_tick: 13000 },
  },
  {
    name: 'private shield duration',
    value: { ...SHIELD_WINDOWS, shield_duration_ticks: 160 },
  },
];
