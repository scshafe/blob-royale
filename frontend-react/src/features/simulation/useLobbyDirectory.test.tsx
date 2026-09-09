import { act, renderHook } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import type { SimulationApiBoundary } from './SimulationApi';
import { SimulationApiError } from './SimulationApiError';
import { lobbyDirectoryMessageExample } from './fixtures/protocolV2Examples';
import { LOBBY_DIRECTORY_POLL_MILLISECONDS } from './simulationConstants';
import type { SessionLobbyListing } from './simulationProtocolTypes';
import { validateLobbyDirectoryMessage } from './sessionProtocolValidation';
import {
  initialLobbyDirectoryState,
  lobbyDirectoryReducer,
  useLobbyDirectory,
} from './useLobbyDirectory';

const listings: readonly SessionLobbyListing[] = validateLobbyDirectoryMessage(
  structuredClone(lobbyDirectoryMessageExample),
  lobbyDirectoryMessageExample.meta.request_id,
).data.lobbies;

class FakeDirectoryApi implements SimulationApiBoundary {
  readonly dispose = vi.fn();
  readonly fetchLobbies = vi.fn((signal: AbortSignal) => {
    void signal;
    return Promise.resolve(listings);
  });
  readonly loadConfiguration = vi.fn(() =>
    Promise.reject(new Error('TEST.CONFIGURATION_NOT_USED')),
  );
  readonly openSession = vi.fn();
  readonly sendCommand = vi.fn(() => false);
}

/**
 * Created once per test and never inside the render callback: the hook re-creates its reader when
 * the factory's identity changes, so a factory made per render would read, re-render, and read
 * again without end.
 */
function createApiFactory(apis: FakeDirectoryApi[]) {
  return vi.fn(() => {
    const api = new FakeDirectoryApi();
    apis.push(api);
    return api;
  });
}

/**
 * The number of reads, as a number: a failed `toHaveBeenCalledTimes` on this mock would have vitest
 * pretty-print the recorded `AbortSignal` arguments, which are jsdom objects large enough to exhaust
 * the worker's heap before the assertion message is written.
 */
function readCount(api: FakeDirectoryApi): number {
  return api.fetchLobbies.mock.calls.length;
}

function requireApi(apis: FakeDirectoryApi[], index: number): FakeDirectoryApi {
  const api = apis[index];
  if (api === undefined) {
    throw new Error('TEST.API_NOT_CREATED');
  }
  return api;
}

function flushPromises(): Promise<void> {
  return act(async () => {
    await Promise.resolve();
    await Promise.resolve();
  });
}

async function advance(milliseconds: number): Promise<void> {
  await act(async () => {
    await vi.advanceTimersByTimeAsync(milliseconds);
  });
  await flushPromises();
}

function setPageVisibility(state: DocumentVisibilityState): void {
  Object.defineProperty(document, 'visibilityState', {
    configurable: true,
    get: () => state,
  });
}

afterEach(() => {
  Reflect.deleteProperty(document, 'visibilityState');
  vi.useRealTimers();
});

describe('lobbyDirectoryReducer', () => {
  it('keeps the last listings across a failed read and a pause', () => {
    const ready = lobbyDirectoryReducer(initialLobbyDirectoryState, {
      listings,
      type: 'fetch_succeeded',
    });
    expect(ready).toMatchObject({ error: null, status: 'ready' });
    expect(ready.listings).toBe(listings);

    const error = new SimulationApiError(
      'SIMULATION.LOBBY_DIRECTORY_REQUEST_FAILED',
      'The directory is unreachable.',
      { retryable: true },
    );
    const failed = lobbyDirectoryReducer(ready, {
      error,
      type: 'fetch_failed',
    });
    expect(failed).toMatchObject({ error, status: 'failed' });
    expect(failed.listings).toBe(listings);

    const stopped = lobbyDirectoryReducer(failed, { type: 'stopped' });
    expect(stopped.status).toBe('idle');
    expect(stopped.listings).toBe(listings);
    expect(
      lobbyDirectoryReducer(stopped, { type: 'fetch_started' }).status,
    ).toBe('loading');
  });
});

describe('useLobbyDirectory', () => {
  it('reads the directory at once, then once a second, and stops when disabled', async () => {
    vi.useFakeTimers();
    const apis: FakeDirectoryApi[] = [];
    const apiFactory = createApiFactory(apis);
    const { rerender, result } = renderHook(
      ({ enabled }: { readonly enabled: boolean }) =>
        useLobbyDirectory({ enabled }, apiFactory),
      { initialProps: { enabled: true } },
    );
    await flushPromises();

    expect(readCount(requireApi(apis, 0))).toBe(1);
    expect(result.current.status).toBe('ready');
    expect(result.current.listings).toBe(listings);

    // The next read is scheduled after the last one completed, not on an interval.
    await advance(LOBBY_DIRECTORY_POLL_MILLISECONDS - 1);
    expect(readCount(requireApi(apis, 0))).toBe(1);
    await advance(1);
    expect(readCount(requireApi(apis, 0))).toBe(2);

    // Off screen: no timer, no read, the API disposed, the last listings kept for when it returns.
    rerender({ enabled: false });
    await flushPromises();
    expect(result.current.status).toBe('idle');
    expect(result.current.listings).toBe(listings);
    expect(requireApi(apis, 0).dispose).toHaveBeenCalledOnce();
    expect(vi.getTimerCount()).toBe(0);
    await advance(5 * LOBBY_DIRECTORY_POLL_MILLISECONDS);
    expect(readCount(requireApi(apis, 0))).toBe(2);

    // Back on screen, which is what leaving a room does: one read immediately, on a fresh API.
    rerender({ enabled: true });
    await flushPromises();
    expect(apiFactory).toHaveBeenCalledTimes(2);
    expect(readCount(requireApi(apis, 1))).toBe(1);
    expect(result.current.status).toBe('ready');
  });

  it('reports a failed read beside the last listings and keeps reading', async () => {
    vi.useFakeTimers();
    const apis: FakeDirectoryApi[] = [];
    const apiFactory = createApiFactory(apis);
    const { result } = renderHook(() =>
      useLobbyDirectory({ enabled: true }, apiFactory),
    );
    await flushPromises();
    expect(result.current.status).toBe('ready');

    requireApi(apis, 0).fetchLobbies.mockRejectedValueOnce(
      new SimulationApiError(
        'SIMULATION.LOBBY_DIRECTORY_REQUEST_FAILED',
        'The directory is unreachable.',
        { retryable: true },
      ),
    );
    await advance(LOBBY_DIRECTORY_POLL_MILLISECONDS);
    expect(result.current.status).toBe('failed');
    expect(result.current.error?.code).toBe(
      'SIMULATION.LOBBY_DIRECTORY_REQUEST_FAILED',
    );
    expect(result.current.listings).toBe(listings);

    await advance(LOBBY_DIRECTORY_POLL_MILLISECONDS);
    expect(result.current.status).toBe('ready');
    expect(result.current.error).toBeNull();
    expect(readCount(requireApi(apis, 0))).toBe(3);
  });

  it('pauses while the page is hidden and reads again the moment it is shown', async () => {
    vi.useFakeTimers();
    const apis: FakeDirectoryApi[] = [];
    const apiFactory = createApiFactory(apis);
    renderHook(() => useLobbyDirectory({ enabled: true }, apiFactory));
    await flushPromises();
    expect(readCount(requireApi(apis, 0))).toBe(1);

    setPageVisibility('hidden');
    act(() => {
      document.dispatchEvent(new Event('visibilitychange'));
    });
    await advance(3 * LOBBY_DIRECTORY_POLL_MILLISECONDS);
    expect(readCount(requireApi(apis, 0))).toBe(1);

    setPageVisibility('visible');
    act(() => {
      document.dispatchEvent(new Event('visibilitychange'));
    });
    await flushPromises();
    expect(readCount(requireApi(apis, 0))).toBe(2);
  });

  it('aborts a read in flight when it unmounts and reports nothing afterwards', async () => {
    vi.useFakeTimers();
    const apis: FakeDirectoryApi[] = [];
    const apiFactory = createApiFactory(apis);
    const observedSignals: AbortSignal[] = [];
    const { result, unmount } = renderHook(() =>
      useLobbyDirectory({ enabled: true }, apiFactory),
    );
    await flushPromises();
    const api = requireApi(apis, 0);
    // The first read resolved with the default mock; the second, scheduled a second later, is the
    // one left in flight.
    api.fetchLobbies.mockImplementation(
      (signal: AbortSignal) =>
        new Promise<readonly SessionLobbyListing[]>((_resolve, reject) => {
          observedSignals.push(signal);
          signal.addEventListener('abort', () => {
            reject(new Error('aborted'));
          });
        }),
    );
    await advance(LOBBY_DIRECTORY_POLL_MILLISECONDS);
    expect(observedSignals).toHaveLength(1);
    expect(result.current.status).toBe('loading');

    unmount();
    await flushPromises();
    expect(observedSignals[0]?.aborted).toBe(true);
    expect(api.dispose).toHaveBeenCalledOnce();
    expect(result.current.status).toBe('loading');
    expect(vi.getTimerCount()).toBe(0);
  });
});
