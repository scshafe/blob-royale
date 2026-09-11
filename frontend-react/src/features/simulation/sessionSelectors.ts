import { SimulationApiError } from './SimulationApiError';
import type {
  SessionEntitySnapshot,
  SessionMatchSection,
  SessionPlacement,
  SessionVector2,
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
  'blob-royale://protocol/v3/mode-state/royale';

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

/** The mode-state schema id of the hill block, typed like the royale id and for the same reason. */
const KING_OF_THE_HILL_MODE_STATE_SCHEMA_ID: SessionMatchSection['mode_state']['schema_id'] =
  'blob-royale://protocol/v3/mode-state/king-of-the-hill';

/**
 * The three declared constants of the `king_of_the_hill` block (`king-of-the-hill-mode-state.schema.json`):
 * the denominators a client cannot compute from the frame. Scores, the hill, and progress toward
 * the next point are entity components and are read from the entity list, never from here.
 */
export interface KingOfTheHillRules {
  /** `I`, the consecutive inside ticks that earn one point. Zero is legal and means the first tick. */
  readonly pointIntervalTicks: number;
  /** The score at which the match is decided. */
  readonly pointsToWin: number;
  /** Running ticks after which the leader wins. */
  readonly timeLimitTicks: number;
}

function isRecord(value: unknown): value is Readonly<Record<string, unknown>> {
  return typeof value === 'object' && value !== null;
}

/** A published count: a safe integer that is not negative, or `null` for anything else. */
function publishedCount(value: unknown): number | null {
  if (typeof value !== 'number' || !Number.isSafeInteger(value) || value < 0) {
    return null;
  }
  return value;
}

/**
 * The hill rules of this frame, or `null` when the frame is not a hill frame. Like
 * `eliminationGraceTicks`, this is the one place the schema id is correlated with the block's
 * shape, and a block missing a member reads as no rules at all rather than as invented ones: the
 * HUD then shows the mode-neutral rows, which is exactly what it shows for royale.
 */
export function kingOfTheHillRules(
  match: SessionMatchSection | null,
): KingOfTheHillRules | null {
  if (
    match === null ||
    match.mode_state.schema_id !== KING_OF_THE_HILL_MODE_STATE_SCHEMA_ID
  ) {
    return null;
  }
  const block: unknown = match.mode_state.value;
  if (!isRecord(block)) {
    return null;
  }
  const pointsToWin = publishedCount(block.points_to_win);
  const pointIntervalTicks = publishedCount(block.point_interval_ticks);
  const timeLimitTicks = publishedCount(block.time_limit_ticks);
  if (
    pointsToWin === null ||
    pointIntervalTicks === null ||
    timeLimitTicks === null
  ) {
    return null;
  }
  return { pointIntervalTicks, pointsToWin, timeLimitTicks };
}

/** One participant's line on the board. */
export interface ScoreboardRow {
  readonly controllerId: number;
  readonly displayName: string;
  readonly entityId: number;
  /** Whether the participant has a body this frame; a respawning player keeps its line without one. */
  readonly isInPlay: boolean;
  readonly points: number;
}

/**
 * Every participant with its points, best first. A participant is an entity carrying `controllable`
 * (ADR 0007's roster rule: being knocked out takes the body and keeps the entity, its name, and its
 * score), so the join is within one entity and an absent `score` reads as zero. Ties keep ascending
 * entity id, which is the server's own order, so two clients render the same board from the same
 * frame.
 */
export function scoreboard(
  entities: readonly SessionEntitySnapshot[],
): readonly ScoreboardRow[] {
  const rows: ScoreboardRow[] = [];
  for (const entity of entities) {
    const controllable = entity.components.controllable;
    if (controllable === undefined) {
      continue;
    }
    rows.push({
      controllerId: controllable.controller_id,
      displayName: controllable.display_name,
      entityId: entity.entity_id,
      isInPlay: entity.components.physics_body !== undefined,
      points: entity.components.score?.points ?? 0,
    });
  }
  return rows.sort(
    (left, right) =>
      right.points - left.points || left.entityId - right.entityId,
  );
}

/**
 * How much of the interval a presence has earned, in `[0, 1]`. The same three decisions as
 * `graceSpentFraction`: a zero interval is a legal `[king_of_the_hill]` value meaning "a point on
 * the first inside tick", so it saturates rather than divides, and a counter past its bound clamps.
 */
export function hillProgressFraction(
  insideTicks: number,
  pointIntervalTicks: number,
): number {
  if (pointIntervalTicks <= 0) {
    return 1;
  }
  return Math.min(1, Math.max(0, insideTicks / pointIntervalTicks));
}

/** What one entity's hill presence means this frame, in the units a player is shown. */
export interface HillPresenceReport {
  /** Seconds the center has held the hill toward the next point. */
  readonly heldSeconds: number;
  /** Seconds of holding left before the next point. Never negative. */
  readonly remainingSeconds: number;
  /** Interval earned, in `[0, 1]`. */
  readonly spentFraction: number;
}

/**
 * One entity's presence on the hill, or `null` when it carries none. `hill_scoring` erases the
 * component the tick a center leaves the hill, the tick a point is scored, and the tick the body is
 * lost, so `null` is exactly "nothing is being counted for this entity" and the caller needs no
 * timer. A contested hill freezes the counter rather than erasing it, so a frozen report is a
 * report that does not advance.
 */
export function hillPresenceReport(
  entities: readonly SessionEntitySnapshot[],
  entityId: number | null,
  ticksPerSecond: number,
  rules: KingOfTheHillRules | null,
): HillPresenceReport | null {
  if (rules === null || ticksPerSecond <= 0) {
    return null;
  }
  const presence = findEntityById(entities, entityId)?.components.hill_presence;
  if (presence === undefined) {
    return null;
  }
  const insideTicks = presence.inside_ticks;
  return {
    heldSeconds: insideTicks / ticksPerSecond,
    remainingSeconds:
      Math.max(0, rules.pointIntervalTicks - insideTicks) / ticksPerSecond,
    spentFraction: hillProgressFraction(insideTicks, rules.pointIntervalTicks),
  };
}

/**
 * Seconds of running time left before the clock decides the match, or `null` when the match is not
 * running, the frame carries no limit, or the phase start is not yet known. The same guards as
 * `phaseElapsedSeconds`, and the remainder is `max(0, limit - elapsed)` in ticks so a frame past
 * the limit reads as no time left rather than as a negative countdown.
 */
export function runningTimeRemainingSeconds(
  match: SessionMatchSection | null,
  tickSequence: number | null,
  ticksPerSecond: number,
  timeLimitTicks: number | null,
): number | null {
  if (
    match === null ||
    timeLimitTicks === null ||
    match.phase !== 'running' ||
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
  return Math.max(0, timeLimitTicks - elapsedTicks) / ticksPerSecond;
}

/**
 * Seconds until a knocked-out entity is offered a seat again, or `null` when it is not waiting for
 * one. `respawn_timer` is carried only by an entity with no body and is erased the tick it reaches
 * zero, so `null` is exactly "in play, or not seated at all".
 */
export function respawnCountdownSeconds(
  entities: readonly SessionEntitySnapshot[],
  entityId: number | null,
  ticksPerSecond: number,
): number | null {
  if (ticksPerSecond <= 0) {
    return null;
  }
  const timer = findEntityById(entities, entityId)?.components.respawn_timer;
  if (timer === undefined) {
    return null;
  }
  return timer.ticks_remaining / ticksPerSecond;
}

/** Everything the hill section of the HUD says, resolved once per frame. */
export interface HillHudReport {
  /** The session's own points, or `null` when it has no entity this frame. */
  readonly ownPoints: number | null;
  readonly presence: HillPresenceReport | null;
  readonly respawnSeconds: number | null;
  readonly rules: KingOfTheHillRules;
  readonly scoreboard: readonly ScoreboardRow[];
  readonly timeRemainingSeconds: number | null;
}

export interface HillHudInput {
  readonly entities: readonly SessionEntitySnapshot[];
  readonly match: SessionMatchSection | null;
  readonly ownEntityId: number | null;
  readonly tickSequence: number | null;
  readonly ticksPerSecond: number;
}

/**
 * The hill section of the HUD for one frame, or `null` when the frame is not a hill frame. The
 * HUD is keyed on this rather than on `match.mode`: the mode name is an open string on the wire,
 * while the block's schema id is the closed enum the client already fails closed on, and the block
 * is what carries the denominators every row here divides by.
 */
export function hillHudReport({
  entities,
  match,
  ownEntityId,
  tickSequence,
  ticksPerSecond,
}: HillHudInput): HillHudReport | null {
  const rules = kingOfTheHillRules(match);
  if (rules === null) {
    return null;
  }
  const board = scoreboard(entities);
  const ownRow = board.find((row) => row.entityId === ownEntityId);
  return {
    ownPoints: ownRow === undefined ? null : ownRow.points,
    presence: hillPresenceReport(entities, ownEntityId, ticksPerSecond, rules),
    respawnSeconds: respawnCountdownSeconds(
      entities,
      ownEntityId,
      ticksPerSecond,
    ),
    rules,
    scoreboard: board,
    timeRemainingSeconds: runningTimeRemainingSeconds(
      match,
      tickSequence,
      ticksPerSecond,
      rules.timeLimitTicks,
    ),
  };
}

/** A finish record survives the entity it names; controller identity recognizes the own result. */
export interface RaceStanding {
  readonly entity_id: number;
  readonly controller_id: number;
  readonly placement: number;
  readonly finished_tick: number;
}

/** The schema-correlated race block shared by the course renderer, HUD, and results. */
export interface RaceModeState {
  readonly track_half_width: number;
  readonly checkpoint_radius: number;
  readonly track: readonly SessionVector2[];
  readonly checkpoints: readonly SessionVector2[];
  readonly time_limit_ticks: number;
  readonly finish_window_ticks: number;
  readonly standings: readonly RaceStanding[];
}

const RACE_MODE_STATE_SCHEMA_ID: SessionMatchSection['mode_state']['schema_id'] =
  'blob-royale://protocol/v3/mode-state/race';

function isPositiveNumber(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value) && value > 0;
}

function isRacePoint(value: unknown): value is SessionVector2 {
  return (
    isRecord(value) &&
    typeof value.x === 'number' &&
    Number.isFinite(value.x) &&
    typeof value.y === 'number' &&
    Number.isFinite(value.y)
  );
}

function isRaceStanding(value: unknown): value is RaceStanding {
  return (
    isRecord(value) &&
    [
      value.entity_id,
      value.controller_id,
      value.placement,
      value.finished_tick,
    ].every((member) => (publishedCount(member) ?? 0) > 0)
  );
}

/**
 * @canonical race_mode_state -- resolves the schema/value correlation once for every client reader.
 * Input is a schema-validated match or null. Other modes return null; a malformed known block is an
 * internal invariant failure, never an apparently successful frame with its race section missing.
 */
export function raceModeState(
  match: SessionMatchSection | null,
): RaceModeState | null {
  if (
    match === null ||
    match.mode_state.schema_id !== RACE_MODE_STATE_SCHEMA_ID
  ) {
    return null;
  }
  const block: unknown = match.mode_state.value;
  if (
    !isRecord(block) ||
    !isPositiveNumber(block.track_half_width) ||
    !isPositiveNumber(block.checkpoint_radius) ||
    !Array.isArray(block.track) ||
    block.track.length < 2 ||
    !block.track.every(isRacePoint) ||
    !Array.isArray(block.checkpoints) ||
    block.checkpoints.length < 1 ||
    !block.checkpoints.every(isRacePoint) ||
    publishedCount(block.time_limit_ticks) === null ||
    publishedCount(block.finish_window_ticks) === null ||
    !Array.isArray(block.standings) ||
    !block.standings.every(isRaceStanding)
  ) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_INVARIANT_VIOLATION',
      'Cannot read the race mode state from a malformed validated match.',
      { context: { schema_id: match.mode_state.schema_id } },
    );
  }
  // JSON Schema validates the full closed shape at ingress. These guards make the generated open
  // value type usable and expose internal callers that bypass that boundary.
  return block as unknown as RaceModeState;
}

/** The recorded finish order with names from live entities when they still exist. */
export interface RaceStandingsRow {
  readonly controllerId: number;
  readonly displayName: string;
  readonly entityId: number;
  readonly isOwn: boolean;
  readonly placement: number;
}

/** Everything the race HUD says, resolved from committed ticks and the recorded finish order. */
export interface RaceHudReport {
  readonly checkpointCount: number;
  readonly ownCheckpointCount: number | null;
  readonly ownStanding: RaceStanding | null;
  readonly respawnSeconds: number | null;
  readonly awaitingReturn: 'checkpoint' | 'grid' | null;
  readonly standings: readonly RaceStandingsRow[];
  readonly timeRemainingSeconds: number | null;
  readonly finishWindowRemainingSeconds: number | null;
}

export interface RaceHudInput extends HillHudInput {
  readonly ownControllerId: number | null;
}

/**
 * Resolves the local racer's gates and return state, plus every recorded finisher without creating
 * a distance ranking. Once a finish is recorded its window replaces the running time limit, as in
 * RaceObjective. A bodyless participant whose timer is gone is still waiting for a clear return
 * point, including the tick between timer expiry and the next seating attempt.
 */
export function raceHudReport({
  entities,
  match,
  ownControllerId,
  ownEntityId,
  tickSequence,
  ticksPerSecond,
}: RaceHudInput): RaceHudReport | null {
  const state = raceModeState(match);
  if (state === null) {
    return null;
  }
  const ownEntity = findEntityById(entities, ownEntityId);
  const ownStanding =
    state.standings.find(
      (standing) => standing.controller_id === ownControllerId,
    ) ?? null;
  const ownProgress = ownEntity?.components.race_progress;
  const waitingForReturn =
    match?.phase === 'running' &&
    ownProgress !== undefined &&
    ownEntity?.components.physics_body === undefined;
  const timerSeconds = respawnCountdownSeconds(
    entities,
    ownEntityId,
    ticksPerSecond,
  );
  const firstFinish = state.standings[0];
  return {
    checkpointCount: state.checkpoints.length,
    ownCheckpointCount:
      ownStanding !== null
        ? state.checkpoints.length
        : (ownProgress?.next_checkpoint ?? null),
    ownStanding,
    respawnSeconds:
      waitingForReturn && ticksPerSecond > 0 ? (timerSeconds ?? 0) : null,
    awaitingReturn:
      waitingForReturn && timerSeconds === null
        ? ownProgress.next_checkpoint === 0
          ? 'grid'
          : 'checkpoint'
        : null,
    standings: state.standings.map((standing) => ({
      controllerId: standing.controller_id,
      displayName:
        findEntityById(entities, standing.entity_id)?.components.controllable
          ?.display_name ?? `entity ${standing.entity_id}`,
      entityId: standing.entity_id,
      isOwn: standing.controller_id === ownControllerId,
      placement: standing.placement,
    })),
    timeRemainingSeconds:
      firstFinish === undefined
        ? runningTimeRemainingSeconds(
            match,
            tickSequence,
            ticksPerSecond,
            state.time_limit_ticks,
          )
        : null,
    finishWindowRemainingSeconds:
      firstFinish !== undefined &&
      match?.phase === 'running' &&
      tickSequence !== null &&
      tickSequence >= firstFinish.finished_tick &&
      ticksPerSecond > 0
        ? Math.max(
            0,
            state.finish_window_ticks -
              (tickSequence - firstFinish.finished_tick),
          ) / ticksPerSecond
        : null,
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

function winnerPoints(
  entities: readonly SessionEntitySnapshot[],
  winnerEntityId: number,
): number {
  return (
    findEntityById(entities, winnerEntityId)?.components.score?.points ?? 0
  );
}

function pointsLabel(points: number): string {
  return points === 1 ? '1 point' : `${points} points`;
}

/**
 * The winner's sentence is the mode's. The generic match section says who won; how they won is the
 * mode's own ranking, and the hill's is its scoreboard, which is still on the frame at `ended`
 * because the wipe happens on the lobby tick after.
 */
function describeEndedMatch({
  entities,
  match,
  ownControllerId,
  ownEntityId,
}: MatchOverlayInput & {
  readonly match: SessionMatchSection;
}): MatchOverlayDescription {
  const { outcome } = match;
  const race = raceModeState(match);
  if (race !== null) {
    if (outcome.kind === 'drawn') {
      return {
        title: 'Draw',
        detail: !entities.some(
          (entity) => entity.components.controllable !== undefined,
        )
          ? 'No racers remained. The next lobby opens shortly.'
          : race.standings.some((standing) => standing.placement === 1)
            ? 'The first finishers crossed on the same tick. The next lobby opens shortly.'
            : 'Time ran out with the lead tied on gates taken. The next lobby opens shortly.',
      };
    }
    if (outcome.kind === 'won_by_entity' && outcome.winner_entity_id !== null) {
      const winningStanding = race.standings.find(
        (standing) => standing.entity_id === outcome.winner_entity_id,
      );
      const isOwnWin =
        outcome.winner_entity_id === ownEntityId ||
        (ownControllerId !== null &&
          winningStanding?.controller_id === ownControllerId);
      const winner = isOwnWin
        ? 'You'
        : winnerLabel(entities, outcome.winner_entity_id);
      const winningProgress = findEntityById(entities, outcome.winner_entity_id)
        ?.components.race_progress;
      return {
        title: isOwnWin ? 'You win' : 'Winner',
        detail:
          winningStanding !== undefined
            ? `${winner} finished #${winningStanding.placement}.`
            : `${winner} led on gates taken when time ran out${winningProgress === undefined ? '' : ` (${winningProgress.next_checkpoint} of ${race.checkpoints.length})`}.`,
      };
    }
    return { title: 'Match over', detail: 'The next lobby opens shortly.' };
  }
  const isHill = kingOfTheHillRules(match) !== null;
  if (outcome.kind === 'drawn') {
    return {
      detail: isHill
        ? 'Nobody took the hill outright. The next lobby opens shortly.'
        : 'No blob was left standing. The next lobby opens shortly.',
      title: 'Draw',
    };
  }
  if (outcome.kind === 'won_by_entity' && outcome.winner_entity_id !== null) {
    const isOwnWin = outcome.winner_entity_id === ownEntityId;
    const winner = isOwnWin
      ? 'You'
      : winnerLabel(entities, outcome.winner_entity_id);
    return {
      detail: isHill
        ? `${winner} held the hill with ${pointsLabel(winnerPoints(entities, outcome.winner_entity_id))}.`
        : isOwnWin
          ? 'You were the last blob in the zone.'
          : `${winner} was the last blob in the zone.`,
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
      detail:
        raceModeState(match) !== null
          ? 'Get ready: take each gate in order and stay on the road.'
          : kingOfTheHillRules(match) === null
            ? 'Get ready: the zone starts shrinking when the match begins.'
            : 'Get ready: hold the hill to score once the match begins.',
      title: 'Match starting',
    };
  }

  if (ownEntityId !== null) {
    return null;
  }
  const ownFinish = raceModeState(match)?.standings.find(
    (standing) => standing.controller_id === ownControllerId,
  );
  if (ownFinish !== undefined) {
    return {
      title: 'Finished',
      detail: `You finished #${ownFinish.placement}. The other racers can finish until the window closes.`,
    };
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
