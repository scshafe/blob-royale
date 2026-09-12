import type {
  AbilityStatusReport,
  AbilityWindowReport,
  HillHudReport,
  RaceHudReport,
  ScoreboardRow,
  ShieldStatusReport,
  ZoneExposureReport,
} from './sessionSelectors';
import type {
  SessionMatchSection,
  SessionPlacement,
} from './simulationProtocolTypes';
import type { ThrustDirection } from './useThrustInput';

export interface SimulationHudProps {
  /** The own blob's ability windows at this frame's tick; a `null` member is unpublished. */
  readonly ability: AbilityStatusReport;
  readonly aliveCount: number;
  readonly displayName: string | null;
  /** The hill section, present exactly when the frame carries the `king_of_the_hill` block. */
  readonly hill: HillHudReport | null;
  readonly isOwnBodyPresent: boolean;
  readonly match: SessionMatchSection | null;
  /** The session's own entity this frame, which the scoreboard marks; a body is not required. */
  readonly ownEntityId: number | null;
  readonly ownPlacement: SessionPlacement | null;
  readonly phaseElapsedSeconds: number | null;
  /** The race section, present exactly when the frame carries the race schema id. */
  readonly race: RaceHudReport | null;
  readonly thrust: ThrustDirection;
  readonly zoneExposure: ZoneExposureReport | null;
}

const AWAITING = 'Awaiting match';

function formatSeconds(seconds: number | null): string {
  return seconds === null ? '—' : `${seconds.toFixed(1)} s`;
}

function formatPlacement(
  placement: SessionPlacement | null,
  isOwnBodyPresent: boolean,
): string {
  if (placement !== null) {
    return `#${placement.placement}`;
  }
  return isOwnBodyPresent ? 'In play' : '—';
}

/**
 * What the row says while the own blob is outside. The remainder is the answer to the question a
 * player actually has, so it is shown whenever the frame carries a grace to subtract from; elapsed
 * exposure is the fallback for a mode that publishes none, and is what this row said for every
 * frame before protocol 2.2 put `elimination_grace_ticks` on the wire.
 */
function formatZoneExposure(exposure: ZoneExposureReport): string {
  return exposure.remainingSeconds === null
    ? `Outside ${formatSeconds(exposure.elapsedSeconds)}`
    : `${formatSeconds(exposure.remainingSeconds)} left`;
}

function formatScore(ownPoints: number | null, pointsToWin: number): string {
  return `${ownPoints === null ? '—' : String(ownPoints)} of ${String(pointsToWin)}`;
}

/**
 * What the shield row says, and it has three readings rather than two. Protection is not the
 * component: a stun cancels protection by shortening it to the cancelling tick and leaves the
 * cooldown running, so the majority of a shield component's published life by duration is exactly
 * the third reading -- no protection at all, the component still on the wire because its cooldown
 * is. A row keyed on presence would tell a stunned player they are safe.
 *
 * No reading names a duration the wire does not carry. Only the protection that actually happened is
 * published, and a cancellation shortens it, so the row counts down the remainder of the window in
 * front of it and never states how long a full opening would have been.
 */
function formatShieldProtection(shield: ShieldStatusReport): string {
  if (shield.perfect.isActive) {
    return `Perfect opening ${formatSeconds(shield.perfect.remainingSeconds)} left`;
  }
  if (shield.protection.isActive) {
    return `Protected ${formatSeconds(shield.protection.remainingSeconds)} left`;
  }
  return 'No protection';
}

/**
 * What a cooldown row says, and "cooling" and "cooldown over" are its only two readings. Neither is
 * a claim of availability, and that is the point: charge's final refusal is the server's safety
 * envelope, which is deliberately not on the wire, and a shield still holding protection refuses its
 * own next pulse. A row that said "ready" would be lying on exactly the frames a player would act on
 * it, so an elapsed cooldown reports only that the cooldown elapsed.
 */
function formatCooldown(cooldown: AbilityWindowReport): string {
  return cooldown.isActive
    ? `Cooling ${formatSeconds(cooldown.remainingSeconds)}`
    : 'Cooldown over';
}

/**
 * The own blob's ability rows. Each exists exactly while the frame publishes the component behind
 * it, which is the bargain the zone-exposure row already makes: the server erases a status the tick
 * it ends, so a row clears with the snapshot that cleared it and no client timer can disagree.
 *
 * The stun row is the one exception, and it is narrower rather than wider: it exists only while the
 * window actually contains the tick, because a published-but-elapsed stun still locks nothing and a
 * row saying "Stunned" would be false. Its remainder is read against a denominator that can grow --
 * a merge keeps the activation and takes the maximum expiry -- so the row states time left rather
 * than a share of a window whose length is not stable.
 */
function AbilityRows({ ability }: { readonly ability: AbilityStatusReport }) {
  const { charge, shield, stun } = ability;
  return (
    <>
      {stun === null || !stun.isActive ? null : (
        <tr className="MatchHudDanger">
          <th scope="row">Stunned</th>
          <td>{formatSeconds(stun.remainingSeconds)} left</td>
        </tr>
      )}
      {shield === null ? null : (
        <>
          <tr>
            <th scope="row">Shield</th>
            <td>{formatShieldProtection(shield)}</td>
          </tr>
          <tr>
            <th scope="row">Shield cooldown</th>
            <td>{formatCooldown(shield.cooldown)}</td>
          </tr>
        </>
      )}
      {charge === null ? null : (
        <tr>
          <th scope="row">Charge cooldown</th>
          <td>{formatCooldown(charge)}</td>
        </tr>
      )}
    </>
  );
}

const RING_RADIUS = 8;
const RING_CIRCUMFERENCE = 2 * Math.PI * RING_RADIUS;

/**
 * The progress ring the hill row carries beside its words: the earned part of the interval as an
 * arc against the whole. Decorative -- the text in the same cell is the accessible reading -- and
 * drawn from the same fraction `hillProgressFraction` clamps, so a zero interval draws a full ring.
 */
function HillProgressRing({ fraction }: { readonly fraction: number }) {
  return (
    <svg
      aria-hidden="true"
      className="HillProgressRing"
      height="20"
      viewBox="0 0 20 20"
      width="20"
    >
      <circle
        cx="10"
        cy="10"
        fill="none"
        r={RING_RADIUS}
        stroke="#e2e8f0"
        strokeWidth="3"
      />
      <circle
        cx="10"
        cy="10"
        fill="none"
        r={RING_RADIUS}
        stroke="#b45309"
        strokeDasharray={`${String(RING_CIRCUMFERENCE * fraction)} ${String(RING_CIRCUMFERENCE)}`}
        strokeWidth="3"
        transform="rotate(-90 10 10)"
      />
    </svg>
  );
}

/**
 * What the hill row says. A knocked-out blob is counting down to a seat, a blob with a presence is
 * counting up to a point, a blob with a body and no presence is off the hill, and a session with no
 * entity has nothing to hold.
 */
function HillRow({
  hill,
  isOwnBodyPresent,
}: {
  readonly hill: HillHudReport;
  readonly isOwnBodyPresent: boolean;
}) {
  if (hill.respawnSeconds !== null) {
    return (
      <tr className="MatchHudDanger">
        <th scope="row">Hill</th>
        <td>Back in {formatSeconds(hill.respawnSeconds)}</td>
      </tr>
    );
  }
  if (hill.presence !== null) {
    return (
      <tr>
        <th scope="row">Hill</th>
        <td>
          <HillProgressRing fraction={hill.presence.spentFraction} />
          {formatSeconds(hill.presence.remainingSeconds)} to a point
        </td>
      </tr>
    );
  }
  return (
    <tr>
      <th scope="row">Hill</th>
      <td>{isOwnBodyPresent ? 'Off the hill' : '—'}</td>
    </tr>
  );
}

function ScoreboardTable({
  rows,
  ownEntityId,
}: {
  readonly rows: readonly ScoreboardRow[];
  readonly ownEntityId: number | null;
}) {
  return (
    <table className="MatchHud Scoreboard">
      <caption>Scoreboard</caption>
      <tbody>
        {rows.map((row) => (
          <tr
            aria-current={row.entityId === ownEntityId ? 'true' : undefined}
            className={row.entityId === ownEntityId ? 'MatchHudOwn' : undefined}
            key={row.entityId}
          >
            <th scope="row">{row.displayName}</th>
            <td>
              {row.isInPlay
                ? String(row.points)
                : `${String(row.points)} (out)`}
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

function RaceRows({ race }: { readonly race: RaceHudReport }) {
  return (
    <>
      <tr>
        <th scope="row">Gate</th>
        <td>
          {race.ownCheckpointCount === null
            ? '—'
            : `${race.ownCheckpointCount} of ${race.checkpointCount}`}
        </td>
      </tr>
      {race.timeRemainingSeconds === null ? null : (
        <tr>
          <th scope="row">Time left</th>
          <td>{formatSeconds(race.timeRemainingSeconds)}</td>
        </tr>
      )}
      {race.finishWindowRemainingSeconds === null ? null : (
        <tr>
          <th scope="row">Finish window</th>
          <td>{formatSeconds(race.finishWindowRemainingSeconds)}</td>
        </tr>
      )}
      {race.ownStanding === null ? null : (
        <tr>
          <th scope="row">Finish</th>
          <td>#{race.ownStanding.placement}</td>
        </tr>
      )}
      {race.respawnSeconds === null ? null : (
        <tr className="MatchHudDanger">
          <th scope="row">Return</th>
          <td>
            Back on the road in {formatSeconds(race.respawnSeconds)}
            {race.awaitingReturn === null
              ? null
              : ` · waiting for a clear ${race.awaitingReturn === 'grid' ? 'starting grid space' : 'checkpoint'}`}
          </td>
        </tr>
      )}
    </>
  );
}

function RaceStandingsTable({ race }: { readonly race: RaceHudReport }) {
  return (
    <table className="MatchHud Scoreboard">
      <caption>Standings</caption>
      <tbody>
        {race.standings.length === 0 ? (
          <tr>
            <td colSpan={3}>No finishers yet</td>
          </tr>
        ) : (
          race.standings.map((row) => (
            <tr
              aria-current={row.isOwn ? 'true' : undefined}
              className={row.isOwn ? 'MatchHudOwn' : undefined}
              key={row.entityId}
            >
              <th scope="row">{row.isOwn ? 'You' : row.displayName}</th>
              <td>#{row.placement}</td>
              <td title="Certified finish time: tick and normalized fraction">
                tick {row.finishedTick} + {row.finishedTickOffset}
              </td>
            </tr>
          ))
        )}
      </tbody>
    </table>
  );
}

function formatThrust(thrust: ThrustDirection): string {
  return thrust.x === 0 && thrust.y === 0
    ? 'idle'
    : `${thrust.x.toFixed(2)}, ${thrust.y.toFixed(2)}`;
}

/**
 * The player-facing match header. Alive count is counted from the entities the client already
 * renders, and phase elapsed is derived from `phase_started_tick`, which is the only phase timing
 * the wire carries: the mode's configured phase durations are composition-root configuration and
 * are deliberately not published, so this reports elapsed time rather than inventing a remainder.
 *
 * Zone exposure is no longer the same bargain, and the difference is the point of protocol 2.2. The
 * wire carries `zone_exposure.outside_ticks` and now also `elimination_grace_ticks`, so the row
 * counts the real remainder down instead of counting elapsed exposure up; a mode that publishes no
 * grace still gets the elapsed reading, because the alternative would be inventing a duration. The
 * row exists only while the own blob is outside: `zoneExposure` is `null` on the first snapshot
 * after re-entry, so the warning clears with the snapshot that cleared the server's counter and no
 * client timer can disagree.
 *
 * The hill section is the same bargain made a second time. It exists exactly while the frame carries
 * the `king_of_the_hill` block, whose three constants are the denominators of every row it adds:
 * time left against `time_limit_ticks`, the own score against `points_to_win`, the own presence
 * against `point_interval_ticks`, and the return countdown from `respawn_timer`. The scoreboard is
 * the `score` components of the participants the client already renders, so the board and the
 * arena cannot disagree about who has what. A royale frame carries none of this and renders exactly
 * the rows it always did; placement is royale's ranking and is not shown for the hill, whose ranking
 * is the board.
 *
 * The ability rows are the screen-space half of Step 20's combat feedback, and they are here rather
 * than in a world renderer because a cooldown readout must not pan and scale with the camera. Every
 * value on them is a published absolute tick read against this frame's own tick, converted to
 * seconds by the one cadence the configuration publishes; nothing on them counts down locally, so a
 * dropped frame delays the reading rather than desynchronising it. Two things they never say,
 * because the wire does not carry them: that charge is *ready*, which only the server's safety
 * envelope can decide, and how long a shield's authored protection would have been, which a
 * cancellation makes unknowable from the outside.
 */
export function SimulationHud({
  ability,
  aliveCount,
  displayName,
  hill,
  isOwnBodyPresent,
  match,
  ownEntityId,
  ownPlacement,
  phaseElapsedSeconds,
  race,
  thrust,
  zoneExposure,
}: SimulationHudProps) {
  return (
    <>
      <table className="MatchHud">
        <caption>Match status</caption>
        <tbody>
          <tr>
            <th scope="row">Player</th>
            <td>{displayName ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Phase</th>
            <td>{match?.phase ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Phase elapsed</th>
            <td>{formatSeconds(phaseElapsedSeconds)}</td>
          </tr>
          {hill === null || hill.timeRemainingSeconds === null ? null : (
            <tr>
              <th scope="row">Time left</th>
              <td>{formatSeconds(hill.timeRemainingSeconds)}</td>
            </tr>
          )}
          {hill === null ? null : (
            <tr>
              <th scope="row">Score</th>
              <td>{formatScore(hill.ownPoints, hill.rules.pointsToWin)}</td>
            </tr>
          )}
          {hill === null ? null : (
            <HillRow hill={hill} isOwnBodyPresent={isOwnBodyPresent} />
          )}
          {race === null ? null : <RaceRows race={race} />}
          {zoneExposure === null ? null : (
            <tr className="MatchHudDanger">
              <th scope="row">Zone exposure</th>
              <td>{formatZoneExposure(zoneExposure)}</td>
            </tr>
          )}
          <AbilityRows ability={ability} />
          <tr>
            <th scope="row">Alive</th>
            <td>{aliveCount}</td>
          </tr>
          {hill === null && race === null ? (
            <tr>
              <th scope="row">Placement</th>
              <td>{formatPlacement(ownPlacement, isOwnBodyPresent)}</td>
            </tr>
          ) : null}
          <tr>
            <th scope="row">Thrust</th>
            <td>{formatThrust(thrust)}</td>
          </tr>
        </tbody>
      </table>
      {hill === null ? null : (
        <ScoreboardTable ownEntityId={ownEntityId} rows={hill.scoreboard} />
      )}
      {race === null ? null : <RaceStandingsTable race={race} />}
    </>
  );
}
