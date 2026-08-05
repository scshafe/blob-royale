import { render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { SimulationViewer } from './SimulationViewer';
import { SimulationApiError } from './SimulationApiError';
import type { SimulationConnectionState } from './useSimulationConnection';
import {
  configurationResponseExample,
  snapshotMessageExample,
} from './fixtures/protocolV1Examples';
import {
  validateSimulationConfigurationResponse,
  validateSimulationSnapshotMessage,
} from './simulationProtocolValidation';

const configuration = validateSimulationConfigurationResponse(
  structuredClone(configurationResponseExample),
  configurationResponseExample.meta.request_id,
).data;
const snapshotDocument = structuredClone(snapshotMessageExample);
snapshotDocument.meta.message_sequence = 1;
const snapshot = validateSimulationSnapshotMessage(
  snapshotDocument,
  configuration,
  null,
);

afterEach(() => {
  vi.restoreAllMocks();
});

describe('SimulationViewer', () => {
  it('renders accessible read-only state without command controls', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const connection: SimulationConnectionState = {
      configuration,
      error: null,
      reconnectAttempt: 0,
      snapshot,
      status: 'connected',
    };

    render(<SimulationViewer connection={connection} />);

    expect(
      screen.getByRole('heading', { level: 1, name: 'Blob Royale' }),
    ).toBeVisible();
    expect(screen.getByRole('status')).toHaveTextContent(
      'Connected to the read-only snapshot stream.',
    );
    expect(screen.getByRole('img')).toHaveAccessibleDescription(
      'Complete tick 1601 with 2 players.',
    );
    expect(screen.queryAllByRole('button')).toHaveLength(0);
    expect(screen.queryAllByRole('textbox')).toHaveLength(0);
    expect(screen.queryAllByRole('slider')).toHaveLength(0);
  });

  it('announces a terminal connection failure as an alert', () => {
    const error = new SimulationApiError(
      'SIMULATION.RECONNECT_EXHAUSTED',
      'Simulation reconnect budget was exhausted.',
    );
    const connection: SimulationConnectionState = {
      configuration: null,
      error,
      reconnectAttempt: 5,
      snapshot: null,
      status: 'failed',
    };

    render(<SimulationViewer connection={connection} />);

    expect(screen.getByRole('alert')).toHaveTextContent('could not connect');
    expect(screen.getByText(/SIMULATION.RECONNECT_EXHAUSTED/)).toBeVisible();
  });
});
