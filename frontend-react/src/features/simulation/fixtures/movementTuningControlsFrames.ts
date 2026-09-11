import type {
  MovementTuningExchangeState,
  SessionMovementState,
  SessionSetMovementTuningCommand,
  SessionTuningResult,
} from '../simulationProtocolTypes';
import {
  initialSimulationConnectionState,
  type SimulationCommandSender,
  type SimulationConnection,
} from '../useSimulationConnection';
import { cameraSessionIdentity } from './simulationCameraFrames';
import { snapshotDocument } from './sessionFrames';
import { validateSessionSnapshotMessage } from '../sessionProtocolValidation';

export const TUNING_UI_CURRENT = Object.freeze({
  acceleration_world_units_per_second_squared: 240,
  normal_top_speed_world_units_per_second: 950,
});
export const TUNING_UI_DEFAULTS = Object.freeze({
  acceleration_world_units_per_second_squared: 120,
  normal_top_speed_world_units_per_second: 850,
});
export const TUNING_UI_DRAFT = Object.freeze({
  acceleration_world_units_per_second_squared: 480,
  normal_top_speed_world_units_per_second: 1250,
});
export const TUNING_UI_PEER = Object.freeze({
  acceleration_world_units_per_second_squared: 360,
  normal_top_speed_world_units_per_second: 1100,
});
export const TUNING_UI_REVISION = 4;

/** Distinct authored/current/draft pairs prevent hardcoded defaults from passing controls tests. */
export function movementTuningConnection(
  sendCommand: SimulationCommandSender,
  overrides: Partial<SimulationConnection> = {},
): SimulationConnection {
  const session = cameraSessionIdentity();
  const document = snapshotDocument();
  document.data.match.movement = {
    ...document.data.match.movement,
    current: { ...TUNING_UI_CURRENT },
    defaults: { ...TUNING_UI_DEFAULTS },
    revision: TUNING_UI_REVISION,
    effective_tick: document.data.tick_sequence,
  };
  const snapshot = validateSessionSnapshotMessage(document, {
    messageSequence: 1,
    requestId: document.meta.request_id,
    tickSequence: null,
    terrain: session.terrain,
  });
  return {
    ...initialSimulationConnectionState,
    session,
    snapshot,
    match: snapshot.data.match,
    entities: snapshot.data.entities,
    ownEntityId: session.firstEntityId,
    status: 'connected',
    sendCommand,
    ...overrides,
  };
}

/** A new coherent authoritative pair/revision without inventing an individual request outcome. */
export function withMovement(
  connection: SimulationConnection,
  changes: Partial<SessionMovementState>,
): SimulationConnection {
  if (connection.match === null || connection.snapshot === null)
    throw new Error('TEST.TUNING_MATCH_MISSING');
  const match = {
    ...connection.match,
    movement: { ...connection.match.movement, ...changes },
  };
  return {
    ...connection,
    match,
    snapshot: {
      ...connection.snapshot,
      data: { ...connection.snapshot.data, match },
    },
  };
}

export function pendingTuning(
  command: SessionSetMovementTuningCommand,
): MovementTuningExchangeState {
  return { status: 'pending', request: command.payload };
}

export function resolvedTuning(
  command: SessionSetMovementTuningCommand,
  status: SessionTuningResult['status'] = 'applied',
  revision = TUNING_UI_REVISION + 1,
): MovementTuningExchangeState {
  const admission =
    status === 'rate_limited' ||
    status === 'mailbox_full' ||
    status === 'mailbox_evicted';
  return {
    status: 'resolved',
    request: command.payload,
    result: {
      tuning_request_id: command.payload.tuning_request_id,
      status,
      decision_tick: admission ? null : 12904,
      revision: admission
        ? null
        : status === 'revision_exhausted'
          ? Number.MAX_SAFE_INTEGER
          : revision,
      retry_after_milliseconds: status === 'rate_limited' ? 500 : null,
    },
  };
}
