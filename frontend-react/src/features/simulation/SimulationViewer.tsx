import { useId, useState } from 'react';

import { LobbyPanel } from './LobbyPanel';
import { MovementTuningPanel } from './MovementTuningPanel';
import type { MovementTuningControls } from './useMovementTuning';
import { MatchOverlay } from './MatchOverlay';
import { SimulationCanvas } from './SimulationCanvas';
import { SimulationCameraControls } from './SimulationCameraControls';
import { useSimulationCamera } from './useSimulationCamera';
import { SimulationDebugPanel } from './SimulationDebugPanel';
import { SimulationHud } from './SimulationHud';
import {
  countAlivePlayers,
  describeMatchOverlay,
  eliminationGraceTicks,
  findEntityById,
  findPlacementForController,
  hillHudReport,
  phaseElapsedSeconds,
  raceHudReport,
  zoneExposureReport,
} from './sessionSelectors';
import type { SimulationConnection } from './useSimulationConnection';
import type { ThrustAimObservation, ThrustDirection } from './useThrustInput';

export interface SimulationViewerProps {
  readonly connection: SimulationConnection;
  /** The room this view is in, which is the shell's decision and not the socket's. */
  readonly lobbyId: number;
  readonly thrust: ThrustDirection;
  readonly movementTuning: MovementTuningControls;
  readonly onAimObservation: (observation: ThrustAimObservation | null) => void;
}

const connectionStatusLabels = Object.freeze({
  awaiting_match:
    'Joined the session. Waiting for the next match to seat a blob…',
  connected: 'Connected to the match session.',
  connecting: 'Connecting to the match session…',
  failed: 'The simulation viewer could not connect.',
  idle: 'Not in a room.',
  loading_configuration: 'Loading public simulation configuration…',
  refused: 'This room refused the join.',
  retrying: 'The match session disconnected. Retrying with bounded backoff…',
});

/** Renders match state and steering feedback from validated values it does not own. */
export function SimulationViewer({
  connection,
  lobbyId,
  thrust,
  movementTuning,
  onAimObservation,
}: SimulationViewerProps) {
  const [debugPanelVisible, setDebugPanelVisible] = useState(false);
  const debugPanelId = useId();
  const statusLabel = connectionStatusLabels[connection.status];
  const controllerId = connection.session?.controllerId ?? null;
  const tickSequence = connection.snapshot?.data.tick_sequence ?? null;
  const { camera, setMode, panByWorldOffset } = useSimulationCamera({
    configuration: connection.configuration,
    lobbyId,
    session: connection.session,
    snapshot: connection.snapshot?.data ?? null,
  });
  // The own entity and the own body are different questions since respawn timers: a knocked-out
  // player keeps its entity, with its name and score, and only the body is gone until it is
  // re-seated. "In play" is the body.
  const ownBody = findEntityById(connection.entities, connection.ownEntityId)
    ?.components.physics_body;
  const isOwnBodyPresent = ownBody !== undefined;
  // The lobby is operable exactly while the match is in `lobby` and this session may start one:
  // a mode with no lobby publishes an empty roster and no `start_match`, and the tick ignores every
  // lobby command outside `lobby`, so the panel is not drawn where nothing it does could land.
  const operableLobby =
    connection.match !== null &&
    connection.match.phase === 'lobby' &&
    connection.session !== null &&
    connection.session.acceptedCommandKinds.includes('start_match')
      ? { match: connection.match, session: connection.session }
      : null;

  return (
    <section aria-labelledby="simulation-viewer-heading">
      <h2 id="simulation-viewer-heading">Room {lobbyId}</h2>
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
              camera={camera}
              configuration={connection.configuration}
              onPan={panByWorldOffset}
              ownEntityId={connection.ownEntityId}
              ownBodyPosition={ownBody?.position ?? null}
              onAimObservation={onAimObservation}
              snapshot={connection.snapshot?.data ?? null}
              terrain={connection.session?.terrain ?? null}
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
            <SimulationCameraControls
              mode={camera.mode}
              setMode={setMode}
              panByWorldOffset={panByWorldOffset}
            />
            {operableLobby === null ? null : (
              <LobbyPanel
                entities={connection.entities}
                match={operableLobby.match}
                npcCatalogue={operableLobby.session.npcCatalogue}
                ownControllerId={controllerId}
                seatCountMaximum={operableLobby.session.seatCountMaximum}
                sendCommand={connection.sendCommand}
              />
            )}
            <SimulationHud
              aliveCount={countAlivePlayers(connection.entities)}
              displayName={connection.session?.displayName ?? null}
              hill={hillHudReport({
                entities: connection.entities,
                match: connection.match,
                ownEntityId: connection.ownEntityId,
                tickSequence,
                ticksPerSecond:
                  connection.configuration.simulation.ticks_per_second,
              })}
              isOwnBodyPresent={isOwnBodyPresent}
              match={connection.match}
              ownEntityId={connection.ownEntityId}
              ownPlacement={findPlacementForController(
                connection.match,
                controllerId,
              )}
              phaseElapsedSeconds={phaseElapsedSeconds(
                connection.match,
                tickSequence,
                connection.configuration.simulation.ticks_per_second,
              )}
              race={raceHudReport({
                entities: connection.entities,
                match: connection.match,
                ownControllerId: controllerId,
                ownEntityId: connection.ownEntityId,
                tickSequence,
                ticksPerSecond:
                  connection.configuration.simulation.ticks_per_second,
              })}
              thrust={thrust}
              zoneExposure={zoneExposureReport(
                connection.entities,
                connection.ownEntityId,
                connection.configuration.simulation.ticks_per_second,
                eliminationGraceTicks(connection.match),
              )}
            />
            <p className="SteeringHint">
              Aim with the cursor, click or tab into the arena, and hold Space
              to move. Release Space to coast. Left-drag pans in Manual view;
              WASD and arrows do not steer.
            </p>
            <MovementTuningPanel controls={movementTuning} />
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
