import { render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { SimulationCanvas } from './SimulationCanvas';
import {
  CANVAS_MAX_HEIGHT_PIXELS,
  CANVAS_MAX_WIDTH_PIXELS,
  SNAPSHOT_PLAYER_LIMIT,
} from './simulationConstants';
import type { SimulationWorldSnapshot } from './simulationProtocolTypes';
import { configurationResponseExample } from './fixtures/protocolV1Examples';
import { validateSimulationConfigurationResponse } from './simulationProtocolValidation';

const configuration = validateSimulationConfigurationResponse(
  structuredClone(configurationResponseExample),
  configurationResponseExample.meta.request_id,
).data;

function createCanvasContext(): {
  readonly arc: ReturnType<typeof vi.fn>;
  readonly context: CanvasRenderingContext2D;
} {
  const arc = vi.fn();
  const context = {
    arc,
    beginPath: vi.fn(),
    clearRect: vi.fn(),
    fill: vi.fn(),
    fillRect: vi.fn(),
    fillStyle: '',
    stroke: vi.fn(),
    strokeRect: vi.fn(),
    strokeStyle: '',
  } as unknown as CanvasRenderingContext2D;
  return { arc, context };
}

afterEach(() => {
  vi.restoreAllMocks();
});

describe('SimulationCanvas', () => {
  it('bounds its backing buffer and the number of drawn players', () => {
    const { arc, context } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );
    const snapshot: SimulationWorldSnapshot = {
      players: Array.from({ length: 5_000 }, (_, playerIndex) => ({
        acceleration: { x: 0, y: 0 },
        entity_id: playerIndex + 1,
        position: { x: 240, y: 300 },
        velocity: { x: 0, y: 0 },
      })),
      tick_sequence: 1,
    };

    render(
      <SimulationCanvas configuration={configuration} snapshot={snapshot} />,
    );

    const canvas = screen.getByRole('img', {
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
    expect(arc).toHaveBeenCalledTimes(SNAPSHOT_PLAYER_LIMIT);
  });

  it('exposes an accessible waiting description before the first snapshot', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      createCanvasContext().context,
    );

    render(<SimulationCanvas configuration={configuration} snapshot={null} />);

    expect(
      screen.getByText('Waiting for the first complete world snapshot.'),
    ).toBeVisible();
  });
});
