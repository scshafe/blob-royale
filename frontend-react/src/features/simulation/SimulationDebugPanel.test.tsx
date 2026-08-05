import { render, screen, within } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { SimulationDebugPanel } from './SimulationDebugPanel';
import { DEBUG_PLAYER_ROW_LIMIT } from './simulationConstants';
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

describe('SimulationDebugPanel', () => {
  it('renders semantic table captions and bounds player rows', () => {
    const snapshotDocument = structuredClone(snapshotMessageExample);
    snapshotDocument.meta.message_sequence = 1;
    snapshotDocument.data.players = Array.from(
      { length: DEBUG_PLAYER_ROW_LIMIT + 25 },
      (_, playerIndex) => ({
        acceleration: { x: 0, y: 0 },
        entity_id: playerIndex + 1,
        position: { x: 240, y: 300 },
        velocity: { x: 0, y: 0 },
      }),
    );
    const snapshot = validateSimulationSnapshotMessage(
      snapshotDocument,
      configuration,
      null,
    );

    render(
      <SimulationDebugPanel
        configuration={configuration}
        snapshot={snapshot}
      />,
    );

    expect(
      screen.getByRole('table', { name: 'Public configuration' }),
    ).toBeVisible();
    const metadataTable = screen.getByRole('table', {
      name: 'Snapshot protocol metadata',
    });
    expect(within(metadataTable).getByText('1.0')).toBeVisible();
    expect(
      within(metadataTable).getByText(
        'blob-royale://protocol/v1/snapshot-message',
      ),
    ).toBeVisible();
    expect(
      within(metadataTable).getByText(snapshot.meta.request_id),
    ).toBeVisible();
    expect(
      within(metadataTable).getByText(snapshot.meta.sent_at_utc),
    ).toBeVisible();
    const playersTable = screen.getByRole('table', {
      name: `Players — showing ${DEBUG_PLAYER_ROW_LIMIT} of ${DEBUG_PLAYER_ROW_LIMIT + 25}`,
    });
    expect(within(playersTable).getAllByRole('row')).toHaveLength(
      DEBUG_PLAYER_ROW_LIMIT + 1,
    );
  });
});
