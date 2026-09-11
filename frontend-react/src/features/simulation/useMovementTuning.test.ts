import { act, renderHook } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import {
  nextMovementTuningRequestId,
  useMovementTuning,
} from './useMovementTuning';
import type {
  SessionCommand,
  SessionMatchPhase,
} from './simulationProtocolTypes';
import type { SimulationConnection } from './useSimulationConnection';
import {
  movementTuningConnection,
  pendingTuning,
  resolvedTuning,
  TUNING_UI_CURRENT,
  TUNING_UI_DEFAULTS,
  TUNING_UI_DRAFT,
  TUNING_UI_PEER,
  TUNING_UI_REVISION,
  withMovement,
} from './fixtures/movementTuningControlsFrames';

function createSender() {
  return vi.fn((command: SessionCommand) => {
    void command;
    return true;
  });
}
function requireCommand(sender: ReturnType<typeof createSender>) {
  const command = sender.mock.lastCall?.[0];
  if (command?.kind !== 'set_movement_tuning')
    throw new Error('TEST.TUNING_COMMAND_MISSING');
  return command;
}
async function advance(milliseconds: number) {
  await act(async () => {
    await vi.advanceTimersByTimeAsync(milliseconds);
  });
}
function renderControls(connection: SimulationConnection, lobbyId = 1) {
  return renderHook(
    (options: { connection: SimulationConnection; lobbyId: number }) =>
      useMovementTuning(options),
    { initialProps: { connection, lobbyId } },
  );
}

afterEach(() => {
  vi.useRealTimers();
  vi.restoreAllMocks();
});

describe('useMovementTuning', () => {
  it('coalesces local edits into one atomic request without inferring application from send or shared state', () => {
    const sender = createSender();
    const connection = movementTuningConnection(sender);
    const hook = renderControls(connection);
    act(() => {
      hook.result.current.editAcceleration('480');
      hook.result.current.editNormalTopSpeed('1250');
    });
    expect(sender).not.toHaveBeenCalled();
    expect(hook.result.current.authoritative?.current).toEqual(
      TUNING_UI_CURRENT,
    );
    act(() => hook.result.current.apply());
    const command = requireCommand(sender);
    expect(command.payload).toEqual({
      ...TUNING_UI_DRAFT,
      tuning_request_id: 1,
      expected_revision: TUNING_UI_REVISION,
    });
    expect(hook.result.current.outcome.kind).not.toBe('applied');
    const pending = {
      ...withMovement(connection, { current: TUNING_UI_DRAFT, revision: 5 }),
      movementTuning: pendingTuning(command),
    };
    hook.rerender({ connection: pending, lobbyId: 1 });
    expect(hook.result.current.outcome.kind).toBe('pending');
    expect(hook.result.current.canEdit).toBe(false);
    act(() => hook.result.current.editAcceleration('900'));
    expect(hook.result.current.acceleration).toBe('480');
  });

  it('retains draft and base revision across peer commits and requires a new explicit review after each revision', () => {
    const sender = createSender();
    let connection = movementTuningConnection(sender);
    const hook = renderControls(connection);
    act(() => hook.result.current.editAcceleration('480'));
    connection = withMovement(connection, {
      current: TUNING_UI_PEER,
      revision: 5,
    });
    hook.rerender({ connection, lobbyId: 1 });
    expect(hook.result.current.acceleration).toBe('480');
    expect(hook.result.current.normalTopSpeed).toBe('950');
    expect(hook.result.current.needsReview).toBe(true);
    act(() => hook.result.current.apply());
    expect(sender).not.toHaveBeenCalled();
    act(() => hook.result.current.reviewCurrent());
    expect(hook.result.current.needsReview).toBe(false);
    expect(sender).not.toHaveBeenCalled();
    connection = withMovement(connection, { revision: 6 });
    hook.rerender({ connection, lobbyId: 1 });
    expect(hook.result.current.needsReview).toBe(true);
    act(() => hook.result.current.reviewCurrent());
    act(() => hook.result.current.apply());
    expect(requireCommand(sender).payload).toEqual({
      acceleration_world_units_per_second_squared: 480,
      normal_top_speed_world_units_per_second: 950,
      tuning_request_id: 1,
      expected_revision: 6,
    });
  });

  it('Reset explicitly replaces a conflicted draft with authored defaults against the latest revision', () => {
    const sender = createSender();
    let connection = movementTuningConnection(sender);
    const hook = renderControls(connection);
    act(() => hook.result.current.editAcceleration('480'));
    connection = withMovement(connection, {
      current: TUNING_UI_PEER,
      revision: 5,
    });
    hook.rerender({ connection, lobbyId: 1 });
    expect(hook.result.current.needsReview).toBe(true);
    expect(hook.result.current.resetDisabledReason).toBeNull();
    act(() => hook.result.current.reset());
    expect(requireCommand(sender).payload).toEqual({
      ...TUNING_UI_DEFAULTS,
      expected_revision: 5,
      tuning_request_id: 1,
    });
    expect(hook.result.current.acceleration).toBe('120');
    expect(hook.result.current.normalTopSpeed).toBe('850');
    expect(hook.result.current.outcome.kind).not.toBe('applied');
  });

  it.each(['', 'NaN', 'Infinity', '-1', '10001'])(
    'blocks invalid acceleration draft %s without clamping or sending',
    (value) => {
      const sender = createSender();
      const hook = renderControls(movementTuningConnection(sender));
      act(() => hook.result.current.editAcceleration(value));
      expect(hook.result.current.acceleration).toBe(value);
      expect(hook.result.current.validationMessage).not.toBeNull();
      act(() => hook.result.current.apply());
      expect(sender).not.toHaveBeenCalled();
    },
  );

  it('accepts zero acceleration and fractional values but refuses zero normal speed', () => {
    const sender = createSender();
    const hook = renderControls(movementTuningConnection(sender));
    act(() => {
      hook.result.current.editAcceleration('0');
      hook.result.current.editNormalTopSpeed('0');
    });
    expect(hook.result.current.validationMessage).not.toBeNull();
    act(() => hook.result.current.editNormalTopSpeed('850.25'));
    expect(hook.result.current.validationMessage).toBeNull();
    act(() => hook.result.current.apply());
    expect(
      requireCommand(sender).payload.normal_top_speed_world_units_per_second,
    ).toBe(850.25);
    expect(
      requireCommand(sender).payload
        .acceleration_world_units_per_second_squared,
    ).toBe(0);
  });

  it.each<SessionMatchPhase>(['lobby', 'countdown', 'running', 'ended'])(
    'allows a seated bodyless controller in %s',
    (phase) => {
      const sender = createSender();
      const base = movementTuningConnection(sender);
      if (base.match === null) throw new Error('TEST.TUNING_MATCH_MISSING');
      const hook = renderControls({
        ...base,
        ownEntityId: null,
        entities: [],
        match: { ...base.match, phase },
      });
      expect(hook.result.current.canEdit).toBe(true);
      act(() => hook.result.current.apply());
      expect(sender).toHaveBeenCalledTimes(1);
    },
  );

  it.each(['unsupported', 'unseated', 'disconnected', 'exhausted'] as const)(
    'disables submissions when %s',
    (reason) => {
      const sender = createSender();
      let connection = movementTuningConnection(sender);
      if (connection.session === null || connection.match === null)
        throw new Error('TEST.TUNING_MATCH_MISSING');
      const session = connection.session;
      const match = connection.match;
      if (reason === 'unsupported')
        connection = {
          ...connection,
          session: {
            ...session,
            acceptedCommandKinds: ['set_thrust'],
          },
        };
      if (reason === 'unseated')
        connection = {
          ...connection,
          match: { ...match, seats: [] },
        };
      if (reason === 'disconnected')
        connection = { ...connection, status: 'retrying' };
      if (reason === 'exhausted')
        connection = withMovement(connection, {
          revision: Number.MAX_SAFE_INTEGER,
        });
      const hook = renderControls(connection);
      act(() => {
        hook.result.current.apply();
        hook.result.current.reset();
      });
      expect(sender).not.toHaveBeenCalled();
      expect(hook.result.current.applyDisabledReason).not.toBeNull();
      expect(hook.result.current.resetDisabledReason).not.toBeNull();
    },
  );

  it('bounds rapid Apply and Reset at 499/500ms and never sends from the timer', async () => {
    vi.useFakeTimers();
    const sender = createSender();
    const connection = movementTuningConnection(sender);
    const hook = renderControls(connection);
    act(() => {
      hook.result.current.apply();
      hook.result.current.reset();
    });
    expect(sender).toHaveBeenCalledTimes(1);
    await advance(499);
    act(() => hook.result.current.apply());
    expect(sender).toHaveBeenCalledTimes(1);
    await advance(1);
    expect(sender).toHaveBeenCalledTimes(1);
    act(() => hook.result.current.reset());
    expect(sender).toHaveBeenCalledTimes(2);
    expect(requireCommand(sender).payload.tuning_request_id).toBe(2);
  });

  it('honors one rate-result deadline without restarting it on snapshots or automatically retrying', async () => {
    vi.useFakeTimers();
    const sender = createSender();
    let connection = movementTuningConnection(sender);
    const hook = renderControls(connection);
    act(() => hook.result.current.apply());
    await advance(400);
    connection = {
      ...connection,
      movementTuning: resolvedTuning(requireCommand(sender), 'rate_limited'),
    };
    hook.rerender({ connection, lobbyId: 1 });
    await advance(100);
    hook.rerender({ connection: { ...connection }, lobbyId: 1 });
    expect(hook.result.current.applyDisabledReason).not.toBeNull();
    await advance(399);
    act(() => hook.result.current.reset());
    expect(sender).toHaveBeenCalledTimes(1);
    await advance(1);
    expect(hook.result.current.applyDisabledReason).toBeNull();
    expect(sender).toHaveBeenCalledTimes(1);
    act(() => hook.result.current.reset());
    expect(requireCommand(sender).payload.tuning_request_id).toBe(2);
  });

  it('uses elapsed time even when the wall clock jumps backwards during the tuning interval', async () => {
    vi.useFakeTimers();
    const sender = createSender();
    const hook = renderControls(movementTuningConnection(sender));
    act(() => hook.result.current.apply());
    vi.setSystemTime(new Date('2000-01-01T00:00:00Z'));
    await advance(500);
    expect(hook.result.current.applyDisabledReason).toBeNull();
    act(() => hook.result.current.apply());
    expect(sender).toHaveBeenCalledTimes(2);
  });

  it('keeps ids across body and panel availability changes but resets for a new welcome with repeated numeric ids', async () => {
    vi.useFakeTimers();
    const sender = createSender();
    let connection = movementTuningConnection(sender);
    const hook = renderControls(connection);
    act(() => hook.result.current.apply());
    const first = requireCommand(sender);
    connection = {
      ...withMovement(connection, { revision: 5 }),
      movementTuning: resolvedTuning(first),
      ownEntityId: null,
    };
    hook.rerender({ connection, lobbyId: 1 });
    await advance(500);
    hook.rerender({
      connection: { ...connection, ownEntityId: 70 },
      lobbyId: 1,
    });
    act(() => hook.result.current.apply());
    expect(requireCommand(sender).payload.tuning_request_id).toBe(2);
    if (connection.session === null)
      throw new Error('TEST.TUNING_SESSION_MISSING');
    connection = { ...connection, session: { ...connection.session } };
    hook.rerender({ connection, lobbyId: 1 });
    expect(hook.result.current.applyDisabledReason).toBeNull();
    act(() => hook.result.current.apply());
    expect(requireCommand(sender).payload.tuning_request_id).toBe(1);
  });

  it('retains unknown across reconnect and matching current values until review, without inventing an outcome or replay', () => {
    const sender = createSender();
    let connection = movementTuningConnection(sender);
    const hook = renderControls(connection);
    act(() => hook.result.current.editAcceleration('480'));
    act(() => hook.result.current.apply());
    const command = requireCommand(sender);
    connection = {
      ...connection,
      session: null,
      status: 'retrying',
      movementTuning: { status: 'unknown', request: command.payload },
    };
    hook.rerender({ connection, lobbyId: 1 });
    const fresh = movementTuningConnection(sender, {
      movementTuning: connection.movementTuning,
    });
    connection = withMovement(fresh, {
      current: {
        acceleration_world_units_per_second_squared:
          command.payload.acceleration_world_units_per_second_squared,
        normal_top_speed_world_units_per_second:
          command.payload.normal_top_speed_world_units_per_second,
      },
      revision: 5,
    });
    hook.rerender({ connection, lobbyId: 1 });
    expect(hook.result.current.outcome.kind).toBe('unknown');
    expect(hook.result.current.needsReview).toBe(true);
    act(() => hook.result.current.reset());
    expect(sender).toHaveBeenCalledTimes(1);
    act(() => hook.result.current.reviewCurrent());
    expect(hook.result.current.outcome.kind).toBe('unknown');
    expect(sender).toHaveBeenCalledTimes(1);
    act(() => hook.result.current.apply());
    expect(requireCommand(sender).payload.tuning_request_id).toBe(1);
    expect(requireCommand(sender).payload.expected_revision).toBe(5);
  });

  it('reports historical applied result separately from a later current pair and preserves newer draft edits', () => {
    const sender = createSender();
    let connection = movementTuningConnection(sender);
    const hook = renderControls(connection);
    act(() => hook.result.current.editAcceleration('480'));
    act(() => hook.result.current.apply());
    const command = requireCommand(sender);
    act(() => hook.result.current.editAcceleration('700'));
    connection = {
      ...withMovement(connection, { current: TUNING_UI_PEER, revision: 6 }),
      movementTuning: resolvedTuning(command, 'applied', 5),
    };
    hook.rerender({ connection, lobbyId: 1 });
    expect(hook.result.current.outcome.kind).toBe('applied');
    expect(hook.result.current.outcome.message).toContain('revision 5');
    expect(hook.result.current.authoritative?.current).toEqual(TUNING_UI_PEER);
    expect(hook.result.current.acceleration).toBe('700');
    expect(hook.result.current.needsReview).toBe(true);
  });

  it.each([
    'superseded',
    'stale_revision',
    'not_seated',
    'revision_exhausted',
    'mailbox_full',
    'mailbox_evicted',
  ] as const)(
    'reports the exact %s refusal without a resend or a fabricated applied state',
    (status) => {
      const sender = createSender();
      let connection = movementTuningConnection(sender);
      const hook = renderControls(connection);
      act(() => hook.result.current.apply());
      const command = requireCommand(sender);
      connection = {
        ...withMovement(connection, {
          revision:
            status === 'revision_exhausted' ? Number.MAX_SAFE_INTEGER : 5,
        }),
        movementTuning: resolvedTuning(command, status),
      };
      hook.rerender({ connection, lobbyId: 1 });
      expect(hook.result.current.outcome.kind).toBe('rejected');
      expect(hook.result.current.outcome.message).toContain('not applied:');
      expect(hook.result.current.outcome.message).toContain(
        'No automatic retry.',
      );
      expect(hook.result.current.authoritative?.current).toEqual(
        TUNING_UI_CURRENT,
      );
      expect(sender).toHaveBeenCalledTimes(1);
    },
  );

  it('does not overwrite synchronous resolved feedback and exposes later local refusal separately', async () => {
    vi.useFakeTimers();
    const sender = createSender();
    let connection = movementTuningConnection(sender);
    const hook = renderControls(connection);
    sender.mockImplementationOnce((command) => {
      if (command.kind !== 'set_movement_tuning')
        throw new Error('TEST.TUNING_COMMAND_MISSING');
      connection = {
        ...withMovement(connection, { revision: 5 }),
        movementTuning: resolvedTuning(command),
      };
      hook.rerender({ connection, lobbyId: 1 });
      return true;
    });
    act(() => hook.result.current.apply());
    expect(hook.result.current.outcome.kind).toBe('applied');
    await advance(500);
    sender.mockReturnValueOnce(false);
    act(() => hook.result.current.apply());
    expect(hook.result.current.outcome.kind).toBe('applied');
    expect(hook.result.current.localSubmissionMessage).toContain(
      'not a server rejection',
    );
  });

  it('keeps two hook instances isolated and prevents an old room timer from changing the new room', async () => {
    vi.useFakeTimers();
    const senderA = createSender();
    const senderB = createSender();
    const connectionA = movementTuningConnection(senderA);
    const baseB = movementTuningConnection(senderB);
    if (baseB.session === null) throw new Error('TEST.TUNING_SESSION_MISSING');
    const connectionB = withMovement(
      { ...baseB, session: { ...baseB.session, lobbyId: 2 } },
      { current: TUNING_UI_PEER },
    );
    const a = renderControls(connectionA);
    const b = renderControls(connectionB, 2);
    act(() => {
      a.result.current.editAcceleration('480');
      a.result.current.apply();
    });
    expect(b.result.current.acceleration).toBe('360');
    expect(b.result.current.applyDisabledReason).toBeNull();
    a.rerender({ connection: connectionB, lobbyId: 2 });
    expect(a.result.current.dirty).toBe(false);
    await advance(250);
    act(() => a.result.current.apply());
    await advance(250);
    expect(a.result.current.applyDisabledReason).not.toBeNull();
    expect(requireCommand(senderB).payload.tuning_request_id).toBe(1);
    expect(senderA).toHaveBeenCalledTimes(1);
  });

  it.each(['resolved', 'unknown'] as const)(
    'immediately suppresses old-room %s before the connection effect clears it',
    (status) => {
      const sender = createSender();
      let connection = movementTuningConnection(sender);
      const hook = renderControls(connection);
      act(() => hook.result.current.apply());
      const command = requireCommand(sender);
      connection = {
        ...withMovement(connection, { revision: 5 }),
        movementTuning:
          status === 'unknown'
            ? { status: 'unknown', request: command.payload }
            : resolvedTuning(command),
      };
      hook.rerender({ connection, lobbyId: 1 });
      expect(hook.result.current.visible).toBe(true);
      hook.rerender({ connection, lobbyId: 2 });
      expect(hook.result.current.visible).toBe(false);
      expect(hook.result.current.outcome.kind).toBe('idle');
      expect(hook.result.current.authoritative).toBeNull();
      hook.rerender({
        connection: { ...connection, session: null },
        lobbyId: 2,
      });
      expect(hook.result.current.visible).toBe(false);
      expect(hook.result.current.outcome.kind).toBe('idle');
    },
  );
});

describe('movement tuning id allocation', () => {
  it('allocates the maximum safe id once and then reports exhaustion without arithmetic overflow', () => {
    expect(nextMovementTuningRequestId(0)).toBe(1);
    const last = nextMovementTuningRequestId(Number.MAX_SAFE_INTEGER - 1);
    expect(last).toBe(Number.MAX_SAFE_INTEGER);
    if (last === null) throw new Error('TEST.TUNING_ID_MISSING');
    expect(nextMovementTuningRequestId(last)).toBeNull();
  });
  it.each([-1, 1.5, Number.POSITIVE_INFINITY, Number.MAX_SAFE_INTEGER + 1])(
    'fails visibly for invalid internal counter %s',
    (value) => {
      expect(() => nextMovementTuningRequestId(value)).toThrow(
        'Movement tuning request counter is invalid.',
      );
    },
  );
});
