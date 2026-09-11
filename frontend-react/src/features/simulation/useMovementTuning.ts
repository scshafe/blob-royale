import { useEffect, useRef, useState } from 'react';

import { SimulationApiError } from './SimulationApiError';
import type {
  MovementTuningExchangeState,
  SessionMovementState,
  SessionMovementTuning,
  SessionSetMovementTuningCommand,
  SessionTuningResult,
} from './simulationProtocolTypes';
import type {
  SimulationConnection,
  SimulationSessionIdentity,
} from './useSimulationConnection';

interface MovementTuningDraft {
  readonly acceleration: string;
  readonly normalTopSpeed: string;
  readonly baseRevision: number;
}

interface MovementTuningEditorState {
  readonly lobbyId: number | null;
  readonly session: SimulationSessionIdentity | null;
  readonly draft: MovementTuningDraft | null;
  readonly reviewRequired: boolean;
  readonly reviewedUnknown: SessionSetMovementTuningCommand['payload'] | null;
  readonly handledResult: SessionTuningResult | null;
  readonly resultSession: SimulationSessionIdentity | null;
  readonly releasedRateResult: SessionTuningResult | null;
  readonly rateDeadline: number | null;
  readonly idExhausted: boolean;
  readonly submissionUnavailable: boolean;
}

export interface MovementTuningControls {
  readonly visible: boolean;
  readonly authoritative: SessionMovementState | null;
  readonly acceleration: string;
  readonly normalTopSpeed: string;
  readonly dirty: boolean;
  readonly canEdit: boolean;
  readonly needsReview: boolean;
  readonly canReview: boolean;
  readonly applyDisabledReason: string | null;
  readonly resetDisabledReason: string | null;
  readonly validationMessage: string | null;
  readonly localSubmissionMessage: string | null;
  readonly outcome: {
    readonly kind: 'idle' | 'pending' | 'applied' | 'rejected' | 'unknown';
    readonly message: string;
  };
  readonly editAcceleration: (value: string) => void;
  readonly editNormalTopSpeed: (value: string) => void;
  readonly reviewCurrent: () => void;
  readonly apply: () => void;
  readonly reset: () => void;
}

export interface MovementTuningOptions {
  readonly lobbyId: number | null;
  readonly connection: SimulationConnection;
}

/** The production allocation boundary: positive safe ids, no wrap or rounded successor. */
export function nextMovementTuningRequestId(previous: number): number | null {
  if (!Number.isSafeInteger(previous) || previous < 0) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_INVARIANT_VIOLATION',
      'Movement tuning request counter is invalid.',
      { context: { previous } },
    );
  }
  return previous === Number.MAX_SAFE_INTEGER ? null : previous + 1;
}

function initialEditor(
  lobbyId: number | null,
  session: SimulationSessionIdentity | null,
): MovementTuningEditorState {
  return {
    lobbyId,
    session,
    draft: null,
    reviewRequired: false,
    reviewedUnknown: null,
    handledResult: null,
    resultSession: null,
    releasedRateResult: null,
    rateDeadline: null,
    idExhausted: false,
    submissionUnavailable: false,
  };
}

function draftFrom(
  current: SessionMovementTuning,
  revision: number,
): MovementTuningDraft {
  return {
    acceleration: String(current.acceleration_world_units_per_second_squared),
    normalTopSpeed: String(current.normal_top_speed_world_units_per_second),
    baseRevision: revision,
  };
}

function parsedDraft(
  draft: MovementTuningDraft,
  movement: SessionMovementState,
): SessionMovementTuning | null {
  if (draft.acceleration.trim() === '' || draft.normalTopSpeed.trim() === '')
    return null;
  const acceleration = Number(draft.acceleration);
  const speed = Number(draft.normalTopSpeed);
  const accelerationLimits =
    movement.limits.acceleration_world_units_per_second_squared;
  const speedLimits = movement.limits.normal_top_speed_world_units_per_second;
  if (
    !Number.isFinite(acceleration) ||
    !Number.isFinite(speed) ||
    acceleration < accelerationLimits.minimum ||
    acceleration > accelerationLimits.maximum ||
    speed < speedLimits.minimum ||
    speed > speedLimits.maximum
  )
    return null;
  return {
    acceleration_world_units_per_second_squared: acceleration,
    normal_top_speed_world_units_per_second: speed,
  };
}

function samePair(
  left: SessionMovementTuning,
  right: SessionMovementTuning,
): boolean {
  return (
    left.acceleration_world_units_per_second_squared ===
      right.acceleration_world_units_per_second_squared &&
    left.normal_top_speed_world_units_per_second ===
      right.normal_top_speed_world_units_per_second
  );
}

function describeOutcome(
  exchange: MovementTuningExchangeState,
  unavailable: boolean,
): MovementTuningControls['outcome'] {
  switch (exchange.status) {
    case 'idle':
      return {
        kind: 'idle',
        message: unavailable
          ? 'Submission unavailable. No server outcome has been confirmed.'
          : 'Edits stay local until Apply. Reset submits the authored defaults.',
      };
    case 'pending':
      return {
        kind: 'pending',
        message: `Request ${exchange.request.tuning_request_id} pending server confirmation.`,
      };
    case 'unknown':
      return {
        kind: 'unknown',
        message: `Request ${exchange.request.tuning_request_id} has an unknown outcome after interruption. Review the current room values before another submission; this does not resolve the earlier request.`,
      };
    case 'resolved': {
      const result = exchange.result;
      if (result.status === 'applied')
        return {
          kind: 'applied',
          message: `Request ${result.tuning_request_id} applied at revision ${result.revision}, tick ${result.decision_tick}. Current room values are shown separately.`,
        };
      const reasons = {
        superseded: 'another eligible request won the same tick',
        stale_revision: 'the room revision changed',
        not_seated: 'the sender no longer held a seat',
        revision_exhausted: 'the room revision is exhausted',
        rate_limited: `the minimum interval has not elapsed; retry after ${result.retry_after_milliseconds} ms`,
        mailbox_full: 'the command mailbox was full',
        mailbox_evicted: 'the command was evicted before commit',
      };
      return {
        kind: 'rejected',
        message: `Request ${result.tuning_request_id} not applied: ${reasons[result.status]}. No automatic retry.`,
      };
    }
  }
}

/**
 * @canonical movement_tuning_controls -- local drafts and explicit submissions through sendCommand.
 * Mount above conditional panel/body rendering. Immutable welcome identity scopes allocation;
 * room changes discard local edits, while reconnects retain drafts for explicit review. API state
 * alone supplies outcomes. Timers only release local admission guards and never send commands.
 */
export function useMovementTuning({
  lobbyId,
  connection,
}: MovementTuningOptions): MovementTuningControls {
  const session =
    connection.session?.lobbyId === lobbyId ? connection.session : null;
  const movement =
    session === null ? null : (connection.match?.movement ?? null);
  const [state, setState] = useState(() => initialEditor(lobbyId, session));
  // Navigation renders before the connection effect disposes the previous room. An absent
  // session is only a same-room interruption when this editor previously observed its welcome.
  const exchange: MovementTuningExchangeState =
    lobbyId !== null &&
    (connection.session !== null
      ? connection.session.lobbyId === lobbyId
      : state.lobbyId === lobbyId && state.session?.lobbyId === lobbyId)
      ? connection.movementTuning
      : { status: 'idle' };
  // Refs are read only by event handlers/effects. A rapid second click cannot use a stale render's
  // deadline or id, and discarded renders cannot consume ids.
  const allocation = useRef<{
    session: SimulationSessionIdentity | null;
    lastId: number;
  }>({ session: null, lastId: 0 });
  const admission = useRef<{
    session: SimulationSessionIdentity | null;
    deadline: number;
  }>({ session: null, deadline: 0 });
  let editor = state;
  if (state.lobbyId !== lobbyId) {
    editor = initialEditor(lobbyId, session);
    setState(editor);
  } else if (session !== null && state.session !== session) {
    editor = {
      ...state,
      session,
      rateDeadline: null,
      idExhausted: false,
      reviewRequired: state.draft !== null,
      reviewedUnknown: null,
      submissionUnavailable: false,
    };
    setState(editor);
  } else if (
    session !== null &&
    exchange.status === 'resolved' &&
    editor.handledResult !== exchange.result
  ) {
    const submittedDraft =
      movement === null || editor.draft === null
        ? null
        : parsedDraft(editor.draft, movement);
    const appliedOwnDraft =
      exchange.result.status === 'applied' &&
      submittedDraft !== null &&
      editor.draft?.baseRevision === exchange.request.expected_revision &&
      samePair(submittedDraft, exchange.request);
    editor = {
      ...editor,
      handledResult: exchange.result,
      resultSession: session,
      draft: appliedOwnDraft ? null : editor.draft,
      reviewRequired: appliedOwnDraft ? false : editor.reviewRequired,
      submissionUnavailable: false,
    };
    setState(editor);
  }

  const result = exchange.status === 'resolved' ? exchange.result : null;
  const rateResult =
    result?.status === 'rate_limited' && editor.resultSession === session
      ? result
      : null;
  useEffect(() => {
    if (
      session === null ||
      rateResult === null ||
      rateResult.retry_after_milliseconds === null
    )
      return;
    const deadline = performance.now() + rateResult.retry_after_milliseconds;
    admission.current = {
      session,
      deadline: Math.max(
        admission.current.session === session ? admission.current.deadline : 0,
        deadline,
      ),
    };
    const effectiveDeadline = admission.current.deadline;
    const timer = setTimeout(
      () => {
        setState((current) =>
          current.session === session
            ? { ...current, releasedRateResult: rateResult }
            : current,
        );
      },
      Math.max(0, effectiveDeadline - performance.now()),
    );
    return () => clearTimeout(timer);
  }, [rateResult, session]);

  useEffect(() => {
    if (editor.rateDeadline === null) return;
    const deadline = editor.rateDeadline;
    const timer = setTimeout(
      () => {
        setState((current) =>
          current.rateDeadline === deadline
            ? { ...current, rateDeadline: null }
            : current,
        );
      },
      Math.max(0, deadline - performance.now()),
    );
    return () => clearTimeout(timer);
  }, [editor.rateDeadline]);

  const pending = exchange.status === 'pending';
  const supported =
    session?.acceptedCommandKinds.includes('set_movement_tuning') === true;
  const seated =
    session !== null &&
    connection.match?.seats.some(
      (seat) => seat.controller_id === session.controllerId,
    ) === true;
  const ready =
    connection.status === 'connected' &&
    session !== null &&
    movement !== null &&
    supported &&
    seated;
  const draft =
    editor.draft ??
    (movement === null ? null : draftFrom(movement.current, movement.revision));
  const pair =
    draft === null || movement === null ? null : parsedDraft(draft, movement);
  const unknownReview =
    exchange.status === 'unknown' &&
    editor.reviewedUnknown !== exchange.request;
  const dirtyReview =
    editor.draft !== null &&
    (editor.reviewRequired || editor.draft.baseRevision !== movement?.revision);
  const needsReview = unknownReview || dirtyReview;
  const unavailableReason = !supported
    ? 'This session does not advertise live tuning.'
    : connection.status !== 'connected' || movement === null
      ? 'Waiting for a connected room snapshot.'
      : !seated
        ? 'A seat in this room is required.'
        : pending
          ? 'Wait for the pending request outcome.'
          : editor.idExhausted
            ? 'Request ids are exhausted for this session.'
            : movement.revision === Number.MAX_SAFE_INTEGER
              ? 'This room revision is exhausted.'
              : editor.rateDeadline !== null ||
                  (rateResult !== null &&
                    editor.releasedRateResult !== rateResult)
                ? 'Waiting for the minimum tuning interval.'
                : null;
  const validationMessage =
    pair === null && draft !== null
      ? 'Enter finite values inside both published limits.'
      : null;
  const applyDisabledReason =
    unavailableReason ??
    (needsReview
      ? 'Review the current room values before applying this draft.'
      : validationMessage);
  const resetDisabledReason =
    unavailableReason ??
    (unknownReview ? 'Review the current room values before resetting.' : null);

  const edit = (
    field: 'acceleration' | 'normalTopSpeed',
    value: string,
  ): void => {
    if (!ready || pending || movement === null) return;
    setState((current) => {
      const edited = {
        ...(current.draft ?? draftFrom(movement.current, movement.revision)),
        [field]: value,
      };
      const clean =
        edited.baseRevision === movement.revision &&
        edited.acceleration ===
          String(
            movement.current.acceleration_world_units_per_second_squared,
          ) &&
        edited.normalTopSpeed ===
          String(movement.current.normal_top_speed_world_units_per_second);
      return {
        ...current,
        draft: clean ? null : edited,
        submissionUnavailable: false,
      };
    });
  };

  const submit = (reset: boolean): void => {
    if (
      !ready ||
      movement === null ||
      session === null ||
      (reset ? resetDisabledReason : applyDisabledReason) !== null ||
      (pair === null && !reset)
    )
      return;
    const now = performance.now();
    if (
      admission.current.session === session &&
      now < admission.current.deadline
    )
      return;
    const previous =
      allocation.current.session === session ? allocation.current.lastId : 0;
    const id = nextMovementTuningRequestId(previous);
    if (id === null) {
      setState((current) => ({ ...current, idExhausted: true }));
      return;
    }
    allocation.current = { session, lastId: id };
    const deadline = now + session.movementTuningMinimumIntervalMilliseconds;
    admission.current = { session, deadline };
    const selectedPair = reset ? movement.defaults : pair;
    if (selectedPair === null) return;
    const payload = {
      ...selectedPair,
      tuning_request_id: id,
      expected_revision: reset
        ? movement.revision
        : (draft?.baseRevision ?? movement.revision),
    };
    // Reserve the local rate guard before entering the synchronous transport/callback boundary.
    setState((current) => ({
      ...current,
      rateDeadline: deadline,
      draft: reset
        ? draftFrom(movement.defaults, movement.revision)
        : current.draft,
      reviewRequired: reset ? false : current.reviewRequired,
      submissionUnavailable: false,
      idExhausted: id === Number.MAX_SAFE_INTEGER,
    }));
    const sent = connection.sendCommand({
      kind: 'set_movement_tuning',
      payload,
    });
    if (!sent)
      setState((current) => ({ ...current, submissionUnavailable: true }));
  };

  return {
    visible: supported || exchange.status !== 'idle',
    authoritative: movement,
    acceleration: draft?.acceleration ?? '',
    normalTopSpeed: draft?.normalTopSpeed ?? '',
    dirty: editor.draft !== null,
    canEdit: ready && !pending,
    needsReview,
    canReview: ready && !pending && needsReview,
    applyDisabledReason,
    resetDisabledReason,
    validationMessage,
    localSubmissionMessage: editor.submissionUnavailable
      ? 'Submission unavailable. This is not a server rejection; any earlier request outcome remains unchanged.'
      : null,
    outcome: describeOutcome(exchange, editor.submissionUnavailable),
    editAcceleration: (value) => edit('acceleration', value),
    editNormalTopSpeed: (value) => edit('normalTopSpeed', value),
    reviewCurrent: () => {
      if (!ready || pending || movement === null) return;
      setState((current) => ({
        ...current,
        reviewRequired: false,
        draft:
          current.draft === null
            ? null
            : { ...current.draft, baseRevision: movement.revision },
        reviewedUnknown:
          exchange.status === 'unknown'
            ? exchange.request
            : current.reviewedUnknown,
      }));
    },
    apply: () => submit(false),
    reset: () => submit(true),
  };
}
