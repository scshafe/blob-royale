import { render, screen, within } from '@testing-library/react';
import { describe, expect, it } from 'vitest';

import { SimulationDebugPanel } from './SimulationDebugPanel';
import { DEBUG_ENTITY_ROW_LIMIT } from './simulationConstants';
import { configurationResponseExample } from './fixtures/protocolV1Examples';
import {
  legacyNpcCatalogue,
  snapshotDocument,
  welcomeDocument,
} from './fixtures/sessionFrames';
import {
  SUPPORTED_PROTOCOL_VERSION,
  validateSessionSnapshotMessage,
} from './sessionProtocolValidation';
import { validateSimulationConfigurationResponse } from './simulationProtocolValidation';
import type { SimulationSessionIdentity } from './useSimulationConnection';
import { solidTerrain } from './fixtures/terrainFrames';

const configuration = validateSimulationConfigurationResponse(
  structuredClone(configurationResponseExample),
  configurationResponseExample.meta.request_id,
).data;

const session: SimulationSessionIdentity = Object.freeze({
  acceptedCommandKinds: ['set_thrust'] as const,
  controllerId: 3,
  displayName: 'Cole Shaffer',
  firstEntityId: 7,
  lobbyId: 1,
  map: 'arena-960x640',
  mode: 'royale',
  movementTuningMinimumIntervalMilliseconds:
    welcomeDocument().data.movement_tuning_minimum_interval_milliseconds,
  npcCatalogue: legacyNpcCatalogue,
  seatCountMaximum: 32,
  terrain: solidTerrain,
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
      npcCatalogue: legacyNpcCatalogue,
      terrain: solidTerrain,
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
    expect(within(identityTable).getByText('1 (up to 32 seats)')).toBeVisible();
    expect(within(identityTable).getByText('set_thrust')).toBeVisible();

    const metadataTable = screen.getByRole('table', {
      name: 'Session protocol metadata',
    });
    // Read from the supported-version constant rather than a literal. The panel's job is to show
    // the version it decoded, not a particular number, and hardcoding one turns every protocol
    // minor into an unrelated test edit -- which is exactly what happened when the server published
    // `lethal_on_contact` and the schema set went to 2.1.
    expect(
      within(metadataTable).getByText(SUPPORTED_PROTOCOL_VERSION),
    ).toBeVisible();
    expect(
      within(metadataTable).getByText(
        'blob-royale://protocol/v3/snapshot-message',
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
        'blob-royale://protocol/v3/mode-state/royale',
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
    // Display name, controller, first entity, mode and map, and since 2.4 the room.
    expect(within(identityTable).getAllByText('Awaiting frame')).toHaveLength(
      5,
    );
    expect(
      screen.getByRole('table', { name: 'Entities — showing 0 of 0' }),
    ).toBeVisible();
  });
});
