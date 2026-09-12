import {
  useCallback,
  useEffect,
  useId,
  useMemo,
  useRef,
  type PointerEvent,
} from 'react';

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
import type { ThrustAimObservation } from './useThrustInput';

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
  readonly ownBodyPosition: WorldPoint | null;
  readonly onAimObservation: (observation: ThrustAimObservation | null) => void;
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
  ownBodyPosition,
  onAimObservation,
}: SimulationCanvasProps) {
  const canvasReference = useRef<HTMLCanvasElement>(null);
  const dragReference = useRef<{
    pointerId: number;
    x: number;
    y: number;
  } | null>(null);
  const pointerReference = useRef<{
    pointerId: number;
    clientX: number;
    clientY: number;
  } | null>(null);
  const expectedCaptureReleaseReference = useRef<number | null>(null);
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
  // Drawing, inverse panning, and aim share one logical-CSS transform. Zero-sized hidden views
  // have no projection and publish no usable aim rather than substituting a rectangle.
  const projection = useMemo(
    () =>
      viewport.width > 0 && viewport.height > 0
        ? createWorldProjection(
            { x: camera.center.x, y: camera.center.y },
            { width: viewport.width, height: viewport.height },
            CAMERA_PIXELS_PER_WORLD_UNIT,
          )
        : null,
    [camera.center.x, camera.center.y, viewport.width, viewport.height],
  );
  const bodyX = ownBodyPosition?.x ?? null;
  const bodyY = ownBodyPosition?.y ?? null;
  const publishAim = useCallback(() => {
    const canvas = canvasReference.current;
    const pointer = pointerReference.current;
    if (
      canvas === null ||
      pointer === null ||
      projection === null ||
      bodyX === null ||
      bodyY === null
    ) {
      onAimObservation(null);
      return;
    }
    const rectangle = canvas.getBoundingClientRect();
    if (
      rectangle.width <= 0 ||
      rectangle.height <= 0 ||
      pointer.clientX < rectangle.left ||
      pointer.clientX > rectangle.right ||
      pointer.clientY < rectangle.top ||
      pointer.clientY > rectangle.bottom
    ) {
      onAimObservation(null);
      return;
    }
    onAimObservation({
      pointer: {
        x:
          ((pointer.clientX - rectangle.left) * viewport.width) /
          rectangle.width,
        y:
          ((pointer.clientY - rectangle.top) * viewport.height) /
          rectangle.height,
      },
      projectedBodyCenter: projectWorldPoint(projection, {
        x: bodyX,
        y: bodyY,
      }),
      cameraGestureActive: dragReference.current !== null,
    });
  }, [
    bodyX,
    bodyY,
    onAimObservation,
    projection,
    viewport.width,
    viewport.height,
  ]);

  // A stationary mouse still aims relative to this frame's body/camera and actual DOM position.
  // Passive delivery follows the input owner's layout-phase identity reset. Repeated identical
  // observations do not update its direction, so this cannot create a rendering feedback loop.
  useEffect(() => {
    publishAim();
  });
  useEffect(() => {
    window.addEventListener('scroll', publishAim, true);
    window.addEventListener('resize', publishAim);
    return () => {
      window.removeEventListener('scroll', publishAim, true);
      window.removeEventListener('resize', publishAim);
    };
  }, [publishAim]);
  useEffect(() => () => onAimObservation(null), [onAimObservation]);

  function observePointer(event: PointerEvent<HTMLCanvasElement>) {
    if (!event.isPrimary || event.pointerType !== 'mouse') return;
    pointerReference.current = {
      pointerId: event.pointerId,
      clientX: event.clientX,
      clientY: event.clientY,
    };
    publishAim();
  }

  function cancelPointer(event: PointerEvent<HTMLCanvasElement>) {
    if (pointerReference.current?.pointerId === event.pointerId) {
      pointerReference.current = null;
      onAimObservation(null);
    }
  }

  function finishDrag(event: PointerEvent<HTMLCanvasElement>) {
    if (dragReference.current?.pointerId !== event.pointerId) return;
    dragReference.current = null;
    if (event.currentTarget.hasPointerCapture(event.pointerId)) {
      // A normal pointerup relinquishes capture too. That browser notification is not an
      // unexpected pointer loss: keep its usable aim, with the gesture's go already cancelled.
      expectedCaptureReleaseReference.current = event.pointerId;
      event.currentTarget.releasePointerCapture(event.pointerId);
    }
    publishAim();
  }

  useEffect(() => {
    const canvas = canvasReference.current;
    function cancelDrag() {
      const drag = dragReference.current;
      dragReference.current = null;
      if (drag !== null && canvas?.hasPointerCapture(drag.pointerId)) {
        canvas.releasePointerCapture(drag.pointerId);
      }
      pointerReference.current = null;
      onAimObservation(null);
    }
    if (camera.mode !== 'manual' && dragReference.current !== null)
      cancelDrag();
    window.addEventListener('blur', cancelDrag);
    return () => {
      window.removeEventListener('blur', cancelDrag);
      cancelDrag();
    };
  }, [camera.mode, onAimObservation]);

  useEffect(() => {
    const canvas = canvasReference.current;
    const context = canvas?.getContext('2d');
    if (canvas === null || context === null || context === undefined) {
      return;
    }

    context.setTransform(1, 0, 0, 1, 0, 0);
    context.clearRect(0, 0, backingWidth, backingHeight);
    if (projection === null) return;
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
      // The tick every ability window on the wire is expressed against, read from the same snapshot
      // the accessible caption below reports, so a mark drawn on a body and the sentence describing
      // the frame can never be a tick apart. The frame type admits `null` for the frames drawn
      // before the first snapshot arrives; this line runs only past the `snapshot === null` return,
      // so what a renderer receives here is always the committed tick.
      tickSequence: snapshot.tick_sequence,
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
    projection,
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
          tabIndex={0}
          onPointerEnter={observePointer}
          onPointerLeave={cancelPointer}
          onPointerDown={(event) => {
            if (event.isPrimary && event.button === 0)
              event.currentTarget.focus({ preventScroll: true });
            if (
              camera.mode !== 'manual' ||
              event.button !== 0 ||
              !event.isPrimary ||
              dragReference.current !== null
            ) {
              observePointer(event);
              return;
            }
            event.preventDefault();
            expectedCaptureReleaseReference.current = null;
            event.currentTarget.setPointerCapture(event.pointerId);
            dragReference.current = {
              pointerId: event.pointerId,
              x: event.clientX,
              y: event.clientY,
            };
            observePointer(event);
            publishAim();
          }}
          onPointerMove={(event) => {
            const drag = dragReference.current;
            if (drag === null || drag.pointerId === event.pointerId)
              observePointer(event);
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
            if (projection !== null) {
              onPan(unprojectCanvasOffset(projection, offset));
            }
          }}
          onPointerUp={(event) => {
            observePointer(event);
            finishDrag(event);
          }}
          onPointerCancel={(event) => {
            finishDrag(event);
            cancelPointer(event);
          }}
          onLostPointerCapture={(event) => {
            if (expectedCaptureReleaseReference.current === event.pointerId) {
              expectedCaptureReleaseReference.current = null;
              publishAim();
              return;
            }
            finishDrag(event);
            expectedCaptureReleaseReference.current = null;
            cancelPointer(event);
          }}
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
