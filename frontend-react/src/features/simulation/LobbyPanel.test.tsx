import { solidTerrain } from './fixtures/terrainFrames';
import { act, fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { LobbyPanel, type LobbyPanelProps } from './LobbyPanel';
import { snapshotDocument } from './fixtures/sessionFrames';
import { SEAT_COUNT_COMMAND_DEBOUNCE_MILLISECONDS } from './simulationConstants';
import type {
  SessionMatchSection,
  SessionSeat,
} from './simulationProtocolTypes';
import { validateSessionSnapshotMessage } from './sessionProtocolValidation';

const snapshot = validateSessionSnapshotMessage(snapshotDocument(), {
  messageSequence: 1,
  requestId: snapshotDocument().meta.request_id,
  tickSequence: null,
  terrain: solidTerrain,
}).data;

const PERSON: SessionSeat = Object.freeze({
  controller_id: 3,
  kind: 'controller',
  npc_kind: null,
});
const BUILT_BOT: SessionSeat = Object.freeze({
  controller_id: 12,
  kind: 'npc',
  npc_kind: 'wanderer',
});
const EMPTY: SessionSeat = Object.freeze({
  controller_id: null,
  kind: 'empty',
  npc_kind: null,
});

/** The golden roster in `lobby`: a person, a built bot, a joining bot, and an empty seat. */
function lobbyMatch(
  overrides: Partial<SessionMatchSection> = {},
): SessionMatchSection {
  return {
    ...snapshot.match,
    phase: 'lobby',
    start_requested: false,
    ...overrides,
  };
}

function renderPanel(overrides: Partial<LobbyPanelProps> = {}) {
  const sendCommand = vi.fn(() => true);
  const props: LobbyPanelProps = {
    entities: snapshot.entities,
    match: lobbyMatch(),
    npcControllerKinds: ['wanderer', 'chaser'],
    ownControllerId: 3,
    seatCountMaximum: 6,
    sendCommand,
    ...overrides,
  };
  const view = render(<LobbyPanel {...props} />);
  return { ...view, props, sendCommand };
}

afterEach(() => {
  vi.useRealTimers();
});

describe('LobbyPanel', () => {
  it('renders every seat by name, marks the own seat, and explains why Start is disabled', () => {
    renderPanel();

    expect(
      screen.getByRole('heading', { level: 3, name: 'Lobby' }),
    ).toBeVisible();
    const seats = screen.getAllByRole('listitem');
    expect(seats).toHaveLength(4);
    expect(seats[0]).toHaveTextContent('Seat 1 Cole Shaffer (you)');
    expect(seats[1]).toHaveTextContent('Seat 2 wanderer');
    expect(seats[2]).toHaveTextContent('Seat 3 chaser (joining)');
    expect(seats[3]).toHaveTextContent('Seat 4 Empty');

    expect(screen.getByRole('button', { name: 'Start match' })).toBeDisabled();
    expect(
      screen.getByText('Waiting for 1 empty seat to be filled.'),
    ).toBeVisible();
    // Only the empty seat is a control; the person's seat and the bots' seats are not buttons.
    expect(
      screen.getByRole('button', { name: 'Seat 4 Empty' }),
    ).toHaveAttribute('aria-haspopup', 'menu');
    expect(screen.queryByRole('button', { name: /Cole Shaffer/ })).toBeNull();
  });

  it('opens the bot menu on right-click, seats a bot from it, and suppresses the browser menu', () => {
    const { sendCommand } = renderPanel();
    const emptySeat = screen.getByRole('button', { name: 'Seat 4 Empty' });

    const contextMenu = fireEvent.contextMenu(emptySeat);
    expect(contextMenu).toBe(false);
    const menu = screen.getByRole('menu', { name: 'Bots for seat 4' });
    expect(
      within(menu)
        .getAllByRole('menuitem')
        .map((item) => item.textContent),
    ).toEqual(['wanderer', 'chaser']);

    fireEvent.click(within(menu).getByRole('menuitem', { name: 'chaser' }));
    expect(sendCommand).toHaveBeenCalledWith({
      kind: 'seat_npc',
      payload: { npc_kind: 'chaser', seat_index: 3 },
    });
    expect(screen.queryByRole('menu')).toBeNull();
  });

  it('opens the same menu from the keyboard and closes it on Escape or an outside press', () => {
    renderPanel();
    const emptySeat = screen.getByRole('button', { name: 'Seat 4 Empty' });

    // A button activates on Enter and Space; the click is what either produces.
    fireEvent.click(emptySeat);
    expect(emptySeat).toHaveAttribute('aria-expanded', 'true');
    expect(screen.getByRole('menu')).toBeVisible();

    fireEvent.keyDown(document, { key: 'Escape' });
    expect(screen.queryByRole('menu')).toBeNull();
    expect(emptySeat).toHaveAttribute('aria-expanded', 'false');

    fireEvent.click(emptySeat);
    expect(screen.getByRole('menu')).toBeVisible();
    fireEvent.pointerDown(document.body);
    expect(screen.queryByRole('menu')).toBeNull();

    // A press inside the panel is not an outside press.
    fireEvent.click(emptySeat);
    fireEvent.pointerDown(
      screen.getByRole('heading', { level: 3, name: 'Lobby' }),
    );
    expect(screen.getByRole('menu')).toBeVisible();
  });

  it("offers no bot on a person's seat and says so when no kind is registered", () => {
    renderPanel({ npcControllerKinds: [] });

    const contextMenu = fireEvent.contextMenu(screen.getByText(/Cole Shaffer/));
    expect(contextMenu).toBe(false);
    expect(screen.queryByRole('menu')).toBeNull();

    fireEvent.click(screen.getByRole('button', { name: 'Seat 4 Empty' }));
    expect(screen.getByRole('menu')).toHaveTextContent(
      'No bot kinds are registered.',
    );
    expect(screen.queryByRole('menuitem')).toBeNull();
  });

  it("clears a bot seat and never a person's", () => {
    const { sendCommand } = renderPanel();

    expect(screen.getAllByRole('button', { name: /^Clear seat/ })).toHaveLength(
      2,
    );
    fireEvent.click(screen.getByRole('button', { name: 'Clear seat 3' }));
    expect(sendCommand).toHaveBeenCalledWith({
      kind: 'clear_seat',
      payload: { seat_index: 2 },
    });
    expect(screen.queryByRole('button', { name: 'Clear seat 1' })).toBeNull();
  });

  it('enables Start exactly when every seat is filled and sends start_match', () => {
    const { sendCommand } = renderPanel({
      match: lobbyMatch({ seats: [PERSON, BUILT_BOT] }),
    });

    const start = screen.getByRole('button', { name: 'Start match' });
    expect(start).toBeEnabled();
    expect(screen.queryByText(/Waiting for/)).toBeNull();
    fireEvent.click(start);
    expect(sendCommand).toHaveBeenCalledWith({
      kind: 'start_match',
      payload: {},
    });
  });

  it('says Start is pressed while the field completes', () => {
    renderPanel({ match: lobbyMatch({ start_requested: true }) });

    expect(screen.getByRole('button', { name: 'Start match' })).toBeDisabled();
    expect(
      screen.getByText(
        'Start is pressed. The match begins once every seat is filled.',
      ),
    ).toBeVisible();
  });

  it('bounds the seat-count control and sends one debounced set_seat_count', () => {
    vi.useFakeTimers();
    const { sendCommand } = renderPanel();
    const control = screen.getByRole('spinbutton', { name: 'Seats' });

    // Floored one above the highest occupied seat (the joining bot in seat 3) and capped at the
    // map's six markers.
    expect(control).toHaveAttribute('min', '3');
    expect(control).toHaveAttribute('max', '6');
    expect(control).toHaveValue(4);
    expect(screen.getByText('3 to 6')).toBeVisible();

    // Two changes inside the debounce window are one command carrying the final value.
    fireEvent.change(control, { target: { value: '5' } });
    fireEvent.change(control, { target: { value: '6' } });
    expect(control).toHaveValue(6);
    act(() => {
      vi.advanceTimersByTime(SEAT_COUNT_COMMAND_DEBOUNCE_MILLISECONDS - 1);
    });
    expect(sendCommand).not.toHaveBeenCalled();
    act(() => {
      vi.advanceTimersByTime(1);
    });
    expect(sendCommand).toHaveBeenCalledTimes(1);
    expect(sendCommand).toHaveBeenCalledWith({
      kind: 'set_seat_count',
      payload: { seat_count: 6 },
    });

    // An ask below the floor or above the ceiling is clamped rather than sent as the tick would
    // ignore it.
    fireEvent.change(control, { target: { value: '1' } });
    expect(control).toHaveValue(3);
    fireEvent.change(control, { target: { value: '40' } });
    expect(control).toHaveValue(6);
  });

  it('shows the published count once the server agrees, and asks for nothing then', () => {
    vi.useFakeTimers();
    const { rerender, props, sendCommand } = renderPanel();
    const control = screen.getByRole('spinbutton', { name: 'Seats' });

    fireEvent.change(control, { target: { value: '5' } });
    rerender(
      <LobbyPanel
        {...props}
        match={lobbyMatch({ seats: [...snapshot.match.seats, EMPTY] })}
      />,
    );
    expect(control).toHaveValue(5);
    act(() => {
      vi.advanceTimersByTime(SEAT_COUNT_COMMAND_DEBOUNCE_MILLISECONDS);
    });
    expect(sendCommand).not.toHaveBeenCalled();
  });
});
