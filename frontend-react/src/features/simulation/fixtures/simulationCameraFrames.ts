import type { WorldPoint } from '../rendering/worldProjection';
import {
  validateSessionSnapshotMessage,
  validateSessionWelcomeMessage,
} from '../sessionProtocolValidation';
import { validateSimulationConfigurationResponse } from '../simulationProtocolValidation';
import type {
  SessionWorldSnapshot,
  SimulationConfiguration,
} from '../simulationProtocolTypes';
import type { SimulationCameraOptions } from '../useSimulationCamera';
import type { SimulationSessionIdentity } from '../useSimulationConnection';
import { configurationResponseExample } from './protocolV1Examples';
import { hillSnapshotDocument, welcomeDocument } from './sessionFrames';

export const CAMERA_WORLD_SIZE = Object.freeze({ width: 2400, height: 1800 });
export const CAMERA_WORLD_CENTER: WorldPoint = Object.freeze({
  x: 1200,
  y: 900,
});
export const CAMERA_INITIAL_BODY_CENTER: WorldPoint = Object.freeze({
  x: 500,
  y: 330,
});
export const CAMERA_MOVED_BODY_CENTER: WorldPoint = Object.freeze({
  x: 1500,
  y: 900,
});
export const CAMERA_REPLACEMENT_BODY_CENTER: WorldPoint = Object.freeze({
  x: 700,
  y: 1500,
});
export const CAMERA_PAN_OFFSET: WorldPoint = Object.freeze({ x: 120, y: -80 });
export const CAMERA_PANNED_CENTER: WorldPoint = Object.freeze({
  x: 620,
  y: 250,
});
export const CAMERA_EDGE_BODY_CENTERS = Object.freeze([
  Object.freeze({ x: 10, y: 10 }),
  Object.freeze({ x: 2390, y: 1790 }),
]);

/** An independently validated world larger than the fixed readable viewport in both directions. */
export function cameraConfiguration(): SimulationConfiguration {
  const document = structuredClone(configurationResponseExample);
  document.data.world.width_world_units = CAMERA_WORLD_SIZE.width;
  document.data.world.height_world_units = CAMERA_WORLD_SIZE.height;
  return validateSimulationConfigurationResponse(
    document,
    document.meta.request_id,
  ).data;
}

/** Every call is a new welcome identity even when the server reuses the same numeric IDs. */
export function cameraSessionIdentity(): SimulationSessionIdentity {
  const document = welcomeDocument();
  document.data.terrain.bounds.width_world_units = CAMERA_WORLD_SIZE.width;
  document.data.terrain.bounds.height_world_units = CAMERA_WORLD_SIZE.height;
  const welcome = validateSessionWelcomeMessage(document, null).data;
  return Object.freeze({
    acceptedCommandKinds: welcome.accepted_command_kinds,
    controllerId: welcome.controller_id,
    displayName: welcome.display_name,
    firstEntityId: welcome.entity_id,
    lobbyId: welcome.lobby_id,
    map: welcome.map,
    mode: welcome.mode,
    npcControllerKinds: welcome.npc_controller_kinds,
    seatCountMaximum: welcome.seat_count_maximum,
    terrain: welcome.terrain,
  });
}

export type CameraSnapshotScenario =
  'initial' | 'moved' | 'bodyless' | 'missing' | 'replacement' | 'lobby';

/** Camera lifecycle perturbations remain complete schema-validated hill frames. */
export function cameraSnapshot(
  scenario: CameraSnapshotScenario = 'initial',
  center?: WorldPoint,
): SessionWorldSnapshot {
  const document = hillSnapshotDocument();
  const own = document.data.entities.find(
    (entity) => entity.components.controllable?.controller_id === 3,
  );
  if (own?.components.physics_body === undefined) {
    throw new Error('TEST.CAMERA_FIXTURE_BODY_MISSING');
  }
  own.components.physics_body.position = {
    ...(center ??
      (scenario === 'moved'
        ? CAMERA_MOVED_BODY_CENTER
        : scenario === 'replacement'
          ? CAMERA_REPLACEMENT_BODY_CENTER
          : CAMERA_INITIAL_BODY_CENTER)),
  };
  if (scenario === 'missing') {
    document.data.entities = document.data.entities.filter(
      (entity) => entity !== own,
    );
  }
  if (scenario === 'replacement') {
    own.entity_id = 70;
    document.data.entities.sort(
      (left, right) => left.entity_id - right.entity_id,
    );
  }
  if (scenario === 'lobby') {
    document.data.match.phase = 'lobby';
    document.data.match.start_requested = false;
  }
  const entities = document.data.entities.map((entity) => {
    if (scenario !== 'bodyless' || entity !== own) return entity;
    const { physics_body: removedBody, ...components } = entity.components;
    void removedBody;
    return {
      ...entity,
      components: { ...components, respawn_timer: { ticks_remaining: 300 } },
    };
  });
  return validateSessionSnapshotMessage(
    { ...document, data: { ...document.data, entities } },
    {
      messageSequence: 1,
      requestId: document.meta.request_id,
      tickSequence: null,
    },
  ).data;
}

/** A fresh hook instance gets its own welcome token while consuming immutable validated data. */
export function cameraOptions(
  overrides: Partial<SimulationCameraOptions> = {},
): SimulationCameraOptions {
  return {
    configuration: cameraConfiguration(),
    lobbyId: 1,
    session: cameraSessionIdentity(),
    snapshot: cameraSnapshot(),
    ...overrides,
  };
}
