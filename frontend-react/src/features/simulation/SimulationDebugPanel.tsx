import { DEBUG_ENTITY_ROW_LIMIT } from './simulationConstants';
import type {
  SessionSnapshotMessage,
  SimulationConfiguration,
} from './simulationProtocolTypes';
import type { SimulationSessionIdentity } from './useSimulationConnection';

export interface SimulationDebugPanelProps {
  readonly configuration: SimulationConfiguration;
  readonly session: SimulationSessionIdentity | null;
  readonly snapshot: SessionSnapshotMessage | null;
}

const AWAITING = 'Awaiting frame';

function formatVector(vector: { readonly x: number; readonly y: number }) {
  return `(${vector.x}, ${vector.y})`;
}

/** Presents a bounded textual view of already validated simulation state. */
export function SimulationDebugPanel({
  configuration,
  session,
  snapshot,
}: SimulationDebugPanelProps) {
  const entities = snapshot?.data.entities ?? [];
  const shownEntities = entities.slice(0, DEBUG_ENTITY_ROW_LIMIT);
  const match = snapshot?.data.match ?? null;

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
        <caption>Session identity</caption>
        <tbody>
          <tr>
            <th scope="row">Display name</th>
            <td>{session?.displayName ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Controller ID</th>
            <td>{session?.controllerId ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">First entity ID</th>
            <td>{session?.firstEntityId ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Mode and map</th>
            <td>
              {session === null ? AWAITING : `${session.mode} / ${session.map}`}
            </td>
          </tr>
          <tr>
            <th scope="row">Accepted commands</th>
            <td>
              {session === null || session.acceptedCommandKinds.length === 0
                ? 'none'
                : session.acceptedCommandKinds.join(', ')}
            </td>
          </tr>
        </tbody>
      </table>

      <table>
        <caption>Session protocol metadata</caption>
        <tbody>
          <tr>
            <th scope="row">Protocol version</th>
            <td>{snapshot?.meta.protocol_version ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Schema ID</th>
            <td>{snapshot?.meta.schema_id ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Request ID</th>
            <td>{snapshot?.meta.request_id ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Message sequence</th>
            <td>{snapshot?.meta.message_sequence ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Tick sequence</th>
            <td>{snapshot?.data.tick_sequence ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Sent at UTC</th>
            <td>{snapshot?.meta.sent_at_utc ?? AWAITING}</td>
          </tr>
        </tbody>
      </table>

      <table>
        <caption>Match section</caption>
        <tbody>
          <tr>
            <th scope="row">Mode</th>
            <td>{match?.mode ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Phase</th>
            <td>{match?.phase ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Phase started tick</th>
            <td>{match?.phase_started_tick ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Outcome</th>
            <td>{match?.outcome.kind ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Placements</th>
            <td>{match?.placements.length ?? AWAITING}</td>
          </tr>
          <tr>
            <th scope="row">Mode state</th>
            <td>{match?.mode_state.schema_id ?? AWAITING}</td>
          </tr>
        </tbody>
      </table>

      <table>
        <caption>
          Entities — showing {shownEntities.length} of {entities.length}
        </caption>
        <thead>
          <tr>
            <th scope="col">Entity</th>
            <th scope="col">Components</th>
            <th scope="col">Position (wu)</th>
            <th scope="col">Velocity (wu/s)</th>
          </tr>
        </thead>
        <tbody>
          {shownEntities.map((entity) => (
            <tr key={entity.entity_id}>
              <th scope="row">{entity.entity_id}</th>
              <td>{Object.keys(entity.components).sort().join(' ')}</td>
              <td>
                {entity.components.physics_body === undefined
                  ? '—'
                  : formatVector(entity.components.physics_body.position)}
              </td>
              <td>
                {entity.components.physics_body === undefined
                  ? '—'
                  : formatVector(entity.components.physics_body.velocity)}
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </section>
  );
}
