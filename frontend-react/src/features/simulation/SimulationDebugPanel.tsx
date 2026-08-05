import { DEBUG_PLAYER_ROW_LIMIT } from './simulationConstants';
import type {
  SimulationConfiguration,
  SimulationSnapshotMessage,
} from './simulationProtocolTypes';

export interface SimulationDebugPanelProps {
  readonly configuration: SimulationConfiguration;
  readonly snapshot: SimulationSnapshotMessage | null;
}

/** Presents a bounded textual view of already validated simulation state. */
export function SimulationDebugPanel({
  configuration,
  snapshot,
}: SimulationDebugPanelProps) {
  const players = snapshot?.data.players.slice(0, DEBUG_PLAYER_ROW_LIMIT) ?? [];

  return (
    <section aria-labelledby="simulation-debug-heading" className="DebugPanel">
      <h2 id="simulation-debug-heading">Simulation details</h2>

      <table>
        <caption>Public configuration</caption>
        <tbody>
          <tr>
            <th scope="row">World size</th>
            <td>
              {configuration.world.width_world_units} ×{' '}
              {configuration.world.height_world_units} wu
            </td>
          </tr>
          <tr>
            <th scope="row">Player radius</th>
            <td>{configuration.world.player_radius_world_units} wu</td>
          </tr>
          <tr>
            <th scope="row">Simulation cadence</th>
            <td>{configuration.simulation.ticks_per_second} ticks/s</td>
          </tr>
          <tr>
            <th scope="row">Presentation cadence</th>
            <td>
              {configuration.presentation.snapshots_per_second} snapshots/s
            </td>
          </tr>
        </tbody>
      </table>

      <table>
        <caption>Snapshot protocol metadata</caption>
        <tbody>
          <tr>
            <th scope="row">Protocol version</th>
            <td>{snapshot?.meta.protocol_version ?? 'Awaiting snapshot'}</td>
          </tr>
          <tr>
            <th scope="row">Schema ID</th>
            <td>{snapshot?.meta.schema_id ?? 'Awaiting snapshot'}</td>
          </tr>
          <tr>
            <th scope="row">Request ID</th>
            <td>{snapshot?.meta.request_id ?? 'Awaiting snapshot'}</td>
          </tr>
          <tr>
            <th scope="row">Message sequence</th>
            <td>{snapshot?.meta.message_sequence ?? 'Awaiting snapshot'}</td>
          </tr>
          <tr>
            <th scope="row">Tick sequence</th>
            <td>{snapshot?.data.tick_sequence ?? 'Awaiting snapshot'}</td>
          </tr>
          <tr>
            <th scope="row">Sent at UTC</th>
            <td>{snapshot?.meta.sent_at_utc ?? 'Awaiting snapshot'}</td>
          </tr>
        </tbody>
      </table>

      <table>
        <caption>
          Players — showing {players.length} of{' '}
          {snapshot?.data.players.length ?? 0}
        </caption>
        <thead>
          <tr>
            <th scope="col">Entity</th>
            <th scope="col">Position (wu)</th>
            <th scope="col">Velocity (wu/s)</th>
            <th scope="col">Acceleration (wu/s²)</th>
          </tr>
        </thead>
        <tbody>
          {players.map((player) => (
            <tr key={player.entity_id}>
              <th scope="row">{player.entity_id}</th>
              <td>
                ({player.position.x}, {player.position.y})
              </td>
              <td>
                ({player.velocity.x}, {player.velocity.y})
              </td>
              <td>
                ({player.acceleration.x}, {player.acceleration.y})
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </section>
  );
}
