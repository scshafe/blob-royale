import type {
  SessionSetMovementTuningCommand,
  SessionTuningResult,
} from '../simulationProtocolTypes';
import { snapshotDocument } from './sessionFrames';

/** A complete absolute request, distinct from the authored 400/600 defaults. */
export function tuningCommand(
  tuningRequestId = 1,
  expectedRevision = 0,
): SessionSetMovementTuningCommand {
  return {
    kind: 'set_movement_tuning',
    payload: {
      tuning_request_id: tuningRequestId,
      expected_revision: expectedRevision,
      acceleration_world_units_per_second_squared: 500,
      normal_top_speed_world_units_per_second: 700,
    },
  };
}

/** A result-bearing ordinary snapshot; each status has its truthful nullable field shape. */
export function tuningSnapshotDocument(
  status: SessionTuningResult['status'] = 'applied',
  tuningRequestId = 1,
  messageSequence = 2,
) {
  const document = snapshotDocument(messageSequence);
  document.data.tick_sequence += messageSequence - 2;
  const admission =
    status === 'rate_limited' ||
    status === 'mailbox_full' ||
    status === 'mailbox_evicted';
  const revision = admission
    ? null
    : status === 'revision_exhausted'
      ? Number.MAX_SAFE_INTEGER
      : status === 'not_seated' || status === 'stale_revision'
        ? 0
        : 1;
  document.data.match.movement.revision = revision ?? 0;
  if (revision !== null && revision > 0) {
    document.data.match.movement.effective_tick = document.data.tick_sequence;
    document.data.match.movement.current = {
      acceleration_world_units_per_second_squared: 500,
      normal_top_speed_world_units_per_second: 700,
    };
  }
  const result: SessionTuningResult = {
    tuning_request_id: tuningRequestId,
    status,
    decision_tick: admission ? null : document.data.tick_sequence,
    revision,
    retry_after_milliseconds: status === 'rate_limited' ? 500 : null,
  };
  return { ...document, data: { ...document.data, tuning_result: result } };
}

export const TUNING_RESULT_STATUSES = [
  'applied',
  'superseded',
  'stale_revision',
  'not_seated',
  'revision_exhausted',
  'rate_limited',
  'mailbox_full',
  'mailbox_evicted',
] as const;
