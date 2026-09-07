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
 */
export function SimulationHud({
  aliveCount,
  displayName,
  isOwnBodyPresent,
  match,
  ownPlacement,
  phaseElapsedSeconds,
  thrust,
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
