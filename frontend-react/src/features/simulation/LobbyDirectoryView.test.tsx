import { fireEvent, render, screen, within } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { LobbyDirectoryView } from './LobbyDirectoryView';
import { SimulationApiError } from './SimulationApiError';
import { lobbyDirectoryMessageExample } from './fixtures/protocolV2Examples';
import { validateLobbyDirectoryMessage } from './sessionProtocolValidation';
import type { SessionLobbyListing } from './simulationProtocolTypes';
import {
  initialLobbyDirectoryState,
  type LobbyDirectoryState,
} from './useLobbyDirectory';

const listings: readonly SessionLobbyListing[] = validateLobbyDirectoryMessage(
  structuredClone(lobbyDirectoryMessageExample),
  lobbyDirectoryMessageExample.meta.request_id,
).data.lobbies;

function readyDirectory(
  overrides: Partial<LobbyDirectoryState> = {},
): LobbyDirectoryState {
  return {
    ...initialLobbyDirectoryState,
    listings,
    status: 'ready',
    ...overrides,
  };
}

function roomCards() {
  const [roomOne, roomTwo] = screen.getAllByRole('listitem');
  if (roomOne === undefined || roomTwo === undefined) {
    throw new Error('TEST.ROOM_CARDS_MISSING');
  }
  return { roomOne, roomTwo };
}

describe('LobbyDirectoryView', () => {
  it('lists every room with its census and one join per room', () => {
    const onJoin = vi.fn();
    render(
      <LobbyDirectoryView
        directory={readyDirectory()}
        notice={null}
        onJoin={onJoin}
      />,
    );

    expect(
      screen.getByRole('heading', { level: 2, name: 'Rooms' }),
    ).toBeVisible();
    expect(screen.getByRole('status')).toHaveTextContent(
      'Rooms refresh once a second while this list is open.',
    );
    const { roomOne, roomTwo } = roomCards();
    expect(
      within(roomOne).getByRole('heading', { level: 3, name: 'Room 1' }),
    ).toBeVisible();
    expect(within(roomOne).getByText('royale on arena-960x640')).toBeVisible();
    expect(within(roomOne).getByText('Match running')).toBeVisible();
    expect(within(roomOne).getByText('2 of 4 seats filled')).toBeVisible();
    expect(within(roomOne).getByText('1 player, 2 bots')).toBeVisible();
    expect(within(roomTwo).getByText('In the lobby')).toBeVisible();

    fireEvent.click(
      within(roomTwo).getByRole('button', { name: 'Join Room 2' }),
    );
    expect(onJoin).toHaveBeenCalledWith(2);
  });

  it('disables the join of a full room and says why', () => {
    const full = listings.map((listing) =>
      listing.lobby_id === 2
        ? { ...listing, filled_seat_count: 4, session_count: 4 }
        : listing,
    );
    render(
      <LobbyDirectoryView
        directory={readyDirectory({ listings: full })}
        notice={null}
        onJoin={vi.fn()}
      />,
    );

    expect(screen.getByRole('button', { name: 'Join Room 2' })).toBeDisabled();
    expect(screen.getByRole('button', { name: 'Join Room 1' })).toBeEnabled();
    expect(
      screen.getByText(
        'Room 2 is full. Choose another room, or try again once somebody leaves.',
      ),
    ).toBeVisible();
  });

  it('shows the notice a refused join left, and a read error beside the last rooms', () => {
    const error = new SimulationApiError(
      'SIMULATION.LOBBY_DIRECTORY_REQUEST_FAILED',
      'The directory is unreachable.',
      { retryable: true },
    );
    render(
      <LobbyDirectoryView
        directory={readyDirectory({ error, status: 'failed' })}
        notice={{ kind: 'refused', message: 'Room 2 is full.' }}
        onJoin={vi.fn()}
      />,
    );

    expect(screen.getByRole('alert')).toHaveTextContent('Room 2 is full.');
    expect(screen.getByRole('status')).toHaveTextContent('could not be read');
    expect(
      screen.getByText(/SIMULATION.LOBBY_DIRECTORY_REQUEST_FAILED/),
    ).toBeVisible();
    expect(screen.getAllByRole('listitem')).toHaveLength(2);
  });

  it('shows only the status before the first read', () => {
    render(
      <LobbyDirectoryView
        directory={{ ...initialLobbyDirectoryState, status: 'loading' }}
        notice={null}
        onJoin={vi.fn()}
      />,
    );

    expect(screen.getByRole('status')).toHaveTextContent('Reading the rooms…');
    expect(screen.queryAllByRole('listitem')).toHaveLength(0);
    expect(screen.queryByRole('alert')).toBeNull();
  });
});
