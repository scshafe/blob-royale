import { useId, useState } from 'react';

import { MatchOverlay } from './MatchOverlay';
import { SimulationCanvas } from './SimulationCanvas';
import { SimulationDebugPanel } from './SimulationDebugPanel';
import { SimulationHud } from './SimulationHud';
import {
  countAlivePlayers,
  describeMatchOverlay,
  findPlacementForController,
  phaseElapsedSeconds,
} from './sessionSelectors';
import type { SimulationConnection } from './useSimulationConnection';
import type { ThrustDirection } from './useThrustInput';

export interface SimulationViewerProps {
  readonly connection: SimulationConnection;
  readonly thrust: ThrustDirection;
}

const connectionStatusLabels = Object.freeze({
  awaiting_match:
    'Joined the session. Waiting for the next match to seat a blob…',
  connected: 'Connected to the match session.',
  connecting: 'Connecting to the match session…',
  failed: 'The simulation viewer could not connect.',
  loading_configuration: 'Loading public simulation configuration…',
  retrying: 'The match session disconnected. Retrying with bounded backoff…',
});

/** Renders match state and steering feedback from validated values it does not own. */
export function SimulationViewer({
  connection,
  thrust,
}: SimulationViewerProps) {
  const [debugPanelVisible, setDebugPanelVisible] = useState(false);
  const debugPanelId = useId();
  const statusLabel = connectionStatusLabels[connection.status];
  const controllerId = connection.session?.controllerId ?? null;

  return (
    <section aria-labelledby="simulation-viewer-heading">
      <h1 id="simulation-viewer-heading">Blob Royale</h1>
      <p
        aria-live={connection.status === 'failed' ? 'assertive' : 'polite'}
        role={connection.status === 'failed' ? 'alert' : 'status'}
      >
        {statusLabel}
      </p>
      {connection.error === null ? null : (
        <p className="ConnectionError">
          {connection.error.code}: {connection.error.message}
        </p>
      )}
      {connection.configuration === null ? null : (
        <div className="SimulationLayout">
          <div className="SimulationStage">
            <SimulationCanvas
              configuration={connection.configuration}
              ownEntityId={connection.ownEntityId}
              snapshot={connection.snapshot?.data ?? null}
            />
            <MatchOverlay
              description={describeMatchOverlay({
                entities: connection.entities,
                match: connection.match,
                ownControllerId: controllerId,
                ownEntityId: connection.ownEntityId,
              })}
            />
          </div>
          <div className="SimulationSidebar">
            <SimulationHud
              aliveCount={countAlivePlayers(connection.entities)}
              displayName={connection.session?.displayName ?? null}
              isOwnBodyPresent={connection.ownEntityId !== null}
              match={connection.match}
              ownPlacement={findPlacementForController(
                connection.match,
                controllerId,
              )}
              phaseElapsedSeconds={phaseElapsedSeconds(
                connection.match,
                connection.snapshot?.data.tick_sequence ?? null,
                connection.configuration.simulation.ticks_per_second,
              )}
              thrust={thrust}
            />
            <p className="SteeringHint">
              Steer with WASD or the arrow keys while your blob is in the arena.
            </p>
            <button
              aria-controls={debugPanelId}
              aria-expanded={debugPanelVisible}
              onClick={() => {
                setDebugPanelVisible(!debugPanelVisible);
              }}
              type="button"
            >
              {debugPanelVisible
                ? 'Hide simulation details'
                : 'Show simulation details'}
            </button>
            <div id={debugPanelId}>
              {debugPanelVisible ? (
                <SimulationDebugPanel
                  configuration={connection.configuration}
                  session={connection.session}
                  snapshot={connection.snapshot}
                />
              ) : null}
            </div>
          </div>
        </div>
      )}
    </section>
  );
}
