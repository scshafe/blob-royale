import { useEffect, useReducer } from 'react';

import { SimulationApi, type SimulationApiBoundary } from './SimulationApi';
import { SimulationApiError } from './SimulationApiError';
import { LOBBY_DIRECTORY_POLL_MILLISECONDS } from './simulationConstants';
import type { SessionLobbyListing } from './simulationProtocolTypes';

export type LobbyDirectoryStatus = 'idle' | 'loading' | 'ready' | 'failed';

export interface LobbyDirectoryState {
  readonly error: SimulationApiError | null;
  /** The rooms as last read, kept across a failed read and across a pause so the list never blanks. */
  readonly listings: readonly SessionLobbyListing[];
  readonly status: LobbyDirectoryStatus;
}

export type LobbyDirectoryAction =
  | { readonly type: 'fetch_started' }
  | {
      readonly type: 'fetch_succeeded';
      readonly listings: readonly SessionLobbyListing[];
    }
  | { readonly type: 'fetch_failed'; readonly error: SimulationApiError }
  | { readonly type: 'stopped' };

export interface LobbyDirectoryOptions {
  /** Whether the directory is on screen. Off screen it is not read at all. */
  readonly enabled: boolean;
}

export type LobbyDirectoryApiFactory = () => SimulationApiBoundary;

const NO_LISTINGS: readonly SessionLobbyListing[] = Object.freeze([]);

export const initialLobbyDirectoryState: LobbyDirectoryState = Object.freeze({
  error: null,
  listings: NO_LISTINGS,
  status: 'idle',
});

/** Reduces the directory's read state; the listings survive everything but a newer read. */
export function lobbyDirectoryReducer(
  state: LobbyDirectoryState,
  action: LobbyDirectoryAction,
): LobbyDirectoryState {
  switch (action.type) {
    case 'fetch_started':
      return Object.freeze({ ...state, status: 'loading' });
    case 'fetch_succeeded':
      return Object.freeze({
        error: null,
        listings: action.listings,
        status: 'ready',
      });
    case 'fetch_failed':
      return Object.freeze({ ...state, error: action.error, status: 'failed' });
    case 'stopped':
      return Object.freeze({ ...state, status: 'idle' });
  }
}

function createDefaultSimulationApi(): SimulationApiBoundary {
  return new SimulationApi();
}

function normalizeDirectoryError(error: unknown): SimulationApiError {
  return error instanceof SimulationApiError
    ? error
    : new SimulationApiError(
        'SIMULATION.LOBBY_DIRECTORY_REQUEST_FAILED',
        'Lobby directory request failed unexpectedly.',
        { cause: error, retryable: true },
      );
}

function pageIsVisible(): boolean {
  return document.visibilityState !== 'hidden';
}

/**
 * @canonical lobby_directory -- the one reader of `GET /api/v3/lobbies`, and the client's only poll.
 *
 * The directory is a point-in-time read and not a subscription (`docs/protocol/v3.md` § "The lobby
 * directory"), so this reads it once a second for exactly as long as somebody is looking at it:
 * while `enabled` and while the page is visible. The next read is scheduled after the last one
 * completes, never on an interval, so a slow server is asked at most once at a time; a read that
 * fails is reported and the next one is still scheduled, because the directory is advice and a
 * stale list with an error beside it beats no list. Nothing here touches a socket: joining is the
 * connection hook's, and the two never share state.
 */
export function useLobbyDirectory(
  { enabled }: LobbyDirectoryOptions,
  // Referentially stable, like the connection hook's: a new factory is a new reader.
  apiFactory: LobbyDirectoryApiFactory = createDefaultSimulationApi,
): LobbyDirectoryState {
  const [state, dispatch] = useReducer(
    lobbyDirectoryReducer,
    initialLobbyDirectoryState,
  );

  useEffect(() => {
    if (!enabled) {
      dispatch({ type: 'stopped' });
      return undefined;
    }

    const api = apiFactory();
    let abortController: AbortController | null = null;
    let inFlight = false;
    let mounted = true;
    let timer: ReturnType<typeof setTimeout> | null = null;

    const clearTimer = (): void => {
      if (timer !== null) {
        clearTimeout(timer);
        timer = null;
      }
    };

    const scheduleNextRead = (): void => {
      clearTimer();
      if (!mounted || !pageIsVisible()) {
        return;
      }
      timer = setTimeout(() => {
        timer = null;
        void readOnce();
      }, LOBBY_DIRECTORY_POLL_MILLISECONDS);
    };

    const readOnce = async (): Promise<void> => {
      if (!mounted || inFlight) {
        return;
      }
      inFlight = true;
      abortController = new AbortController();
      dispatch({ type: 'fetch_started' });
      try {
        const listings = await api.fetchLobbies(abortController.signal);
        if (mounted) {
          dispatch({ listings, type: 'fetch_succeeded' });
        }
      } catch (error) {
        if (mounted) {
          dispatch({
            error: normalizeDirectoryError(error),
            type: 'fetch_failed',
          });
        }
      } finally {
        inFlight = false;
        abortController = null;
        scheduleNextRead();
      }
    };

    const onVisibilityChange = (): void => {
      clearTimer();
      if (pageIsVisible()) {
        void readOnce();
      }
    };
    document.addEventListener('visibilitychange', onVisibilityChange);
    if (pageIsVisible()) {
      void readOnce();
    }

    return () => {
      mounted = false;
      clearTimer();
      abortController?.abort('directory_hidden');
      document.removeEventListener('visibilitychange', onVisibilityChange);
      api.dispose();
    };
  }, [apiFactory, enabled]);

  return state;
}
