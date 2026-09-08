import type { ZoneExposureReport } from './sessionSelectors';
import type {
  SessionMatchSection,
  SessionPlacement,
} from './simulationProtocolTypes';
import type { ThrustDirection } from './useThrustInput';

export interface SimulationHudProps {
  readonly aliveCount: number;
  readonly displayName: string | null;
  readonly isOwnBodyPresent: boolean;
  readonly match: SessionMatchSection | null;
  readonly ownPlacement: SessionPlacement | null;
  readonly phaseElapsedSeconds: number | null;
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
 */
export function SimulationHud({
  aliveCount,
  displayName,
  isOwnBodyPresent,
  match,
  ownPlacement,
  phaseElapsedSeconds,
  thrust,
  zoneExposure,
}: SimulationHudProps) {
  return (
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
        {zoneExposure === null ? null : (
          <tr className="MatchHudDanger">
            <th scope="row">Zone exposure</th>
            <td>{formatZoneExposure(zoneExposure)}</td>
          </tr>
        )}
        <tr>
          <th scope="row">Alive</th>
          <td>{aliveCount}</td>
        </tr>
        <tr>
          <th scope="row">Placement</th>
          <td>{formatPlacement(ownPlacement, isOwnBodyPresent)}</td>
        </tr>
        <tr>
          <th scope="row">Thrust</th>
          <td>{formatThrust(thrust)}</td>
        </tr>
      </tbody>
    </table>
  );
}
