import { render, screen, within } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { SimulationDebugPanel } from './SimulationDebugPanel';
import { DEBUG_ENTITY_ROW_LIMIT } from './simulationConstants';
import { configurationResponseExample } from './fixtures/protocolV1Examples';
import { snapshotDocument } from './fixtures/sessionFrames';
import { validateSessionSnapshotMessage } from './sessionProtocolValidation';
import { validateSimulationConfigurationResponse } from './simulationProtocolValidation';
import type { SimulationSessionIdentity } from './useSimulationConnection';

const configuration = validateSimulationConfigurationResponse(
  structuredClone(configurationResponseExample),
  configurationResponseExample.meta.request_id,
).data;

const session: SimulationSessionIdentity = Object.freeze({
  acceptedCommandKinds: ['set_thrust'] as const,
  controllerId: 3,
  displayName: 'Cole Shaffer',
  firstEntityId: 7,
  map: 'arena-960x640',
  mode: 'royale',
});

describe('SimulationDebugPanel', () => {
  it('renders semantic table captions and bounds entity rows', () => {
    const document = snapshotDocument();
    const body = {
      acceleration: { x: 0, y: 0 },
      collision_layer: 1,
      collision_mask: 3,
      is_static: false,
      mass: 1,
      position: { x: 240, y: 300 },
      radius: 10,
      velocity: { x: 0, y: 0 },
    };
    document.data.entities = Array.from(
      { length: DEBUG_ENTITY_ROW_LIMIT + 25 },
      (_, entityIndex) => ({
        entity_id: entityIndex + 1,
        components: { physics_body: structuredClone(body) },
      }),
    );
    const snapshot = validateSessionSnapshotMessage(document, {
      messageSequence: 1,
      requestId: document.meta.request_id,
      tickSequence: null,
    });

    render(
      <SimulationDebugPanel
        configuration={configuration}
        session={session}
        snapshot={snapshot}
      />,
    );

    expect(
      screen.getByRole('table', { name: 'Public configuration' }),
    ).toBeVisible();
    const identityTable = screen.getByRole('table', {
      name: 'Session identity',
    });
    expect(within(identityTable).getByText('Cole Shaffer')).toBeVisible();
    expect(
      within(identityTable).getByText('royale / arena-960x640'),
    ).toBeVisible();
    expect(within(identityTable).getByText('set_thrust')).toBeVisible();

    const metadataTable = screen.getByRole('table', {
      name: 'Session protocol metadata',
    });
    expect(within(metadataTable).getByText('2.0')).toBeVisible();
    expect(
      within(metadataTable).getByText(
        'blob-royale://protocol/v2/snapshot-message',
      ),
    ).toBeVisible();
    expect(
      within(metadataTable).getByText(snapshot.meta.request_id),
    ).toBeVisible();
    expect(
      within(metadataTable).getByText(snapshot.meta.sent_at_utc),
    ).toBeVisible();

    const matchTable = screen.getByRole('table', { name: 'Match section' });
    expect(within(matchTable).getByText('running')).toBeVisible();
    expect(
      within(matchTable).getByText(
        'blob-royale://protocol/v2/mode-state/royale',
      ),
    ).toBeVisible();

    const entitiesTable = screen.getByRole('table', {
      name: `Entities — showing ${DEBUG_ENTITY_ROW_LIMIT} of ${DEBUG_ENTITY_ROW_LIMIT + 25}`,
    });
    expect(within(entitiesTable).getAllByRole('row')).toHaveLength(
      DEBUG_ENTITY_ROW_LIMIT + 1,
    );
  });

  it('reports awaiting values before the first session frame', () => {
    render(
      <SimulationDebugPanel
        configuration={configuration}
        session={null}
        snapshot={null}
      />,
    );

    const identityTable = screen.getByRole('table', {
      name: 'Session identity',
    });
    expect(within(identityTable).getAllByText('Awaiting frame')).toHaveLength(
      4,
    );
    expect(
      screen.getByRole('table', { name: 'Entities — showing 0 of 0' }),
    ).toBeVisible();
  });
});
