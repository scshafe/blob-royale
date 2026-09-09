import { StrictMode, type PropsWithChildren } from 'react';
import { act, renderHook } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import type {
  SimulationApiBoundary,
  SimulationDisconnection,
  SimulationSessionCallbacks,
} from './SimulationApi';
import type { SimulationApiFactory } from './useSimulationConnection';
import { useSimulationConnection } from './useSimulationConnection';
import { configurationResponseExample } from './fixtures/protocolV1Examples';
import { snapshotDocument, welcomeDocument } from './fixtures/sessionFrames';
import { RECONNECT_BACKOFF_MILLISECONDS } from './simulationConstants';
import type { SessionCommand } from './simulationProtocolTypes';
import {
  validateSessionSnapshotMessage,
  validateSessionWelcomeMessage,
} from './sessionProtocolValidation';
import { validateSimulationConfigurationResponse } from './simulationProtocolValidation';

const configuration = validateSimulationConfigurationResponse(
  structuredClone(configurationResponseExample),
  configurationResponseExample.meta.request_id,
).data;

const retryableDisconnection: SimulationDisconnection = Object.freeze({
  code: 1013,
  error: null,
  reason: 'slow_consumer',
  retryable: true,
  wasClean: false,
});

const thrustCommand: SessionCommand = Object.freeze({
  kind: 'set_thrust',
  payload: Object.freeze({ x: 1, y: 0 }),
});

class FakeSimulationApi implements SimulationApiBoundary {
  callbacks: SimulationSessionCallbacks | null = null;
  readonly dispose = vi.fn();
  readonly loadConfiguration = vi.fn((signal: AbortSignal) => {
    void signal;
    return Promise.resolve(configuration);
  });
  readonly openSession = vi.fn(
    (
      receivedConfiguration: typeof configuration,
      callbacks: SimulationSessionCallbacks,
    ) => {
      void receivedConfiguration;
      this.callbacks = callbacks;
    },
  );
  readonly sendCommand = vi.fn((command: SessionCommand) => {
    void command;
    return true;
  });
}

function createApiFactory(apis: FakeSimulationApi[]): SimulationApiFactory {
  return vi.fn(() => {
    const api = new FakeSimulationApi();
    apis.push(api);
    return api;
  });
}

function createValidatedWelcome() {
  return validateSessionWelcomeMessage(welcomeDocument(), null);
}

function createValidatedSnapshot(messageSequence = 2) {
  const document = snapshotDocument(messageSequence);
  return validateSessionSnapshotMessage(document, {
    messageSequence: messageSequence - 1,
    requestId: document.meta.request_id,
    tickSequence: null,
  });
}

function flushPromises(): Promise<void> {
  return act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
}

async function advanceRetry(delay: number): Promise<void> {
  await act(async () => {
    await vi.advanceTimersByTimeAsync(delay);
  });
  await flushPromises();
}

afterEach(() => {
  vi.useRealTimers();
});

describe('useSimulationConnection', () => {
  it('survives StrictMode cleanup without reusing a disposed API', async () => {
    const apis: FakeSimulationApi[] = [];
    const apiFactory = createApiFactory(apis);
    const strictModeWrapper = ({ children }: PropsWithChildren) => (
      <StrictMode>{children}</StrictMode>
    );

    const firstMount = renderHook(() => useSimulationConnection(apiFactory), {
      wrapper: strictModeWrapper,
    });
    await flushPromises();
    firstMount.unmount();

    const secondMount = renderHook(() => useSimulationConnection(apiFactory), {
      wrapper: strictModeWrapper,
    });
    await flushPromises();

    expect(apiFactory).toHaveBeenCalledTimes(2);
    expect(apis[0]?.dispose).toHaveBeenCalledOnce();
    expect(apis[1]?.openSession).toHaveBeenCalledOnce();

    act(() => {
      apis[1]?.callbacks?.onConnected();
    });
    expect(secondMount.result.current.status).toBe('awaiting_match');

    act(() => {
      apis[1]?.callbacks?.onWelcome(createValidatedWelcome());
    });
    expect(secondMount.result.current.status).toBe('connected');

    secondMount.unmount();
    expect(apis[1]?.dispose).toHaveBeenCalledOnce();
  });

  it('treats an open socket with no frames as waiting for the next match', async () => {
    const apis: FakeSimulationApi[] = [];
    const apiFactory = createApiFactory(apis);
    const { result } = renderHook(() => useSimulationConnection(apiFactory));
    await flushPromises();

    act(() => {
      apis[0]?.callbacks?.onConnected();
    });

    expect(result.current).toMatchObject({
      error: null,
      ownEntityId: null,
      session: null,
      snapshot: null,
      status: 'awaiting_match',
    });
    expect(result.current.entities).toHaveLength(0);
  });

  it('exposes the welcome identity and resolves the own entity by controller id', async () => {
    const apis: FakeSimulationApi[] = [];
    const apiFactory = createApiFactory(apis);
    const { result } = renderHook(() => useSimulationConnection(apiFactory));
    await flushPromises();

    act(() => {
      apis[0]?.callbacks?.onConnected();
      apis[0]?.callbacks?.onWelcome(createValidatedWelcome());
    });

    expect(result.current.session).toEqual({
      acceptedCommandKinds: [
        'clear_seat',
        'seat_npc',
        'set_seat_count',
        'set_thrust',
        'start_match',
      ],
      controllerId: 3,
      displayName: 'Cole Shaffer',
      firstEntityId: 7,
      map: 'arena-960x640',
      mode: 'royale',
    });
    expect(result.current.ownEntityId).toBeNull();

    act(() => {
      apis[0]?.callbacks?.onSnapshot(createValidatedSnapshot());
    });

    expect(result.current.ownEntityId).toBe(7);
    expect(result.current.match?.phase).toBe('running');
    expect(result.current.entities).toHaveLength(4);
  });

  it('stores complete validated snapshots through the reducer', async () => {
    const apis: FakeSimulationApi[] = [];
    const apiFactory = createApiFactory(apis);
    const { result } = renderHook(() => useSimulationConnection(apiFactory));
    await flushPromises();
    const snapshot = createValidatedSnapshot();

    act(() => {
      apis[0]?.callbacks?.onWelcome(createValidatedWelcome());
      apis[0]?.callbacks?.onSnapshot(snapshot);
    });

    expect(result.current.snapshot).toBe(snapshot);
    expect(result.current.entities).toBe(snapshot.data.entities);
    expect(result.current.match).toBe(snapshot.data.match);
    expect(result.current.status).toBe('connected');
    expect(result.current.reconnectAttempt).toBe(0);
  });

  it('sends commands only while a session is open', async () => {
    const apis: FakeSimulationApi[] = [];
    const apiFactory = createApiFactory(apis);
    const { result } = renderHook(() => useSimulationConnection(apiFactory));
    await flushPromises();

    expect(result.current.sendCommand(thrustCommand)).toBe(false);

    act(() => {
      apis[0]?.callbacks?.onConnected();
    });
    expect(result.current.sendCommand(thrustCommand)).toBe(true);
    expect(apis[0]?.sendCommand).toHaveBeenCalledWith(thrustCommand);

    act(() => {
      apis[0]?.callbacks?.onDisconnected(retryableDisconnection);
    });
    expect(result.current.sendCommand(thrustCommand)).toBe(false);
    expect(apis[0]?.sendCommand).toHaveBeenCalledTimes(1);
  });

  it('uses a fresh API, refetches config, and clears stale state on every retry', async () => {
    vi.useFakeTimers();
    const apis: FakeSimulationApi[] = [];
    const apiFactory = createApiFactory(apis);
    const { result } = renderHook(() => useSimulationConnection(apiFactory));
    await flushPromises();

    act(() => {
      apis[0]?.callbacks?.onWelcome(createValidatedWelcome());
      apis[0]?.callbacks?.onSnapshot(createValidatedSnapshot());
      apis[0]?.callbacks?.onDisconnected(retryableDisconnection);
    });

    expect(result.current).toMatchObject({
      configuration: null,
      match: null,
      ownEntityId: null,
      reconnectAttempt: 1,
      session: null,
      snapshot: null,
      status: 'retrying',
    });
    expect(result.current.entities).toHaveLength(0);
    expect(apis[0]?.dispose).toHaveBeenCalledOnce();

    await advanceRetry(RECONNECT_BACKOFF_MILLISECONDS[0] ?? 0);

    expect(apiFactory).toHaveBeenCalledTimes(2);
    expect(apis[1]?.loadConfiguration).toHaveBeenCalledOnce();
    expect(apis[1]?.openSession).toHaveBeenCalledOnce();
    expect(apis[0]).not.toBe(apis[1]);
  });

  it('uses the exact bounded backoff sequence and then fails visibly', async () => {
    vi.useFakeTimers();
    const apis: FakeSimulationApi[] = [];
    const apiFactory = createApiFactory(apis);
    const { result } = renderHook(() => useSimulationConnection(apiFactory));
    await flushPromises();

    expect(RECONNECT_BACKOFF_MILLISECONDS).toEqual([
      1_000, 2_000, 4_000, 8_000, 16_000, 16_000,
    ]);

    for (const [
      retryIndex,
      delay,
    ] of RECONNECT_BACKOFF_MILLISECONDS.entries()) {
      act(() => {
        apis[retryIndex]?.callbacks?.onDisconnected(retryableDisconnection);
      });
      expect(result.current.status).toBe('retrying');
      expect(result.current.reconnectAttempt).toBe(retryIndex + 1);
      expect(apis[retryIndex]?.dispose).toHaveBeenCalledOnce();

      await advanceRetry(delay - 1);
      expect(apiFactory).toHaveBeenCalledTimes(retryIndex + 1);
      await advanceRetry(1);
      expect(apiFactory).toHaveBeenCalledTimes(retryIndex + 2);
      expect(apis[retryIndex + 1]?.loadConfiguration).toHaveBeenCalledOnce();
      expect(apis[retryIndex + 1]?.openSession).toHaveBeenCalledOnce();
    }

    act(() => {
      apis[RECONNECT_BACKOFF_MILLISECONDS.length]?.callbacks?.onDisconnected(
        retryableDisconnection,
      );
    });

    expect(result.current.status).toBe('failed');
    expect(result.current.error?.code).toBe('SIMULATION.RECONNECT_EXHAUSTED');
    expect(
      apis[RECONNECT_BACKOFF_MILLISECONDS.length]?.dispose,
    ).toHaveBeenCalledOnce();
    expect(vi.getTimerCount()).toBe(0);
  });

  it('resets the retry budget only after the first valid snapshot', async () => {
    vi.useFakeTimers();
    const apis: FakeSimulationApi[] = [];
    const apiFactory = createApiFactory(apis);
    const { result } = renderHook(() => useSimulationConnection(apiFactory));
    await flushPromises();

    act(() => {
      apis[0]?.callbacks?.onDisconnected(retryableDisconnection);
    });
    await advanceRetry(1_000);

    act(() => {
      apis[1]?.callbacks?.onConnected();
      apis[1]?.callbacks?.onWelcome(createValidatedWelcome());
      apis[1]?.callbacks?.onDisconnected(retryableDisconnection);
    });
    expect(result.current.reconnectAttempt).toBe(2);
    await advanceRetry(1_999);
    expect(apiFactory).toHaveBeenCalledTimes(2);
    await advanceRetry(1);
    expect(apiFactory).toHaveBeenCalledTimes(3);

    act(() => {
      apis[2]?.callbacks?.onWelcome(createValidatedWelcome());
      apis[2]?.callbacks?.onSnapshot(createValidatedSnapshot());
    });
    expect(result.current.reconnectAttempt).toBe(0);

    act(() => {
      apis[2]?.callbacks?.onDisconnected(retryableDisconnection);
    });
    expect(result.current.reconnectAttempt).toBe(1);
    await advanceRetry(999);
    expect(apiFactory).toHaveBeenCalledTimes(3);
    await advanceRetry(1);
    expect(apiFactory).toHaveBeenCalledTimes(4);
  });

  it('ignores stale callbacks and disposes each attempt at most once', async () => {
    vi.useFakeTimers();
    const apis: FakeSimulationApi[] = [];
    const apiFactory = createApiFactory(apis);
    const { result, unmount } = renderHook(() =>
      useSimulationConnection(apiFactory),
    );
    await flushPromises();
    const firstCallbacks = apis[0]?.callbacks;

    act(() => {
      firstCallbacks?.onDisconnected(retryableDisconnection);
      firstCallbacks?.onWelcome(createValidatedWelcome());
      firstCallbacks?.onSnapshot(createValidatedSnapshot());
    });

    expect(result.current.status).toBe('retrying');
    expect(result.current.snapshot).toBeNull();
    expect(result.current.session).toBeNull();
    expect(apis[0]?.dispose).toHaveBeenCalledOnce();

    unmount();
    expect(vi.getTimerCount()).toBe(0);
    expect(apis[0]?.dispose).toHaveBeenCalledOnce();

    await vi.advanceTimersByTimeAsync(16_000);
    expect(apiFactory).toHaveBeenCalledOnce();
  });
});
