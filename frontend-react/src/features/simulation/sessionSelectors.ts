import type {
  SessionEntitySnapshot,
  SessionMatchSection,
  SessionPlacement,
} from './simulationProtocolTypes';

/**
 * Resolves the session's own body for one frame. `welcome.entity_id` is the session's first body
 * only: elimination and the lobby wipe destroy it and the server seats the same controller on a new
 * entity, so the own body is resolved by controller id every frame. "No such entity this frame" is
 * the ordinary state of a player who is eliminated, waiting, or deferred by the spawn policy.
 */
export function findOwnEntityId(
  entities: readonly SessionEntitySnapshot[],
  controllerId: number | null,
): number | null {
  if (controllerId === null) {
    return null;
  }
  for (const entity of entities) {
    if (entity.components.controllable?.controller_id === controllerId) {
      return entity.entity_id;
    }
  }
  return null;
}

/**
 * Alive count is not a wire field. It is the number of published entities carrying both
 * `physics_body` and `controllable`, counted from the array the client already renders, because
 * publishing it twice would give a client two sources for one number and a way to disagree.
 */
export function countAlivePlayers(
  entities: readonly SessionEntitySnapshot[],
): number {
  let aliveCount = 0;
  for (const entity of entities) {
    if (
      entity.components.physics_body !== undefined &&
      entity.components.controllable !== undefined
    ) {
      aliveCount += 1;
    }
  }
  return aliveCount;
}

/** Finds this session's finished rank. Placements carry the controller id for exactly this read. */
export function findPlacementForController(
  match: SessionMatchSection | null,
  controllerId: number | null,
): SessionPlacement | null {
  if (match === null || controllerId === null) {
    return null;
  }
  for (const placement of match.placements) {
    if (placement.controller_id === controllerId) {
      return placement;
    }
  }
  return null;
}

export function findEntityById(
  entities: readonly SessionEntitySnapshot[],
  entityId: number | null,
): SessionEntitySnapshot | null {
  if (entityId === null) {
    return null;
  }
  for (const entity of entities) {
    if (entity.entity_id === entityId) {
      return entity;
    }
  }
  return null;
}

/**
 * Seconds the current phase has been running. `phase_started_tick` is the one tick-valued member
 * that admits zero, which truthfully means "this phase has always been the phase"; an elapsed time
 * is rendered from it only once it is nonzero.
 */
export function phaseElapsedSeconds(
  match: SessionMatchSection | null,
  tickSequence: number | null,
  ticksPerSecond: number,
): number | null {
  if (
    match === null ||
    tickSequence === null ||
    match.phase_started_tick === 0 ||
    ticksPerSecond <= 0
  ) {
    return null;
  }
  const elapsedTicks = tickSequence - match.phase_started_tick;
  if (elapsedTicks < 0) {
    return null;
  }
  return elapsedTicks / ticksPerSecond;
}

/**
 * The one mode-state schema id whose block carries an elimination grace. Typed as the generated
 * union rather than a bare string, so renaming the id in `common.schema.json` and regenerating
 * fails this build instead of silently making every royale frame read as an unknown mode.
 */
const ROYALE_MODE_STATE_SCHEMA_ID: SessionMatchSection['mode_state']['schema_id'] =
  'blob-royale://protocol/v2/mode-state/royale';

/**
 * `G`, the consecutive outside ticks the running mode allows before it eliminates, or `null` when
 * this frame does not say.
 *
 * Published in the royale mode-state block since protocol 2.2 (`royale-mode-state.schema.json`) for
 * exactly one reason: every snapshot already carries `zone_exposure.outside_ticks`, and a
 * consecutive-tick counter without its bound cannot answer "how long do I have". The schema makes
 * it `required`, so a frame that validated against a royale block has it; `null` is therefore not a
 * degraded royale frame but a frame from a mode that has no grace at all — `sandbox` publishes the
 * `none` block — and the caller's job in that case is to show what it knows rather than to guess a
 * duration.
 *
 * The value is read defensively even though validation guarantees its shape, because the generated
 * type for `mode_state.value` is the open `{}`: json-schema-to-typescript cannot express the
 * if/then correlation between the schema id and the value, so the correlation is resolved here, in
 * one place, instead of at every call site.
 */
export function eliminationGraceTicks(
  match: SessionMatchSection | null,
): number | null {
  if (
    match === null ||
    match.mode_state.schema_id !== ROYALE_MODE_STATE_SCHEMA_ID
  ) {
    return null;
  }
  const value: unknown = match.mode_state.value;
  if (
    typeof value !== 'object' ||
    value === null ||
    !('elimination_grace_ticks' in value)
  ) {
    return null;
  }
  const graceTicks: unknown = value.elimination_grace_ticks;
  if (
    typeof graceTicks !== 'number' ||
    !Number.isSafeInteger(graceTicks) ||
    graceTicks < 0
  ) {
    return null;
  }
  return graceTicks;
}

/**
 * How much of the grace an exposure has spent, in `[0, 1]`, or `null` when this frame published no
 * grace to spend.
 *
 * Three cases and each is a decision rather than an accident. **No published grace** is `null`: the
 * caller must fall back to reporting elapsed exposure rather than ramping against a denominator it
 * invented. **A grace of zero** is `1`, not a division: zero is a legal `[royale]` value meaning
 * "eliminate on the first outside tick", so a blob that is outside at all has spent every bit of a
 * window that never existed. **An overshoot** clamps: `zone_elimination` eliminates when
 * `outside_ticks` reaches `G` and `placement_recorder` destroys the entity in the same tick, so a
 * published counter should never reach its bound — but a client must not render a fraction above
 * one, or a negative remainder, on a frame that says otherwise.
 */
export function graceSpentFraction(
  outsideTicks: number,
  eliminationGraceTicks: number | null,
): number | null {
  if (eliminationGraceTicks === null) {
    return null;
  }
  if (eliminationGraceTicks <= 0) {
    return 1;
  }
  return Math.min(1, Math.max(0, outsideTicks / eliminationGraceTicks));
}

/** What one entity's zone exposure means this frame, in the units a player is shown. */
export interface ZoneExposureReport {
  /** Seconds the center has been continuously outside. Always greater than zero. */
  readonly elapsedSeconds: number;
  /** Seconds of grace left, or `null` when this frame published no grace. Never negative. */
  readonly remainingSeconds: number | null;
  /** Grace spent, in `[0, 1]`, or `null` when this frame published no grace. */
  readonly spentFraction: number | null;
}

/**
 * One entity's zone exposure, or `null` when it is inside, absent, or unpublished.
 * `zone_exposure.outside_ticks` is a consecutive committed-tick count that `zone_elimination` resets
 * to zero on re-entry (ADR 0005 § "Elimination and placement"), so `null` is exactly "there is
 * nothing to warn about this frame" and the caller needs no timer.
 *
 * It reports elapsed *and* remaining because the two have different availability. Elapsed comes from
 * a counter every royale frame carries and is always known; remaining needs `G`, which arrives in
 * the mode-state block and is absent for a mode that has no grace. Returning both, with the
 * remainder nullable, is what lets one caller show the better answer when it exists and the honest
 * one when it does not, without a second walk of the entity list.
 *
 * The remainder is computed as `max(0, G - outside_ticks)` in ticks and converted once, so nothing
 * ever divides by `G` and an overshoot reads as "no time left" rather than as a negative countdown.
 */
export function zoneExposureReport(
  entities: readonly SessionEntitySnapshot[],
  entityId: number | null,
  ticksPerSecond: number,
  eliminationGraceTicks: number | null,
): ZoneExposureReport | null {
  if (ticksPerSecond <= 0) {
    return null;
  }
  const entity = findEntityById(entities, entityId);
  const outsideTicks = entity?.components.zone_exposure?.outside_ticks ?? 0;
  if (outsideTicks <= 0) {
    return null;
  }
  return {
    elapsedSeconds: outsideTicks / ticksPerSecond,
    remainingSeconds:
      eliminationGraceTicks === null
        ? null
        : Math.max(0, eliminationGraceTicks - outsideTicks) / ticksPerSecond,
    spentFraction: graceSpentFraction(outsideTicks, eliminationGraceTicks),
  };
}

export interface MatchOverlayDescription {
  readonly detail: string;
  readonly title: string;
}

export interface MatchOverlayInput {
  readonly entities: readonly SessionEntitySnapshot[];
  readonly match: SessionMatchSection | null;
  readonly ownControllerId: number | null;
  readonly ownEntityId: number | null;
}

function winnerLabel(
  entities: readonly SessionEntitySnapshot[],
  winnerEntityId: number,
): string {
  const winner = findEntityById(entities, winnerEntityId);
  return (
    winner?.components.controllable?.display_name ?? `entity ${winnerEntityId}`
  );
}

function describeEndedMatch({
  entities,
  match,
  ownEntityId,
}: MatchOverlayInput & {
  readonly match: SessionMatchSection;
}): MatchOverlayDescription {
  const { outcome } = match;
  if (outcome.kind === 'drawn') {
    return {
      detail: 'No blob was left standing. The next lobby opens shortly.',
      title: 'Draw',
    };
  }
  if (outcome.kind === 'won_by_entity' && outcome.winner_entity_id !== null) {
    const isOwnWin = outcome.winner_entity_id === ownEntityId;
    return {
      detail: isOwnWin
        ? 'You were the last blob in the zone.'
        : `${winnerLabel(entities, outcome.winner_entity_id)} was the last blob in the zone.`,
      title: isOwnWin ? 'You win' : 'Winner',
    };
  }
  if (outcome.kind === 'won_by_team' && outcome.winner_team_id !== null) {
    return {
      detail: `Team ${outcome.winner_team_id} was the last side standing.`,
      title: 'Winner',
    };
  }
  return {
    detail: 'The next lobby opens shortly.',
    title: 'Match over',
  };
}

/**
 * The one description of what a player should be told about the match right now. Kept out of the
 * component so the rule is testable without a renderer and the component stays presentational.
 */
export function describeMatchOverlay(
  input: MatchOverlayInput,
): MatchOverlayDescription | null {
  const { match, ownControllerId, ownEntityId } = input;
  if (match === null) {
    return null;
  }

  if (match.phase === 'ended') {
    return describeEndedMatch({ ...input, match });
  }
  if (match.phase === 'lobby') {
    return {
      detail:
        'The match starts when every seat is filled and somebody presses Start.',
      title: 'Waiting for players',
    };
  }
  if (match.phase === 'countdown') {
    return {
      detail: 'Get ready: the zone starts shrinking when the match begins.',
      title: 'Match starting',
    };
  }

  if (ownEntityId !== null) {
    return null;
  }
  const placement = findPlacementForController(match, ownControllerId);
  if (placement !== null) {
    return {
      detail: `You placed #${placement.placement}. The next lobby opens when this match ends.`,
      title: 'Eliminated',
    };
  }
  return {
    detail: 'A match is running. You are seated when the next lobby opens.',
    title: 'Waiting for the next match',
  };
}
