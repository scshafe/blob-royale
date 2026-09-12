import { chargeComponentExample } from './protocolV3Examples';
import { playerEntity, snapshotDocument } from './sessionFrames';

/**
 * The published charge of the `[abilities]` tuning ADR 0008 authors, read at the golden snapshot's
 * own tick: one activation and the 480-tick cooldown that `charge_cooldown_seconds=1.2` converts to
 * at the 400 ticks per second `config/blob-royale.cfg` authors.
 *
 * There are exactly two endpoints because charge is one-shot. The burst is applied on the activation
 * tick and never again, so there is no active window to publish; what outlives the activation is
 * velocity `physics_body` already carries. The activation is published alongside the expiry rather
 * than a bare countdown for two reasons: an absolute tick stays true in a frame a client buffered or
 * received late, and a cooldown arc needs a denominator that `charge_cooldown_seconds` -- server-side
 * on purpose -- cannot supply.
 *
 * This module is its own file rather than a growth of `shieldFrames.ts`, for the reason that module
 * gives for not growing `stunInputFrames.ts`: one component kind's boundary corpus is read as a
 * whole, and a shared module makes two corpora look like one. Here that would be actively
 * misleading, because charge's endpoint ordering is *strict* and shield's is not -- the two corpora
 * disagree about their central case and must not be read as one list.
 */
export const CHARGE_ACTIVATION_TICK = 12800;
export const CHARGE_SNAPSHOT_TICK = 12904;
export const CHARGE_COOLDOWN_EXPIRY_TICK = 13280;

/**
 * A cooldown still live at the snapshot tick, written in the encoder's published member order so a
 * key-order assertion reads as the wire does.
 */
export const CHARGE_COOLDOWN = Object.freeze({
  activation_tick: CHARGE_ACTIVATION_TICK,
  cooldown_expiry_tick: CHARGE_COOLDOWN_EXPIRY_TICK,
});

/**
 * Mutable boundary specimen: `undefined` publishes no charge at all, and every other value is
 * written verbatim so a shape the static types forbid still reaches the validator.
 */
export function chargeSnapshotDocument(
  charge?: unknown,
  tickSequence = CHARGE_SNAPSHOT_TICK,
) {
  const document = snapshotDocument();
  document.data.tick_sequence = tickSequence;
  if (charge !== undefined) {
    Reflect.set(playerEntity(document).components, 'charge', charge);
  }
  return document;
}

/**
 * Frames the server is required to be able to send. Every one separates an activation from a
 * strictly later cooldown expiry, because the ability configuration refuses an authored cooldown
 * that rounds to zero ticks: there is no cancellation path here that could shorten the window the
 * way a stun shortens shield protection, so no accepted case collapses the interval.
 */
export const ACCEPTED_CHARGE_COMPONENTS = Object.freeze([
  {
    name: 'a live cooldown from a burst already spent',
    value: CHARGE_COOLDOWN,
  },
  {
    name: 'activation on the snapshot tick itself',
    value: {
      activation_tick: CHARGE_SNAPSHOT_TICK,
      cooldown_expiry_tick: 13384,
    },
  },
  {
    // The tightest interval strict ordering admits, and the reason the comparison cannot be `>=` on
    // the wire either: a room may tune `charge_cooldown_seconds` down to a single tick and still
    // pass `require_positive_ticks`, so one tick is authored tuning rather than a malformed frame.
    name: 'a one-tick cooldown a room may legitimately tune',
    value: {
      activation_tick: CHARGE_SNAPSHOT_TICK,
      cooldown_expiry_tick: CHARGE_SNAPSHOT_TICK + 1,
    },
  },
  {
    // The last frame that publishes this component: the ability system erases a charge once its
    // cooldown has elapsed, so an expiry beyond the snapshot tick is what keeps it on the wire.
    name: 'a cooldown expiring on the tick after this snapshot',
    value: {
      activation_tick: 12425,
      cooldown_expiry_tick: CHARGE_SNAPSHOT_TICK + 1,
    },
  },
  {
    // The example the protocol publishes, validated here by the semantic pass the JSON Schema
    // conformance script cannot run: a golden that the closed schema accepts but the ordering rule
    // would reject is a golden that teaches a wrong shape.
    name: 'the published golden example',
    value: chargeComponentExample,
  },
  {
    name: 'a cooldown endpoint at the maximum exact integer',
    value: {
      activation_tick: CHARGE_ACTIVATION_TICK,
      cooldown_expiry_tick: Number.MAX_SAFE_INTEGER,
    },
  },
]);

/**
 * Frames no server may send. Two of them delete one required member each, because a charge missing
 * an endpoint is an interval a reader would silently complete with a guess. The rest violate the
 * strict ordering JSON Schema cannot state, leave the exact-integer domain, or smuggle in a member
 * the closed component does not publish -- shield's protection endpoints, an "active" window a
 * one-shot never opens, and the authored tuning the server converts once at load and keeps.
 */
export const INVALID_CHARGE_COMPONENTS: readonly {
  readonly name: string;
  readonly value: unknown;
}[] = [
  {
    name: 'missing activation',
    value: { cooldown_expiry_tick: CHARGE_COOLDOWN_EXPIRY_TICK },
  },
  {
    name: 'missing cooldown expiry',
    value: { activation_tick: CHARGE_ACTIVATION_TICK },
  },
  {
    // The single case that separates this corpus from shield's, where an endpoint equal to the
    // activation is accepted. A charge cooldown is validated strictly positive precisely because
    // there is no second gate behind it, so an interval of zero length describes an ability that
    // fires four hundred times a second -- a frame the server cannot build and this client must not
    // teach a reader to believe.
    name: 'a cooldown that expires on the tick it was activated',
    value: {
      activation_tick: CHARGE_ACTIVATION_TICK,
      cooldown_expiry_tick: CHARGE_ACTIVATION_TICK,
    },
  },
  {
    name: 'a cooldown expiring before its activation',
    value: { ...CHARGE_COOLDOWN, cooldown_expiry_tick: 12799 },
  },
  {
    name: 'activation after the covering snapshot tick',
    value: {
      activation_tick: CHARGE_SNAPSHOT_TICK + 1,
      cooldown_expiry_tick: 13385,
    },
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
    name: 'unsafe cooldown expiry',
    value: {
      ...CHARGE_COOLDOWN,
      cooldown_expiry_tick: Number.MAX_SAFE_INTEGER + 1,
    },
  },
  {
    name: 'string activation',
    value: { ...CHARGE_COOLDOWN, activation_tick: '12800' },
  },
  {
    name: 'null component',
    value: null,
  },
  {
    name: 'array component',
    value: [CHARGE_ACTIVATION_TICK],
  },
  {
    name: 'a shield protection endpoint charge does not publish',
    value: { ...CHARGE_COOLDOWN, perfect_expiry_tick: 12832 },
  },
  {
    name: 'an active window a one-shot never opens',
    value: { ...CHARGE_COOLDOWN, charge_expiry_tick: 12810 },
  },
  {
    name: 'the private cooldown duration the server converts at load',
    value: { ...CHARGE_COOLDOWN, cooldown_duration_ticks: 480 },
  },
  {
    name: 'the authored speed fraction the configuration owns',
    value: { ...CHARGE_COOLDOWN, speed_fraction: 0.75 },
  },
];
