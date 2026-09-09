import { useCallback, useEffect, useMemo, useReducer } from 'react';

import { SimulationApiError } from './SimulationApiError';
import { LOBBY_ID_PATTERN } from './simulationConstants';

/**
 * The query parameter that names the room a page is in. A query rather than a path segment
 * because the bundle is served as a static directory behind `tailscale serve`, which has no
 * single-page fallback: `/?lobby=2` is served by the same `index.html` as `/`, and `/lobbies/2`
 * would be a `404` on refresh.
 */
export const LOBBY_QUERY_PARAMETER = 'lobby';

/** A sentence the directory shows about the room the player was just sent back from. */
export interface RoomNotice {
  readonly kind: 'refused';
  readonly message: string;
}

export interface RoomNavigationState {
  /** The room this page is in, or `null` on the directory. Mirrors the URL, never the socket. */
  readonly lobbyId: number | null;
  readonly notice: RoomNotice | null;
}

export type RoomNavigationAction =
  | { readonly type: 'joined'; readonly lobbyId: number }
  | { readonly type: 'left'; readonly notice: RoomNotice | null }
  | { readonly type: 'location_changed'; readonly lobbyId: number | null };

export interface RoomNavigation extends RoomNavigationState {
  readonly join: (lobbyId: number) => void;
  readonly leave: (notice?: RoomNotice | null) => void;
}

/** The room a page's query names, or `null` for anything outside `[1-9][0-9]{0,2}`. */
export function parseLobbyIdFromSearch(search: string): number | null {
  const value = new URLSearchParams(search).get(LOBBY_QUERY_PARAMETER);
  if (value === null || !LOBBY_ID_PATTERN.test(value)) {
    return null;
  }
  return Number(value);
}

/** The query for a room, or the empty query for the directory. */
export function searchForLobby(lobbyId: number | null): string {
  return lobbyId === null ? '' : `?${LOBBY_QUERY_PARAMETER}=${lobbyId}`;
}

/**
 * Reduces where the page is. A join clears the last notice, because the notice was about a room
 * the player has now chosen to leave behind; a leave keeps the one it was given, because that is
 * the sentence the directory exists to show next.
 */
export function roomNavigationReducer(
  _state: RoomNavigationState,
  action: RoomNavigationAction,
): RoomNavigationState {
  switch (action.type) {
    case 'joined':
      return Object.freeze({ lobbyId: action.lobbyId, notice: null });
    case 'left':
      return Object.freeze({ lobbyId: null, notice: action.notice });
    case 'location_changed':
      return Object.freeze({ lobbyId: action.lobbyId, notice: null });
  }
}

function readLocationState(): RoomNavigationState {
  return Object.freeze({
    lobbyId: parseLobbyIdFromSearch(window.location.search),
    notice: null,
  });
}

/**
 * @canonical room_navigation -- the one place the page's room is decided and the URL is written.
 *
 * URL is state: the directory is `/` and a room is `/?lobby=<id>`, so a room can be reloaded,
 * bookmarked, and sent to a friend, and the browser's back button is a leave. Joining pushes a
 * history entry and leaving pushes one back to the directory; a `popstate` reads the URL again
 * rather than remembering what it pushed, so the two can never disagree.
 */
export function useRoomNavigation(): RoomNavigation {
  const [state, dispatch] = useReducer(
    roomNavigationReducer,
    undefined,
    readLocationState,
  );

  useEffect(() => {
    const onPopState = (): void => {
      dispatch({
        lobbyId: parseLobbyIdFromSearch(window.location.search),
        type: 'location_changed',
      });
    };
    window.addEventListener('popstate', onPopState);
    return () => {
      window.removeEventListener('popstate', onPopState);
    };
  }, []);

  const join = useCallback((lobbyId: number): void => {
    if (
      !Number.isSafeInteger(lobbyId) ||
      !LOBBY_ID_PATTERN.test(String(lobbyId))
    ) {
      throw new SimulationApiError(
        'SIMULATION.ENDPOINT_INVALID',
        'A room can only be joined by a lobby id in [1-9][0-9]{0,2}.',
        { context: { lobby_id: lobbyId } },
      );
    }
    window.history.pushState(
      null,
      '',
      `${window.location.pathname}${searchForLobby(lobbyId)}`,
    );
    dispatch({ lobbyId, type: 'joined' });
  }, []);

  const leave = useCallback((notice: RoomNotice | null = null): void => {
    if (parseLobbyIdFromSearch(window.location.search) !== null) {
      window.history.pushState(null, '', window.location.pathname);
    }
    dispatch({ notice, type: 'left' });
  }, []);

  return useMemo(() => ({ ...state, join, leave }), [join, leave, state]);
}
