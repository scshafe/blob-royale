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
      detail: 'The match starts once enough blobs have joined the arena.',
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
