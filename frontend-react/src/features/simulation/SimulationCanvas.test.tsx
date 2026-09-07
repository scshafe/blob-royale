import { render } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { SimulationCanvas } from './SimulationCanvas';
import {
  CANVAS_MAX_HEIGHT_PIXELS,
  CANVAS_MAX_WIDTH_PIXELS,
  SESSION_ENTITY_LIMIT,
} from './simulationConstants';
import type {
  SessionEntitySnapshot,
  SessionWorldSnapshot,
} from './simulationProtocolTypes';
import { configurationResponseExample } from './fixtures/protocolV1Examples';
import { snapshotDocument } from './fixtures/sessionFrames';
import { validateSessionSnapshotMessage } from './sessionProtocolValidation';
import { validateSimulationConfigurationResponse } from './simulationProtocolValidation';

const configuration = validateSimulationConfigurationResponse(
  structuredClone(configurationResponseExample),
  configurationResponseExample.meta.request_id,
).data;

const goldenSnapshot = validateSessionSnapshotMessage(snapshotDocument(), {
  messageSequence: 1,
  requestId: snapshotDocument().meta.request_id,
  tickSequence: null,
}).data;

// Return type inferred deliberately: an erased `Mock` field would lose the recorded argument types
// and make every assertion on a draw call an unchecked `any`.
function createCanvasContext() {
  const arc = vi.fn();
  const fillText = vi.fn((text: string, x: number, y: number) => {
    void text;
    void x;
    void y;
  });
  const assignments: Record<string, unknown[]> = {
    fillStyle: [],
    lineWidth: [],
    strokeStyle: [],
  };
  const context = {
    arc,
    beginPath: vi.fn(),
    clearRect: vi.fn(),
    fill: vi.fn(),
    fillRect: vi.fn(),
    fillText,
    font: '',
    stroke: vi.fn(),
    strokeRect: vi.fn(),
    textAlign: '',
    textBaseline: '',
    set fillStyle(value: unknown) {
      assignments.fillStyle?.push(value);
    },
    set lineWidth(value: unknown) {
      assignments.lineWidth?.push(value);
    },
    set strokeStyle(value: unknown) {
      assignments.strokeStyle?.push(value);
    },
  } as unknown as CanvasRenderingContext2D;
  return { arc, assignments, context, fillText };
}

function bodyEntity(entityId: number): SessionEntitySnapshot {
  return {
    entity_id: entityId,
    components: {
      physics_body: {
        acceleration: { x: 0, y: 0 },
        collision_layer: 1,
        collision_mask: 3,
        is_static: false,
        mass: 1,
        position: { x: 240, y: 300 },
        radius: 10,
        velocity: { x: 0, y: 0 },
      },
    },
  };
}

afterEach(() => {
  vi.restoreAllMocks();
});

describe('SimulationCanvas', () => {
  it('bounds its backing buffer and the number of drawn entities', () => {
    const { arc, context } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );
    const snapshot: SessionWorldSnapshot = {
      ...goldenSnapshot,
      entities: Array.from({ length: 5_000 }, (_, entityIndex) =>
        bodyEntity(entityIndex + 1),
      ),
    };

    const view = render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={null}
        snapshot={snapshot}
      />,
    );

    const canvas = view.getByRole('img', {
      name: 'Blob Royale simulation world',
    });
    expect(canvas).toHaveAttribute(
      'width',
      expect.stringMatching(/^[1-9][0-9]*$/),
    );
    expect((canvas as HTMLCanvasElement).width).toBeLessThanOrEqual(
      CANVAS_MAX_WIDTH_PIXELS,
    );
    expect((canvas as HTMLCanvasElement).height).toBeLessThanOrEqual(
      CANVAS_MAX_HEIGHT_PIXELS,
    );
    expect(arc).toHaveBeenCalledTimes(SESSION_ENTITY_LIMIT);
  });

  it('draws every registered visual kind and highlights the own body', () => {
    const { arc, assignments, context, fillText } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );

    const view = render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={7}
        snapshot={goldenSnapshot}
      />,
    );

    // One static obstacle, two player bodies, and the zone circle.
    expect(arc).toHaveBeenCalledTimes(4);
    expect(fillText.mock.calls.map((call) => call[0])).toEqual([
      'Cole Shaffer',
      'wanderer-1',
    ]);
    expect(assignments.lineWidth).toContain(4);
    expect(assignments.strokeStyle).toContain('#f8fafc');
    expect(view.getByRole('img')).toHaveAccessibleDescription(
      'Complete tick 12904 with 4 entities and 2 players.',
    );
  });

  it('exposes an accessible waiting description before the first snapshot', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      createCanvasContext().context,
    );

    const view = render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={null}
        snapshot={null}
      />,
    );

    expect(
      view.getByText('Waiting for the first complete world snapshot.'),
    ).toBeVisible();
  });
});
