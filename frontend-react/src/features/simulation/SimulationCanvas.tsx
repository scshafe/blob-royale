import { useEffect, useId, useRef } from 'react';

import {
  CANVAS_MAX_HEIGHT_PIXELS,
  CANVAS_MAX_WIDTH_PIXELS,
  SNAPSHOT_PLAYER_LIMIT,
} from './simulationConstants';
import type {
  SimulationConfiguration,
  SimulationWorldSnapshot,
} from './simulationProtocolTypes';

export interface SimulationCanvasProps {
  readonly configuration: SimulationConfiguration;
  readonly snapshot: SimulationWorldSnapshot | null;
}

interface CanvasViewport {
  readonly height: number;
  readonly width: number;
}

function calculateCanvasViewport(
  configuration: SimulationConfiguration,
): CanvasViewport {
  const worldWidth = configuration.world.width_world_units;
  const worldHeight = configuration.world.height_world_units;
  const scale = Math.min(
    CANVAS_MAX_WIDTH_PIXELS / worldWidth,
    CANVAS_MAX_HEIGHT_PIXELS / worldHeight,
  );

  return Object.freeze({
    height: Math.max(
      1,
      Math.min(CANVAS_MAX_HEIGHT_PIXELS, Math.floor(worldHeight * scale)),
    ),
    width: Math.max(
      1,
      Math.min(CANVAS_MAX_WIDTH_PIXELS, Math.floor(worldWidth * scale)),
    ),
  });
}

function playerFillColor(entityId: number): string {
  return `hsl(${(entityId * 137.508) % 360} 72% 48%)`;
}

/** Draws only validated immutable values and never owns transport state. */
export function SimulationCanvas({
  configuration,
  snapshot,
}: SimulationCanvasProps) {
  const canvasReference = useRef<HTMLCanvasElement>(null);
  const descriptionId = useId();
  const viewport = calculateCanvasViewport(configuration);

  useEffect(() => {
    const canvas = canvasReference.current;
    const context = canvas?.getContext('2d');
    if (canvas === null || context === null || context === undefined) {
      return;
    }

    context.clearRect(0, 0, viewport.width, viewport.height);
    context.fillStyle = '#f8fafc';
    context.fillRect(0, 0, viewport.width, viewport.height);
    context.strokeStyle = '#334155';
    context.strokeRect(0, 0, viewport.width, viewport.height);

    if (snapshot === null) {
      return;
    }

    const horizontalScale =
      viewport.width / configuration.world.width_world_units;
    const verticalScale =
      viewport.height / configuration.world.height_world_units;
    const radiusPixels = Math.max(
      1,
      configuration.world.player_radius_world_units *
        Math.min(horizontalScale, verticalScale),
    );
    const renderedPlayerCount = Math.min(
      snapshot.players.length,
      configuration.presentation.snapshot_player_limit,
      SNAPSHOT_PLAYER_LIMIT,
    );

    for (
      let playerIndex = 0;
      playerIndex < renderedPlayerCount;
      playerIndex += 1
    ) {
      const player = snapshot.players[playerIndex];
      if (player === undefined) {
        break;
      }
      context.beginPath();
      context.arc(
        player.position.x * horizontalScale,
        player.position.y * verticalScale,
        radiusPixels,
        0,
        2 * Math.PI,
      );
      context.fillStyle = playerFillColor(player.entity_id);
      context.fill();
      context.strokeStyle = '#0f172a';
      context.stroke();
    }
  }, [configuration, snapshot, viewport.height, viewport.width]);

  const snapshotDescription =
    snapshot === null
      ? 'Waiting for the first complete world snapshot.'
      : `Complete tick ${snapshot.tick_sequence} with ${snapshot.players.length} players.`;

  return (
    <figure className="SimulationCanvas">
      <canvas
        aria-describedby={descriptionId}
        aria-label="Blob Royale simulation world"
        height={viewport.height}
        ref={canvasReference}
        role="img"
        width={viewport.width}
      />
      <figcaption id={descriptionId}>{snapshotDescription}</figcaption>
    </figure>
  );
}
