import { useCallback, useState } from 'react';

import { SimulationApiError } from './SimulationApiError';
import type { WorldPoint } from './rendering/worldProjection';
import { findEntityById, findOwnEntityId } from './sessionSelectors';
import type {
  SessionWorldSnapshot,
  SimulationConfiguration,
} from './simulationProtocolTypes';
import type { SimulationSessionIdentity } from './useSimulationConnection';

export type SimulationCameraMode = 'follow' | 'manual';

export interface SimulationCamera {
  readonly center: WorldPoint;
  readonly mode: SimulationCameraMode;
}

export interface SimulationCameraOptions {
  readonly configuration: SimulationConfiguration | null;
  readonly lobbyId: number;
  readonly session: SimulationSessionIdentity | null;
  readonly snapshot: SessionWorldSnapshot | null;
}

export interface SimulationCameraControls {
  readonly camera: SimulationCamera;
  readonly panByWorldOffset: (offset: WorldPoint) => void;
  readonly setMode: (mode: SimulationCameraMode) => void;
}

interface SimulationCameraState {
  readonly camera: SimulationCamera;
  readonly lobbyId: number;
  readonly session: SimulationSessionIdentity | null;
  readonly worldHeight: number | null;
  readonly worldWidth: number | null;
}

/** The session's first entity is not its durable body: a return may replace or restore that body. */
function ownBodyCenter({
  configuration,
  lobbyId,
  session,
  snapshot,
}: SimulationCameraOptions): WorldPoint | null {
  if (
    configuration === null ||
    session === null ||
    snapshot === null ||
    session.lobbyId !== lobbyId
  ) {
    return null;
  }
  const ownEntityId = findOwnEntityId(snapshot.entities, session.controllerId);
  return (
    findEntityById(snapshot.entities, ownEntityId)?.components.physics_body
      ?.position ?? null
  );
}

function initialCameraState(
  options: SimulationCameraOptions,
): SimulationCameraState {
  const worldWidth = options.configuration?.world.width_world_units ?? null;
  const worldHeight = options.configuration?.world.height_world_units ?? null;
  return {
    camera: {
      center: ownBodyCenter(options) ?? {
        // No configuration means the stage is not rendered. This origin is a loading state,
        // not a substitute world size; the first configuration establishes its real centre.
        x: worldWidth === null ? 0 : worldWidth / 2,
        y: worldHeight === null ? 0 : worldHeight / 2,
      },
      mode: 'follow',
    },
    lobbyId: options.lobbyId,
    session: options.session,
    worldHeight,
    worldWidth,
  };
}

/**
 * @canonical simulation_camera -- local camera identity and centre, never gameplay commands.
 * @extension-point client_camera -- follow and manual select a centre for the same projection.
 *
 * Accepts validated configuration/snapshots and the immutable welcome identity. Follow uses this
 * render's body without a one-frame effect delay, retaining the last centre only while bodyless.
 * A new room, welcome object, or world establishes a fresh follow camera. During a room change,
 * the previous room's welcome is not a target while the connection effect disposes it. Numeric IDs
 * can repeat across server restarts, so they cannot distinguish sessions. Manual pan selects
 * manual mode and clamps its centre to world bounds; follow never clamps the visible extent.
 *
 * Pan offsets must be finite and configuration must be present; otherwise an explicit
 * SIMULATION.CAMERA_OFFSET_INVALID error is raised. Related: sessionSelectors.findOwnEntityId.
 */
export function useSimulationCamera(
  options: SimulationCameraOptions,
): SimulationCameraControls {
  const { configuration, lobbyId, session } = options;
  const worldWidth = configuration?.world.width_world_units ?? null;
  const worldHeight = configuration?.world.height_world_units ?? null;
  const [state, setState] = useState<SimulationCameraState>(() =>
    initialCameraState(options),
  );
  let currentState = state;
  if (
    state.lobbyId !== lobbyId ||
    state.session !== session ||
    state.worldWidth !== worldWidth ||
    state.worldHeight !== worldHeight
  ) {
    currentState = initialCameraState(options);
    setState(currentState);
  } else if (state.camera.mode === 'follow') {
    const center = ownBodyCenter(options);
    if (
      center !== null &&
      (center.x !== state.camera.center.x || center.y !== state.camera.center.y)
    ) {
      // Conditional same-component state reconciliation lets React discard this stale render
      // before committing children. An effect would paint one tick behind the authoritative body.
      currentState = { ...state, camera: { ...state.camera, center } };
      setState(currentState);
    }
  }

  const setMode = useCallback((mode: SimulationCameraMode): void => {
    setState((previous) =>
      previous.camera.mode === mode
        ? previous
        : { ...previous, camera: { ...previous.camera, mode } },
    );
  }, []);

  const panByWorldOffset = useCallback(
    (offset: WorldPoint): void => {
      if (
        worldWidth === null ||
        worldHeight === null ||
        !Number.isFinite(offset.x) ||
        !Number.isFinite(offset.y)
      ) {
        throw new SimulationApiError(
          'SIMULATION.CAMERA_OFFSET_INVALID',
          'Camera pan requires a configured world and a finite world-space offset.',
          {
            context: {
              offset_x: offset.x,
              offset_y: offset.y,
              world_height: worldHeight,
              world_width: worldWidth,
            },
          },
        );
      }
      setState((previous) => ({
        ...previous,
        camera: {
          center: {
            x: Math.min(
              worldWidth,
              Math.max(0, previous.camera.center.x + offset.x),
            ),
            y: Math.min(
              worldHeight,
              Math.max(0, previous.camera.center.y + offset.y),
            ),
          },
          mode: 'manual',
        },
      }));
    },
    [worldHeight, worldWidth],
  );

  return { camera: currentState.camera, panByWorldOffset, setMode };
}
