import { useEffect, useId, useRef, type PointerEvent } from 'react';

import {
  CAMERA_PIXELS_PER_WORLD_UNIT,
  SESSION_ENTITY_LIMIT,
} from './simulationConstants';
import type {
  SimulationConfiguration,
  SessionWorldSnapshot,
  SessionTerrain,
} from './simulationProtocolTypes';
import { visualEntityRenderers } from './rendering/entityRendererRegistry';
import { modeStateRendererRegistry } from './rendering/modeStateRendererRegistry';
import { drawTerrain } from './rendering/terrainRenderer';
import { countAlivePlayers, eliminationGraceTicks } from './sessionSelectors';
import { useCanvasViewport } from './useCanvasViewport';
import {
  createWorldProjection,
  projectWorldPoint,
  projectWorldDistance,
  unprojectCanvasOffset,
  type WorldPoint,
} from './rendering/worldProjection';

export interface SimulationCanvasProps {
  readonly configuration: SimulationConfiguration;
  readonly ownEntityId: number | null;
  readonly snapshot: SessionWorldSnapshot | null;
  readonly terrain: SessionTerrain | null;
  readonly camera: {
    readonly mode: 'follow' | 'manual';
    readonly center: WorldPoint;
  };
  readonly onPan: (offset: WorldPoint) => void;
}

/**
 * Draws only validated immutable values and never owns transport state. It names no component kind:
 * welcome terrain supplies the bottom layer, followed by mode objectives once per frame and
 * `entityRendererRegistry` across entity layers. New kinds add registrations, not canvas branches.
 */
export function SimulationCanvas({
  configuration,
  ownEntityId,
  snapshot,
  terrain,
  camera,
  onPan,
}: SimulationCanvasProps) {
  const canvasReference = useRef<HTMLCanvasElement>(null);
  const dragReference = useRef<{
    pointerId: number;
    x: number;
    y: number;
  } | null>(null);
  const descriptionId = useId();
  const { containerReference, viewport } = useCanvasViewport();
  const backingWidth = Math.max(
    1,
    Math.round(viewport.width * viewport.pixelRatio),
  );
  const backingHeight = Math.max(
    1,
    Math.round(viewport.height * viewport.pixelRatio),
  );

  function finishDrag(event: PointerEvent<HTMLCanvasElement>) {
    if (dragReference.current?.pointerId !== event.pointerId) return;
    dragReference.current = null;
    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      event.currentTarget.releasePointerCapture(event.pointerId);
    }
  }

  useEffect(() => {
    const canvas = canvasReference.current;
    function cancelDrag() {
      const drag = dragReference.current;
      dragReference.current = null;
      if (drag !== null && canvas?.hasPointerCapture(drag.pointerId)) {
        canvas.releasePointerCapture(drag.pointerId);
      }
    }
    if (camera.mode !== 'manual') cancelDrag();
    window.addEventListener('blur', cancelDrag);
    return () => {
      window.removeEventListener('blur', cancelDrag);
      cancelDrag();
    };
  }, [camera.mode]);

  useEffect(() => {
    const canvas = canvasReference.current;
    const context = canvas?.getContext('2d');
    if (canvas === null || context === null || context === undefined) {
      return;
    }

    context.setTransform(1, 0, 0, 1, 0, 0);
    context.clearRect(0, 0, backingWidth, backingHeight);
    if (viewport.width === 0 || viewport.height === 0) return;
    // The inverse CSS display scaling cancels these backing-buffer factors. World projection
    // remains uniform in CSS pixels even when a fractional DPR rounds the two buffer dimensions.
    context.setTransform(
      backingWidth / viewport.width,
      0,
      0,
      backingHeight / viewport.height,
      0,
      0,
    );
    context.fillStyle = '#e2e8f0';
    context.fillRect(0, 0, viewport.width, viewport.height);
    const projection = createWorldProjection(
      camera.center,
      { width: viewport.width, height: viewport.height },
      CAMERA_PIXELS_PER_WORLD_UNIT,
    );
    const origin = projectWorldPoint(projection, { x: 0, y: 0 });
    const worldWidth = projectWorldDistance(
      projection,
      configuration.world.width_world_units,
    );
    const worldHeight = projectWorldDistance(
      projection,
      configuration.world.height_world_units,
    );
    if (terrain !== null)
      drawTerrain(terrain, { projection, surface: context });
    context.strokeStyle = '#334155';
    context.lineWidth = 1;
    context.strokeRect(origin.x, origin.y, worldWidth, worldHeight);

    if (snapshot === null) {
      return;
    }

    const frame = {
      // Resolved once per frame from the match section the snapshot already carries, so every
      // renderer that draws danger measures it against the same denominator the HUD counts down.
      eliminationGraceTicks: eliminationGraceTicks(snapshot.match),
      ownEntityId,
      projection,
      surface: context,
    };
    const renderedEntities = snapshot.entities.slice(0, SESSION_ENTITY_LIMIT);

    const modeStateRenderer =
      modeStateRendererRegistry[snapshot.match.mode_state.schema_id];
    if (modeStateRenderer.renders) {
      modeStateRenderer.drawModeState(snapshot.match, frame);
    }

    for (const renderer of visualEntityRenderers()) {
      for (const entity of renderedEntities) {
        renderer.drawEntity(entity, frame);
      }
    }
  }, [
    configuration,
    ownEntityId,
    snapshot,
    terrain,
    camera.center,
    viewport.height,
    viewport.width,
    backingHeight,
    backingWidth,
  ]);

  const snapshotDescription =
    snapshot === null
      ? 'Waiting for the first complete world snapshot.'
      : `Complete tick ${snapshot.tick_sequence} with ${snapshot.entities.length} entities and ${countAlivePlayers(snapshot.entities)} players.`;

  return (
    <figure className="SimulationCanvas">
      <div className="SimulationViewport" ref={containerReference}>
        <canvas
          aria-describedby={descriptionId}
          aria-label="Blob Royale simulation world"
          height={backingHeight}
          className={
            camera.mode === 'manual' ? 'ManualCameraCanvas' : undefined
          }
          data-camera-mode={camera.mode}
          data-camera-center-x={camera.center.x}
          data-camera-center-y={camera.center.y}
          onPointerDown={(event) => {
            if (
              camera.mode !== 'manual' ||
              event.button !== 0 ||
              !event.isPrimary ||
              dragReference.current !== null
            )
              return;
            event.preventDefault();
            event.currentTarget.setPointerCapture(event.pointerId);
            dragReference.current = {
              pointerId: event.pointerId,
              x: event.clientX,
              y: event.clientY,
            };
          }}
          onPointerMove={(event) => {
            const drag = dragReference.current;
            if (
              camera.mode !== 'manual' ||
              drag === null ||
              drag.pointerId !== event.pointerId
            )
              return;
            if ((event.buttons & 1) === 0) {
              finishDrag(event);
              return;
            }
            event.preventDefault();
            const offset = {
              x: drag.x - event.clientX,
              y: drag.y - event.clientY,
            };
            dragReference.current = {
              pointerId: drag.pointerId,
              x: event.clientX,
              y: event.clientY,
            };
            if (viewport.width > 0 && viewport.height > 0) {
              onPan(
                unprojectCanvasOffset(
                  createWorldProjection(
                    camera.center,
                    viewport,
                    CAMERA_PIXELS_PER_WORLD_UNIT,
                  ),
                  offset,
                ),
              );
            }
          }}
          onPointerUp={finishDrag}
          onPointerCancel={finishDrag}
          onLostPointerCapture={finishDrag}
          ref={canvasReference}
          role="img"
          style={{ width: viewport.width, height: viewport.height }}
          width={backingWidth}
        />
      </div>
      <figcaption id={descriptionId}>{snapshotDescription}</figcaption>
    </figure>
  );
}
