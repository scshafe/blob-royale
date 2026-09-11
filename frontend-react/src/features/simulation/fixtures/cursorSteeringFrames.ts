import { findOwnEntityId } from '../sessionSelectors';
import { validateSessionSnapshotMessage } from '../sessionProtocolValidation';
import {
  initialSimulationConnectionState,
  type SimulationCommandSender,
  type SimulationConnection,
  type SimulationSessionIdentity,
} from '../useSimulationConnection';
import type { WorldPoint } from '../rendering/worldProjection';
import {
  cameraConfiguration,
  cameraSnapshot,
  type CameraSnapshotScenario,
} from './simulationCameraFrames';
import { snapshotDocument } from './sessionFrames';

/** Complete validated camera/body fixtures; the caller explicitly retains or replaces the welcome. */
export function cursorSteeringConnection(
  sendCommand: SimulationCommandSender,
  session: SimulationSessionIdentity,
  scenario: CameraSnapshotScenario = 'initial',
  center?: WorldPoint,
): SimulationConnection {
  const data = cameraSnapshot(scenario, center);
  const document = snapshotDocument();
  const snapshot = validateSessionSnapshotMessage(
    { ...document, data },
    {
      messageSequence: 1,
      requestId: document.meta.request_id,
      tickSequence: null,
      terrain: session.terrain,
    },
  );
  return {
    ...initialSimulationConnectionState,
    configuration: cameraConfiguration(),
    session,
    entities: data.entities,
    match: data.match,
    ownEntityId: findOwnEntityId(data.entities, session.controllerId),
    snapshot,
    status: 'connected',
    sendCommand,
  };
}
