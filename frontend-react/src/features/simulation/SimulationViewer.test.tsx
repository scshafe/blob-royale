import { fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { SimulationViewer } from './SimulationViewer';
import { SimulationApiError } from './SimulationApiError';
import type {
  SimulationConnection,
  SimulationSessionIdentity,
} from './useSimulationConnection';
import type { SessionEntitySnapshot } from './simulationProtocolTypes';
import { configurationResponseExample } from './fixtures/protocolV1Examples';
import {
  hillSnapshotDocument,
  snapshotDocument,
} from './fixtures/sessionFrames';
import { validateSessionSnapshotMessage } from './sessionProtocolValidation';
import { validateSimulationConfigurationResponse } from './simulationProtocolValidation';

const configuration = validateSimulationConfigurationResponse(
  structuredClone(configurationResponseExample),
  configurationResponseExample.meta.request_id,
).data;

const snapshot = validateSessionSnapshotMessage(snapshotDocument(), {
  messageSequence: 1,
  requestId: snapshotDocument().meta.request_id,
  tickSequence: null,
});

const hillSnapshot = validateSessionSnapshotMessage(hillSnapshotDocument(), {
  messageSequence: 1,
  requestId: hillSnapshotDocument().meta.request_id,
  tickSequence: null,
});

const session: SimulationSessionIdentity = Object.freeze({
  acceptedCommandKinds: ['set_thrust'] as const,
  controllerId: 3,
  displayName: 'Cole Shaffer',
  firstEntityId: 7,
  lobbyId: 1,
  map: 'arena-960x640',
  mode: 'royale',
  npcControllerKinds: ['wanderer', 'chaser'],
  seatCountMaximum: 32,
});

const zeroThrust = Object.freeze({ x: 0, y: 0 });

function createConnection(
  overrides: Partial<SimulationConnection> = {},
): SimulationConnection {
  return Object.freeze({
    configuration,
    entities: snapshot.data.entities,
    error: null,
    match: snapshot.data.match,
    ownEntityId: 7,
    reconnectAttempt: 0,
    sendCommand: vi.fn(() => true),
    session,
    snapshot,
    status: 'connected',
    ...overrides,
  });
}

/** The golden roster with the session's own blob (entity 7) reporting a given exposure counter. */
function entitiesWithOwnExposure(
  outsideTicks: number,
): readonly SessionEntitySnapshot[] {
  return snapshot.data.entities.map((entity) =>
    entity.entity_id === 7
      ? {
          entity_id: entity.entity_id,
          components: {
            ...entity.components,
            zone_exposure: { outside_ticks: outsideTicks },
          },
        }
      : entity,
  );
}

afterEach(() => {
  vi.restoreAllMocks();
});

describe('SimulationViewer', () => {
  it('renders accessible match state with the debug panel behind a toggle', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection()}
        thrust={zeroThrust}
      />,
    );

    // The app's own name is the shell's h1; a room view is one level down.
    expect(
      screen.getByRole('heading', { level: 2, name: 'Room 1' }),
    ).toBeVisible();
    expect(screen.getByRole('status')).toHaveTextContent(
      'Connected to the match session.',
    );
    expect(screen.getByRole('img')).toHaveAccessibleDescription(
      'Complete tick 12904 with 4 entities and 2 players.',
    );

    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(within(hud).getByText('Cole Shaffer')).toBeVisible();
    expect(within(hud).getByText('running')).toBeVisible();
    expect(within(hud).getByText('In play')).toBeVisible();
    expect(within(hud).getByRole('row', { name: 'Alive 2' })).toBeVisible();

    expect(
      screen.queryByRole('table', { name: 'Session identity' }),
    ).toBeNull();
    const toggle = screen.getByRole('button', {
      name: 'Show simulation details',
    });
    expect(toggle).toHaveAttribute('aria-expanded', 'false');

    fireEvent.click(toggle);

    expect(
      screen.getByRole('table', { name: 'Session identity' }),
    ).toBeVisible();
    expect(
      screen.getByRole('button', { name: 'Hide simulation details' }),
    ).toHaveAttribute('aria-expanded', 'true');
  });

  it('tells a deferred joiner it is waiting for the next match', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: [],
          match: null,
          ownEntityId: null,
          session: null,
          snapshot: null,
          status: 'awaiting_match',
        })}
        thrust={zeroThrust}
      />,
    );

    expect(screen.getByRole('status')).toHaveTextContent(
      'Joined the session. Waiting for the next match to seat a blob…',
    );
    expect(screen.getByRole('img')).toHaveAccessibleDescription(
      'Waiting for the first complete world snapshot.',
    );
  });

  it('announces the eliminated overlay for a player with a placement', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const eliminatedSession: SimulationSessionIdentity = {
      ...session,
      controllerId: 6,
    };

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          ownEntityId: null,
          session: eliminatedSession,
        })}
        thrust={zeroThrust}
      />,
    );

    expect(screen.getByText('Eliminated')).toBeVisible();
    expect(screen.getByText(/You placed #3\./)).toBeVisible();
    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(within(hud).getByText('#3')).toBeVisible();
  });

  it('announces the winner overlay when the match has ended', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          match: {
            ...snapshot.data.match,
            outcome: {
              kind: 'won_by_entity',
              winner_entity_id: 7,
              winner_team_id: null,
            },
            phase: 'ended',
          },
        })}
        thrust={zeroThrust}
      />,
    );

    expect(screen.getByText('You win')).toBeVisible();
    expect(
      screen.getByText('You were the last blob in the zone.'),
    ).toBeVisible();
  });

  it('counts the remaining grace down in the HUD while the own blob is outside', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const hudRowHeader = () =>
      within(screen.getByRole('table', { name: 'Match status' })).queryByRole(
        'rowheader',
        { name: 'Zone exposure' },
      );

    // The golden own blob is inside the zone, so there is nothing to warn about.
    const view = render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection()}
        thrust={zeroThrust}
      />,
    );
    expect(hudRowHeader()).toBeNull();

    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: entitiesWithOwnExposure(200),
        })}
        thrust={zeroThrust}
      />,
    );

    expect(hudRowHeader()).toBeVisible();
    // 200 of the golden frame's published 1,200 grace ticks spent leaves 1,000, which is 2.5 s at
    // the published 400 ticks/s. Remaining, not elapsed: `elimination_grace_ticks` reaches the
    // client in the royale mode-state block since protocol 2.2, so the row answers the question the
    // player actually has.
    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(
      within(hud).getByRole('row', { name: 'Zone exposure 2.5 s left' }),
    ).toBeVisible();

    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({ entities: entitiesWithOwnExposure(0) })}
        thrust={zeroThrust}
      />,
    );

    expect(hudRowHeader()).toBeNull();
    expect(screen.queryByText(/left/)).toBeNull();
  });

  it('falls back to elapsed exposure when the frame publishes no grace', () => {
    // A mode with no non-entity-shaped state publishes the `none` block and therefore no grace.
    // The client must not invent a duration; it reports the counter it really was given.
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: entitiesWithOwnExposure(1_200),
          match: {
            ...snapshot.data.match,
            mode_state: {
              schema_id: 'blob-royale://protocol/v2/mode-state/none',
              value: {},
            },
          },
        })}
        thrust={zeroThrust}
      />,
    );

    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(
      within(hud).getByRole('row', { name: 'Zone exposure Outside 3.0 s' }),
    ).toBeVisible();
  });

  it('shows no time left rather than a negative countdown past the grace', () => {
    // `zone_elimination` eliminates on the tick the counter reaches the bound and the recorder
    // destroys the entity in the same tick, so a published counter should never exceed it. The
    // clamp is what keeps a frame that says otherwise from rendering a negative remainder.
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: entitiesWithOwnExposure(5_000),
        })}
        thrust={zeroThrust}
      />,
    );

    const hud = screen.getByRole('table', { name: 'Match status' });
    expect(
      within(hud).getByRole('row', { name: 'Zone exposure 0.0 s left' }),
    ).toBeVisible();
  });

  it('does not warn about a peer that is outside the zone', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    // Entity 8 in the golden snapshot has been outside for 214 ticks; entity 7 is this session.
    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection()}
        thrust={zeroThrust}
      />,
    );

    expect(
      within(screen.getByRole('table', { name: 'Match status' })).queryByRole(
        'rowheader',
        { name: 'Zone exposure' },
      ),
    ).toBeNull();
  });

  it('announces a terminal connection failure as an alert', () => {
    const error = new SimulationApiError(
      'SIMULATION.RECONNECT_EXHAUSTED',
      'Simulation reconnect budget was exhausted.',
    );

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          configuration: null,
          entities: [],
          error,
          match: null,
          ownEntityId: null,
          reconnectAttempt: 5,
          session: null,
          snapshot: null,
          status: 'failed',
        })}
        thrust={zeroThrust}
      />,
    );

    expect(screen.getByRole('alert')).toHaveTextContent('could not connect');
    expect(screen.getByText(/SIMULATION.RECONNECT_EXHAUSTED/)).toBeVisible();
  });

  it('shows the lobby panel exactly while the match is in lobby and the session may start one', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const lobbyMatch = { ...snapshot.data.match, phase: 'lobby' as const };
    const startingSession: SimulationSessionIdentity = {
      ...session,
      acceptedCommandKinds: ['set_thrust', 'start_match'],
    };

    const view = render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          match: lobbyMatch,
          session: startingSession,
        })}
        thrust={zeroThrust}
      />,
    );
    expect(
      screen.getByRole('heading', { level: 3, name: 'Lobby' }),
    ).toBeVisible();
    expect(screen.getByRole('button', { name: 'Start match' })).toBeDisabled();

    // A running match has no lobby to operate, and neither does a session whose welcome advertised
    // no `start_match`: `sandbox` publishes only `set_thrust`.
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({ session: startingSession })}
        thrust={zeroThrust}
      />,
    );
    expect(
      screen.queryByRole('heading', { level: 3, name: 'Lobby' }),
    ).toBeNull();
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({ match: lobbyMatch })}
        thrust={zeroThrust}
      />,
    );
    expect(
      screen.queryByRole('heading', { level: 3, name: 'Lobby' }),
    ).toBeNull();
  });

  it('shows the hill section and the scoreboard for a hill frame only', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const hillSession: SimulationSessionIdentity = {
      ...session,
      map: 'hills-960x640',
      mode: 'king_of_the_hill',
    };

    const view = render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: hillSnapshot.data.entities,
          match: hillSnapshot.data.match,
          session: hillSession,
          snapshot: hillSnapshot,
        })}
        thrust={zeroThrust}
      />,
    );

    const hud = screen.getByRole('table', { name: 'Match status' });
    // 94,000 running ticks left of 96,000 at the published 400 ticks/s; 4 of 30 points; 280 of the
    // 400-tick interval still to hold.
    expect(
      within(hud).getByRole('row', { name: 'Time left 235.0 s' }),
    ).toBeVisible();
    expect(
      within(hud).getByRole('row', { name: 'Score 4 of 30' }),
    ).toBeVisible();
    expect(
      within(hud).getByRole('row', { name: 'Hill 0.7 s to a point' }),
    ).toBeVisible();
    expect(
      within(hud).queryByRole('rowheader', { name: 'Placement' }),
    ).toBeNull();

    const board = screen.getByRole('table', { name: 'Scoreboard' });
    expect(
      within(board)
        .getAllByRole('row')
        .map((row) => row.textContent),
    ).toEqual(['wanderer-16', 'Cole Shaffer4', 'chaser-22 (out)']);
    expect(
      within(board).getByRole('row', { name: 'Cole Shaffer 4' }),
    ).toHaveAttribute('aria-current', 'true');
    expect(screen.getByRole('img')).toHaveAccessibleDescription(
      'Complete tick 12904 with 4 entities and 2 players.',
    );

    // The royale golden frame renders exactly the rows it always did: no hill section, no board.
    view.rerender(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection()}
        thrust={zeroThrust}
      />,
    );
    const royaleHud = screen.getByRole('table', { name: 'Match status' });
    expect(
      within(royaleHud)
        .getAllByRole('rowheader')
        .map((header) => header.textContent),
    ).toEqual([
      'Player',
      'Phase',
      'Phase elapsed',
      'Alive',
      'Placement',
      'Thrust',
    ]);
    expect(screen.queryByRole('table', { name: 'Scoreboard' })).toBeNull();
  });

  it('counts a knocked-out player down to its seat instead of calling it in play', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const knockedOutSession: SimulationSessionIdentity = {
      ...session,
      controllerId: 5,
      displayName: 'chaser-2',
      mode: 'king_of_the_hill',
    };

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: hillSnapshot.data.entities,
          match: hillSnapshot.data.match,
          ownEntityId: 10,
          session: knockedOutSession,
          snapshot: hillSnapshot,
        })}
        thrust={zeroThrust}
      />,
    );

    const hud = screen.getByRole('table', { name: 'Match status' });
    // 300 ticks at 400 ticks/s. The entity is still on the frame, so no overlay covers the arena.
    expect(
      within(hud).getByRole('row', { name: 'Hill Back in 0.8 s' }),
    ).toHaveClass('MatchHudDanger');
    expect(
      within(hud).getByRole('row', { name: 'Score 2 of 30' }),
    ).toBeVisible();
    expect(screen.queryByText('Eliminated')).toBeNull();
  });

  it('announces the hill winner with its points when the match has ended', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer
        lobbyId={1}
        connection={createConnection({
          entities: hillSnapshot.data.entities,
          match: {
            ...hillSnapshot.data.match,
            outcome: {
              kind: 'won_by_entity',
              winner_entity_id: 8,
              winner_team_id: null,
            },
            phase: 'ended',
          },
          session: { ...session, mode: 'king_of_the_hill' },
          snapshot: hillSnapshot,
        })}
        thrust={zeroThrust}
      />,
    );

    expect(screen.getByText('Winner')).toBeVisible();
    expect(
      screen.getByText('wanderer-1 held the hill with 6 points.'),
    ).toBeVisible();
  });
});
