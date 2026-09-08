import { useEffect, useId, useRef } from 'react';

import {
  CANVAS_MAX_HEIGHT_PIXELS,
  CANVAS_MAX_WIDTH_PIXELS,
  SESSION_ENTITY_LIMIT,
} from './simulationConstants';
import type {
  SimulationConfiguration,
  SessionWorldSnapshot,
} from './simulationProtocolTypes';
import { visualEntityRenderers } from './rendering/entityRendererRegistry';
import { countAlivePlayers, eliminationGraceTicks } from './sessionSelectors';

export interface SimulationCanvasProps {
  readonly configuration: SimulationConfiguration;
  readonly ownEntityId: number | null;
  readonly snapshot: SessionWorldSnapshot | null;
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

/**
 * Draws only validated immutable values and never owns transport state. It names no component kind:
 * every pixel comes from `entityRendererRegistry`, so a new kind is a renderer file plus one
 * registration and this file does not change.
 */
export function SimulationCanvas({
  configuration,
  ownEntityId,
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
    context.lineWidth = 1;
    context.strokeRect(0, 0, viewport.width, viewport.height);

    if (snapshot === null) {
      return;
    }

    const frame = {
      // Resolved once per frame from the match section the snapshot already carries, so every
      // renderer that draws danger measures it against the same denominator the HUD counts down.
      eliminationGraceTicks: eliminationGraceTicks(snapshot.match),
      ownEntityId,
      projection: {
        horizontalScale: viewport.width / configuration.world.width_world_units,
        verticalScale: viewport.height / configuration.world.height_world_units,
      },
      surface: context,
    };
    const renderedEntities = snapshot.entities.slice(0, SESSION_ENTITY_LIMIT);

    for (const renderer of visualEntityRenderers()) {
      for (const entity of renderedEntities) {
        renderer.drawEntity(entity, frame);
      }
    }
  }, [configuration, ownEntityId, snapshot, viewport.height, viewport.width]);

  const snapshotDescription =
    snapshot === null
      ? 'Waiting for the first complete world snapshot.'
      : `Complete tick ${snapshot.tick_sequence} with ${snapshot.entities.length} entities and ${countAlivePlayers(snapshot.entities)} players.`;

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
