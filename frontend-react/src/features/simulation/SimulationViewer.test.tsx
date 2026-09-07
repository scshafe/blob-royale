import { fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { SimulationViewer } from './SimulationViewer';
import { SimulationApiError } from './SimulationApiError';
import type {
  SimulationConnection,
  SimulationSessionIdentity,
} from './useSimulationConnection';
import { configurationResponseExample } from './fixtures/protocolV1Examples';
import { snapshotDocument } from './fixtures/sessionFrames';
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

const session: SimulationSessionIdentity = Object.freeze({
  acceptedCommandKinds: ['set_thrust'] as const,
  controllerId: 3,
  displayName: 'Cole Shaffer',
  firstEntityId: 7,
  map: 'arena-960x640',
  mode: 'royale',
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

afterEach(() => {
  vi.restoreAllMocks();
});

describe('SimulationViewer', () => {
  it('renders accessible match state with the debug panel behind a toggle', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);

    render(
      <SimulationViewer connection={createConnection()} thrust={zeroThrust} />,
    );

    expect(
      screen.getByRole('heading', { level: 1, name: 'Blob Royale' }),
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

  it('announces a terminal connection failure as an alert', () => {
    const error = new SimulationApiError(
      'SIMULATION.RECONNECT_EXHAUSTED',
      'Simulation reconnect budget was exhausted.',
    );

    render(
      <SimulationViewer
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
});
