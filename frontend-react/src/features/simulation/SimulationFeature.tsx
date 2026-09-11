import { useEffect } from 'react';

import { LobbyDirectoryView } from './LobbyDirectoryView';
import { SimulationShell } from './SimulationShell';
import { SimulationViewer } from './SimulationViewer';
import { useLobbyDirectory } from './useLobbyDirectory';
import { useRoomNavigation } from './useRoomNavigation';
import { useSimulationConnection } from './useSimulationConnection';
import { useThrustInput } from './useThrustInput';
import { useMovementTuning } from './useMovementTuning';

/**
 * The root of the feature, and deliberately the only place its three long-lived things meet: where
 * the page is (`useRoomNavigation`), the socket into the room it is in (`useSimulationConnection`),
 * and the directory it reads while it is in none (`useLobbyDirectory`). All three are mounted here,
 * above the view switch, so choosing a room swaps the content and never re-creates a manager.
 *
 * Leaving is one transition: the room becomes `null`, the connection hook disposes its socket, and
 * the directory hook, now enabled, reads the directory once immediately and then once a second. A
 * refused join is the same transition made for the player, with the server's sentence carried onto
 * the directory as the notice.
 */
export function SimulationFeature() {
  const navigation = useRoomNavigation();
  const connection = useSimulationConnection(navigation.lobbyId);
  const movementTuning = useMovementTuning({
    lobbyId: navigation.lobbyId,
    connection,
  });
  const directory = useLobbyDirectory({
    enabled: navigation.lobbyId === null,
  });
  const thrust = useThrustInput({
    // A session that owns no body this frame steers nothing: the commands would be admitted, cost a
    // rate token, and then be discarded because there is no entity to stamp.
    enabled:
      connection.ownEntityId !== null &&
      connection.session !== null &&
      connection.session.acceptedCommandKinds.includes('set_thrust'),
    sendCommand: connection.sendCommand,
  });

  const { leave } = navigation;
  const refusal = connection.status === 'refused' ? connection.error : null;
  useEffect(() => {
    if (refusal !== null) {
      leave({ kind: 'refused', message: refusal.message });
    }
  }, [leave, refusal]);

  return (
    <SimulationShell
      lobbyId={navigation.lobbyId}
      onLeave={() => {
        leave(null);
      }}
    >
      {navigation.lobbyId === null ? (
        <LobbyDirectoryView
          directory={directory}
          notice={navigation.notice}
          onJoin={navigation.join}
        />
      ) : (
        <SimulationViewer
          connection={connection}
          lobbyId={navigation.lobbyId}
          movementTuning={movementTuning}
          thrust={thrust}
        />
      )}
    </SimulationShell>
  );
}
