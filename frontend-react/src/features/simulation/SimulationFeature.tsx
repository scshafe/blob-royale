import { SimulationViewer } from './SimulationViewer';
import { useSimulationConnection } from './useSimulationConnection';
import { useThrustInput } from './useThrustInput';

/** Connects the canonical hook and the input hook to presentational viewer components. */
export function SimulationFeature() {
  const connection = useSimulationConnection();
  const thrust = useThrustInput({
    // A session that owns no body this frame steers nothing: the commands would be admitted, cost a
    // rate token, and then be discarded because there is no entity to stamp.
    enabled:
      connection.ownEntityId !== null &&
      connection.session !== null &&
      connection.session.acceptedCommandKinds.includes('set_thrust'),
    sendCommand: connection.sendCommand,
  });

  return <SimulationViewer connection={connection} thrust={thrust} />;
}
