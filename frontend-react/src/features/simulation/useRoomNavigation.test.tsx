import { act, renderHook } from '@testing-library/react';
import { beforeEach, describe, expect, it } from 'vitest';

import {
  parseLobbyIdFromSearch,
  roomNavigationReducer,
  searchForLobby,
  useRoomNavigation,
} from './useRoomNavigation';

beforeEach(() => {
  window.history.replaceState(null, '', '/');
});

describe('parseLobbyIdFromSearch', () => {
  it('accepts exactly the grammar of the room segment', () => {
    expect(parseLobbyIdFromSearch('?lobby=2')).toBe(2);
    expect(parseLobbyIdFromSearch('?lobby=999&other=1')).toBe(999);
    for (const search of [
      '',
      '?lobby=',
      '?lobby=0',
      '?lobby=01',
      '?lobby=1000',
      '?lobby=two',
      '?lobby=1.5',
      '?lobby=-1',
      '?room=2',
    ]) {
      expect(parseLobbyIdFromSearch(search)).toBeNull();
    }
  });

  it('round-trips with searchForLobby', () => {
    expect(searchForLobby(null)).toBe('');
    expect(parseLobbyIdFromSearch(searchForLobby(7))).toBe(7);
  });
});

describe('roomNavigationReducer', () => {
  it('clears the notice on a join and keeps the one a leave was given', () => {
    const notice = { kind: 'refused', message: 'Room 2 is full.' } as const;
    const left = roomNavigationReducer(
      { lobbyId: 2, notice: null },
      { notice, type: 'left' },
    );
    expect(left).toEqual({ lobbyId: null, notice });
    expect(roomNavigationReducer(left, { lobbyId: 1, type: 'joined' })).toEqual(
      { lobbyId: 1, notice: null },
    );
    expect(
      roomNavigationReducer(left, { lobbyId: null, type: 'location_changed' }),
    ).toEqual({ lobbyId: null, notice: null });
  });
});

describe('useRoomNavigation', () => {
  it('starts on the directory, joins by writing the room into the URL, and leaves back to it', () => {
    const { result } = renderHook(() => useRoomNavigation());
    expect(result.current).toMatchObject({ lobbyId: null, notice: null });

    act(() => {
      result.current.join(2);
    });
    expect(result.current.lobbyId).toBe(2);
    expect(window.location.search).toBe('?lobby=2');

    act(() => {
      result.current.leave({ kind: 'refused', message: 'Room 2 is full.' });
    });
    expect(result.current).toMatchObject({
      lobbyId: null,
      notice: { kind: 'refused', message: 'Room 2 is full.' },
    });
    expect(window.location.search).toBe('');

    act(() => {
      result.current.join(1);
    });
    expect(result.current).toMatchObject({ lobbyId: 1, notice: null });
  });

  it('starts in the room the URL names, so a room can be reloaded and shared', () => {
    window.history.replaceState(null, '', '/?lobby=3');
    const { result } = renderHook(() => useRoomNavigation());
    expect(result.current.lobbyId).toBe(3);
  });

  it('ignores a query outside the grammar rather than joining a room that cannot exist', () => {
    window.history.replaceState(null, '', '/?lobby=01');
    const { result } = renderHook(() => useRoomNavigation());
    expect(result.current.lobbyId).toBeNull();
  });

  it('follows the browser back button by reading the URL again', () => {
    const { result } = renderHook(() => useRoomNavigation());
    act(() => {
      result.current.join(2);
    });

    act(() => {
      window.history.replaceState(null, '', '/');
      window.dispatchEvent(new PopStateEvent('popstate'));
    });
    expect(result.current.lobbyId).toBeNull();

    act(() => {
      window.history.replaceState(null, '', '/?lobby=4');
      window.dispatchEvent(new PopStateEvent('popstate'));
    });
    expect(result.current.lobbyId).toBe(4);
  });

  it('refuses to join an id outside the grammar', () => {
    const { result } = renderHook(() => useRoomNavigation());
    expect(() => {
      result.current.join(0);
    }).toThrow(/lobby id/);
    expect(() => {
      result.current.join(1_000);
    }).toThrow(/lobby id/);
    expect(result.current.lobbyId).toBeNull();
    expect(window.location.search).toBe('');
  });
});
