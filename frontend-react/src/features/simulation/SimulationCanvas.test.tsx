import { act, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import {
  SimulationCanvas as CameraCanvas,
  type SimulationCanvasProps,
} from './SimulationCanvas';
import {
  CanvasViewportObserver,
  canvasDensityQuery,
} from './fixtures/canvasViewportObserver';
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
import {
  raceTerrain,
  solidTerrain,
  validatedTerrain,
} from './fixtures/terrainFrames';
import {
  raceSnapshotDocument,
  snapshotDocument,
} from './fixtures/sessionFrames';
import { validateSessionSnapshotMessage } from './sessionProtocolValidation';
import { validateSimulationConfigurationResponse } from './simulationProtocolValidation';
import {
  EXPOSED_OWN_RING_COLOR,
  EXPOSED_PEER_RING_COLOR,
} from './rendering/zoneExposureRenderer';
import { findEntityById } from './sessionSelectors';
import type { ThrustAimObservation } from './useThrustInput';
import {
  aimPointer,
  installCanvasAimSurface,
} from './fixtures/canvasAimObservations';
import type { WorldPoint } from './rendering/worldProjection';

const configuration = validateSimulationConfigurationResponse(
  structuredClone(configurationResponseExample),
  configurationResponseExample.meta.request_id,
).data;

const goldenSnapshot = validateSessionSnapshotMessage(snapshotDocument(), {
  messageSequence: 1,
  requestId: snapshotDocument().meta.request_id,
  tickSequence: null,
  terrain: solidTerrain,
}).data;

function SimulationCanvas(
  props: Omit<
    SimulationCanvasProps,
    'camera' | 'onPan' | 'terrain' | 'ownBodyPosition' | 'onAimObservation'
  > &
    Partial<
      Pick<
        SimulationCanvasProps,
        'camera' | 'onPan' | 'terrain' | 'ownBodyPosition' | 'onAimObservation'
      >
    >,
) {
  return (
    <CameraCanvas
      camera={{
        mode: 'manual',
        center: {
          x: props.configuration.world.width_world_units / 2,
          y: props.configuration.world.height_world_units / 2,
        },
      }}
      onPan={() => undefined}
      onAimObservation={ignoreAimObservation}
      ownBodyPosition={
        findEntityById(props.snapshot?.entities ?? [], props.ownEntityId)
          ?.components.physics_body?.position ?? null
      }
      terrain={solidTerrain}
      {...props}
    />
  );
}

function ignoreAimObservation(_observation: ThrustAimObservation | null): void {
  void _observation;
}

// Return type inferred deliberately: an erased `Mock` field would lose the recorded argument types
// and make every assertion on a draw call an unchecked `any`.
function createCanvasContext() {
  const arc = vi.fn();
  const moveTo = vi.fn();
  const lineTo = vi.fn();
  const restore = vi.fn();
  const setTransform = vi.fn();
  const strokeRect = vi.fn();
  const fillRect = vi.fn();
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
    clip: vi.fn(),
    rect: vi.fn(),
    clearRect: vi.fn(),
    fill: vi.fn(),
    fillRect,
    fillText,
    font: '',
    lineTo,
    moveTo,
    restore,
    save: vi.fn(),
    scale: vi.fn(),
    setTransform,
    stroke: vi.fn(),
    strokeRect,
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
  return {
    arc,
    assignments,
    context,
    fillRect,
    fillText,
    lineTo,
    moveTo,
    restore,
    setTransform,
    strokeRect,
  };
}

function bodyEntity(
  entityId: number,
  position: WorldPoint = { x: 240, y: 300 },
): SessionEntitySnapshot {
  return {
    entity_id: entityId,
    components: {
      physics_body: {
        acceleration: { x: 0, y: 0 },
        collision_layer: 1,
        collision_mask: 3,
        is_static: false,
        mass: 1,
        position,
        radius: 10,
        velocity: { x: 0, y: 0 },
      },
    },
  };
}

function exposedBodyEntity(
  entityId: number,
  outsideTicks: number,
): SessionEntitySnapshot {
  const entity = bodyEntity(entityId);
  return {
    entity_id: entity.entity_id,
    components: {
      ...entity.components,
      zone_exposure: { outside_ticks: outsideTicks },
    },
  };
}

afterEach(() => {
  vi.restoreAllMocks();
});

describe('SimulationCanvas', () => {
  it('observes the same projected body center it draws in CSS pixels without taking focus on hover', () => {
    const { context, arc } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );
    const onAimObservation = vi.fn(
      (observation: ThrustAimObservation | null) => {
        void observation;
      },
    );
    render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={21}
        snapshot={{ ...goldenSnapshot, entities: [bodyEntity(21)] }}
        camera={{ mode: 'follow', center: { x: 240, y: 300 } }}
        onAimObservation={onAimObservation}
      />,
    );
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    installCanvasAimSurface(canvas);
    fireEvent.pointerMove(canvas, aimPointer());
    expect(onAimObservation).toHaveBeenLastCalledWith({
      pointer: { x: 580, y: 320 },
      projectedBodyCenter: { x: 480, y: 320 },
      cameraGestureActive: false,
    });
    expect(arc).toHaveBeenLastCalledWith(480, 320, 10, 0, 2 * Math.PI);
    expect(canvas).not.toHaveFocus();
    fireEvent.pointerDown(canvas, aimPointer(680, 370, { buttons: 1 }));
    expect(canvas).toHaveFocus();
  });

  it('refreshes a stationary cursor for body, camera, viewport, fractional density, scroll, and rendered layout changes', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const onAimObservation = vi.fn(
      (observation: ThrustAimObservation | null) => {
        void observation;
      },
    );
    const props = {
      configuration,
      ownEntityId: 21,
      onAimObservation,
      camera: { mode: 'manual' as const, center: { x: 240, y: 300 } },
    };
    const view = render(
      <SimulationCanvas
        {...props}
        snapshot={{ ...goldenSnapshot, entities: [bodyEntity(21)] }}
      />,
    );
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    const surface = installCanvasAimSurface(canvas);
    fireEvent.pointerMove(canvas, aimPointer());
    view.rerender(
      <SimulationCanvas
        {...props}
        snapshot={{
          ...goldenSnapshot,
          entities: [bodyEntity(21, { x: 340, y: 300 })],
        }}
      />,
    );
    expect(onAimObservation.mock.lastCall?.[0]).toEqual({
      pointer: { x: 580, y: 320 },
      projectedBodyCenter: { x: 580, y: 320 },
      cameraGestureActive: false,
    });
    view.rerender(
      <SimulationCanvas
        {...props}
        camera={{ mode: 'manual', center: { x: 340, y: 300 } }}
        snapshot={{
          ...goldenSnapshot,
          entities: [bodyEntity(21, { x: 340, y: 300 })],
        }}
      />,
    );
    expect(onAimObservation.mock.lastCall?.[0]?.projectedBodyCenter).toEqual({
      x: 480,
      y: 320,
    });
    act(() => {
      for (const observer of CanvasViewportObserver.active)
        observer.resize(601);
    });
    expect(onAimObservation.mock.lastCall?.[0]).toEqual({
      pointer: { x: 580, y: 320 },
      projectedBodyCenter: { x: 300.5, y: 200 },
      cameraGestureActive: false,
    });
    vi.stubGlobal('devicePixelRatio', 1.25);
    fireEvent.resize(window);
    expect(canvas).toHaveAttribute('width', '751');
    expect(onAimObservation.mock.lastCall?.[0]).toEqual({
      pointer: { x: 580, y: 320 },
      projectedBodyCenter: { x: 300.5, y: 200 },
      cameraGestureActive: false,
    });
    surface.moveTo(200, 150);
    fireEvent.scroll(window);
    expect(onAimObservation.mock.lastCall?.[0]?.pointer).toEqual({
      x: 480,
      y: 220,
    });
    surface.moveTo(220, 170);
    view.rerender(
      <SimulationCanvas
        {...props}
        camera={{ mode: 'manual', center: { x: 340, y: 300 } }}
        snapshot={{
          ...goldenSnapshot,
          entities: [bodyEntity(21, { x: 340, y: 300 })],
        }}
      />,
    );
    expect(onAimObservation.mock.lastCall?.[0]?.pointer).toEqual({
      x: 460,
      y: 200,
    });
  });

  it('publishes null for departed, cancelled, bodyless, hidden, and unmounted surfaces', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const onAimObservation = vi.fn(
      (observation: ThrustAimObservation | null) => {
        void observation;
      },
    );
    const props = {
      configuration,
      ownEntityId: 21,
      onAimObservation,
      camera: { mode: 'follow' as const, center: { x: 240, y: 300 } },
    };
    const bodySnapshot = { ...goldenSnapshot, entities: [bodyEntity(21)] };
    const view = render(
      <SimulationCanvas {...props} snapshot={bodySnapshot} />,
    );
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    installCanvasAimSurface(canvas);
    fireEvent.pointerMove(canvas, aimPointer());
    fireEvent.pointerLeave(canvas, aimPointer());
    expect(onAimObservation).toHaveBeenLastCalledWith(null);
    fireEvent.pointerMove(canvas, aimPointer());
    fireEvent.pointerCancel(canvas, aimPointer());
    expect(onAimObservation).toHaveBeenLastCalledWith(null);
    fireEvent.pointerMove(canvas, aimPointer());
    view.rerender(
      <SimulationCanvas
        {...props}
        snapshot={{ ...goldenSnapshot, entities: [] }}
      />,
    );
    expect(onAimObservation).toHaveBeenLastCalledWith(null);
    view.rerender(<SimulationCanvas {...props} snapshot={bodySnapshot} />);
    expect(onAimObservation.mock.lastCall?.[0]).not.toBeNull();
    act(() => {
      for (const observer of CanvasViewportObserver.active) observer.resize(0);
    });
    expect(onAimObservation).toHaveBeenLastCalledWith(null);
    act(() => {
      for (const observer of CanvasViewportObserver.active)
        observer.resize(960);
    });
    expect(onAimObservation.mock.lastCall?.[0]).not.toBeNull();
    view.unmount();
    expect(onAimObservation).toHaveBeenLastCalledWith(null);
  });

  it('marks manual dragging before reporting its aim and preserves right-click browser behavior', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(null);
    const onAimObservation = vi.fn(
      (observation: ThrustAimObservation | null) => {
        void observation;
      },
    );
    const onPan = vi.fn((point: WorldPoint) => {
      void point;
    });
    render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={21}
        onAimObservation={onAimObservation}
        onPan={onPan}
        snapshot={{ ...goldenSnapshot, entities: [bodyEntity(21)] }}
      />,
    );
    const canvas = screen.getByRole<HTMLCanvasElement>('img');
    const surface = installCanvasAimSurface(canvas);
    fireEvent.pointerMove(canvas, aimPointer());
    onAimObservation.mockClear();
    fireEvent.pointerDown(canvas, aimPointer(680, 370, { buttons: 1 }));
    expect(
      onAimObservation.mock.calls.every(
        ([value]) => value?.cameraGestureActive === true,
      ),
    ).toBe(true);
    fireEvent.pointerMove(canvas, aimPointer(660, 380, { buttons: 1 }));
    expect(onPan).toHaveBeenLastCalledWith({ x: 20, y: -10 });
    fireEvent.lostPointerCapture(canvas, aimPointer());
    expect(surface.captured.size).toBe(0);
    expect(onAimObservation).toHaveBeenLastCalledWith(null);
    expect(fireEvent.contextMenu(canvas)).toBe(true);
    fireEvent.pointerDown(
      canvas,
      aimPointer(680, 370, { button: 2, buttons: 2 }),
    );
    expect(surface.captured.size).toBe(0);
    expect(onPan).toHaveBeenCalledTimes(1);
  });

  it('does not infer solid ground before terrain arrives in welcome', () => {
    const { context, fillRect } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );
    const view = render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={null}
        snapshot={null}
        terrain={null}
      />,
    );
    expect(fillRect).toHaveBeenCalledTimes(1);
    // Only the outside/void background exists before welcome; configuration is not terrain.
    expect(fillRect).toHaveBeenLastCalledWith(0, 0, 960, 640);
    view.rerender(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={null}
        snapshot={null}
        terrain={solidTerrain}
      />,
    );
    expect(fillRect).toHaveBeenCalledTimes(3);
  });

  it('keeps hole geometry on the same camera projection through fractional DPR and manual movement', () => {
    const { context, arc, setTransform } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );
    const terrain = validatedTerrain({
      ...solidTerrain,
      holes: [{ name: 'pit', center: { x: 240, y: 300 }, radius: 10 }],
    });
    const view = render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={null}
        snapshot={null}
        terrain={terrain}
        camera={{ mode: 'follow', center: { x: 240, y: 300 } }}
      />,
    );
    expect(arc).toHaveBeenLastCalledWith(480, 320, 10, 0, 2 * Math.PI);
    act(() => {
      for (const observer of CanvasViewportObserver.active)
        observer.resize(601);
    });
    vi.stubGlobal('devicePixelRatio', 1.25);
    fireEvent.resize(window);
    expect(setTransform).toHaveBeenLastCalledWith(751 / 601, 0, 0, 1.25, 0, 0);
    expect(arc).toHaveBeenLastCalledWith(300.5, 200, 10, 0, 2 * Math.PI);
    view.rerender(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={null}
        snapshot={null}
        terrain={terrain}
        camera={{ mode: 'manual', center: { x: 400, y: 400 } }}
      />,
    );
    expect(arc).toHaveBeenLastCalledWith(140.5, 100, 10, 0, 2 * Math.PI);
  });

  it('draws welcome terrain once before race gates and bodies, including bodyless frames', () => {
    const { arc, context, lineTo, moveTo, restore } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );
    const document = raceSnapshotDocument();
    const race = validateSessionSnapshotMessage(document, {
      messageSequence: 1,
      requestId: document.meta.request_id,
      tickSequence: null,
      terrain: raceTerrain,
    }).data;
    const view = render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={null}
        snapshot={{ ...race, entities: [bodyEntity(21), bodyEntity(22)] }}
        terrain={raceTerrain}
      />,
    );

    expect(moveTo).toHaveBeenCalledTimes(1);
    expect(lineTo).toHaveBeenCalledTimes(2);
    // Three course gates precede both body discs; restoring the course projection is the boundary.
    expect(arc).toHaveBeenCalledTimes(5);
    expect(restore.mock.invocationCallOrder[0]).toBeLessThan(
      arc.mock.invocationCallOrder[3] ?? 0,
    );

    view.rerender(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={null}
        snapshot={{ ...race, entities: [] }}
        terrain={raceTerrain}
      />,
    );
    expect(moveTo).toHaveBeenCalledTimes(2);
    expect(lineTo).toHaveBeenCalledTimes(4);
    expect(arc).toHaveBeenCalledTimes(8);
  });

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

    // One static obstacle, two player bodies, the zone circle, and one danger ring: the golden
    // snapshot's entity 8 carries `zone_exposure.outside_ticks` 214 while the own blob's is 0.
    expect(arc).toHaveBeenCalledTimes(5);
    expect(fillText.mock.calls.map((call) => call[0])).toEqual([
      'Cole Shaffer',
      'wanderer-1',
    ]);
    expect(assignments.lineWidth).toContain(4);
    expect(assignments.strokeStyle).toContain('#f8fafc');
    expect(assignments.strokeStyle).toContain(EXPOSED_PEER_RING_COLOR);
    expect(assignments.strokeStyle).not.toContain(EXPOSED_OWN_RING_COLOR);
    expect(view.getByRole('img')).toHaveAccessibleDescription(
      'Complete tick 12904 with 4 entities and 2 players.',
    );
  });

  it('rings only blobs the zone is counting, and alarms the own one', () => {
    const { arc, assignments, context } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );
    const snapshot: SessionWorldSnapshot = {
      ...goldenSnapshot,
      entities: [
        exposedBodyEntity(21, 0),
        exposedBodyEntity(22, 7),
        bodyEntity(23),
      ],
    };

    render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={22}
        snapshot={snapshot}
      />,
    );

    // Three body discs plus the two rings the own exposed blob wears; the safe blob and the blob
    // carrying no exposure component at all are drawn exactly as they were before.
    expect(arc).toHaveBeenCalledTimes(5);
    expect(assignments.strokeStyle).toContain(EXPOSED_OWN_RING_COLOR);
    expect(assignments.strokeStyle).not.toContain(EXPOSED_PEER_RING_COLOR);
  });

  it('leaves a fully safe frame with no danger colour at all', () => {
    const { arc, assignments, context } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );
    const snapshot: SessionWorldSnapshot = {
      ...goldenSnapshot,
      entities: [exposedBodyEntity(21, 0), exposedBodyEntity(22, 0)],
    };

    render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={22}
        snapshot={snapshot}
      />,
    );

    expect(arc).toHaveBeenCalledTimes(2);
    expect(assignments.strokeStyle).not.toContain(EXPOSED_OWN_RING_COLOR);
    expect(assignments.strokeStyle).not.toContain(EXPOSED_PEER_RING_COLOR);
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

  it('centres an edge target while projecting the actual map boundary outside the viewport', () => {
    const { arc, context, assignments, strokeRect } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );
    render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={21}
        camera={{ mode: 'follow', center: { x: 240, y: 300 } }}
        snapshot={{ ...goldenSnapshot, entities: [bodyEntity(21)] }}
      />,
    );
    expect(arc).toHaveBeenCalledWith(480, 320, 10, 0, 2 * Math.PI);
    expect(strokeRect).toHaveBeenCalledWith(240, 20, 960, 640);
    expect(assignments.fillStyle).toContain('#e2e8f0');
  });

  it('resizes visible world extent without changing world radii or camera centre', () => {
    const { arc, context, strokeRect } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );
    render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={21}
        camera={{ mode: 'follow', center: { x: 240, y: 300 } }}
        snapshot={{ ...goldenSnapshot, entities: [bodyEntity(21)] }}
      />,
    );
    act(() => {
      for (const observer of CanvasViewportObserver.active)
        observer.resize(600);
    });
    expect(screen.getByRole('img')).toHaveAttribute('width', '600');
    expect(screen.getByRole('img')).toHaveAttribute('height', '400');
    expect(arc).toHaveBeenLastCalledWith(300, 200, 10, 0, 2 * Math.PI);
    expect(strokeRect).toHaveBeenLastCalledWith(60, -100, 960, 640);
  });

  it('changes only the bounded backing buffer when display density changes', () => {
    const { arc, context, setTransform } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );
    render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={21}
        camera={{ mode: 'follow', center: { x: 240, y: 300 } }}
        snapshot={{ ...goldenSnapshot, entities: [bodyEntity(21)] }}
      />,
    );
    vi.stubGlobal('devicePixelRatio', 2);
    fireEvent.resize(window);
    expect(screen.getByRole('img')).toHaveAttribute('width', '1920');
    expect(screen.getByRole('img')).toHaveStyle({
      width: '960px',
      height: '640px',
    });
    expect(setTransform).toHaveBeenLastCalledWith(2, 0, 0, 2, 0, 0);
    expect(arc).toHaveBeenLastCalledWith(480, 320, 10, 0, 2 * Math.PI);
    vi.stubGlobal('devicePixelRatio', 20);
    fireEvent.resize(window);
    expect(screen.getByRole('img')).toHaveAttribute('width', '3840');
    expect(screen.getByRole('img')).toHaveAttribute('height', '2560');
    expect(arc).toHaveBeenLastCalledWith(480, 320, 10, 0, 2 * Math.PI);
  });

  it('keeps odd-size fractional-DPR geometry uniform and rearms density-only changes', () => {
    const queries: MediaQueryList[] = [];
    vi.stubGlobal('matchMedia', (query: string) => {
      const result = canvasDensityQuery(query);
      queries.push(result);
      return result;
    });
    const { arc, context, setTransform } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );
    const view = render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={21}
        camera={{ mode: 'follow', center: { x: 240, y: 300 } }}
        snapshot={{ ...goldenSnapshot, entities: [bodyEntity(21)] }}
      />,
    );
    act(() => {
      for (const observer of CanvasViewportObserver.active)
        observer.resize(601);
    });
    vi.stubGlobal('devicePixelRatio', 1.25);
    act(() => {
      queries[0]?.dispatchEvent(new Event('change'));
    });
    expect(screen.getByRole('img')).toHaveAttribute('width', '751');
    expect(screen.getByRole('img')).toHaveAttribute('height', '500');
    expect(setTransform).toHaveBeenLastCalledWith(751 / 601, 0, 0, 1.25, 0, 0);
    expect(arc).toHaveBeenLastCalledWith(300.5, 200, 10, 0, 2 * Math.PI);
    expect(queries).toHaveLength(2);
    vi.stubGlobal('devicePixelRatio', 2);
    act(() => {
      queries[0]?.dispatchEvent(new Event('change'));
    });
    expect(screen.getByRole('img')).toHaveAttribute('width', '751');
    act(() => {
      queries[1]?.dispatchEvent(new Event('change'));
    });
    expect(screen.getByRole('img')).toHaveAttribute('width', '1202');
    expect(queries).toHaveLength(3);
    view.unmount();
    act(() => {
      queries[2]?.dispatchEvent(new Event('change'));
    });
    expect(queries).toHaveLength(3);
    expect(CanvasViewportObserver.active.size).toBe(0);
  });

  it('does not invent a drawable area for a hidden viewport and resumes on layout delivery', () => {
    const { arc, context } = createCanvasContext();
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      context,
    );
    render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={21}
        snapshot={{ ...goldenSnapshot, entities: [bodyEntity(21)] }}
      />,
    );
    arc.mockClear();
    act(() => {
      for (const observer of CanvasViewportObserver.active) observer.resize(0);
    });
    expect(arc).not.toHaveBeenCalled();
    expect(screen.getByRole('img')).toHaveStyle({
      width: '0px',
      height: '0px',
    });
    act(() => {
      for (const observer of CanvasViewportObserver.active)
        observer.resize(960);
    });
    expect(arc).toHaveBeenCalledTimes(1);
  });

  it('pans only a captured primary drag and cancels it on blur, mode change, or pointer cancellation', () => {
    vi.spyOn(HTMLCanvasElement.prototype, 'getContext').mockReturnValue(
      createCanvasContext().context,
    );
    const onPan = vi.fn();
    const view = render(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={21}
        onPan={onPan}
        snapshot={goldenSnapshot}
      />,
    );
    const canvas = screen.getByRole('img');
    const captured = new Set<number>();
    Object.assign(canvas, {
      setPointerCapture: (pointerId: number) => {
        captured.add(pointerId);
      },
      hasPointerCapture: (pointerId: number) => captured.has(pointerId),
      releasePointerCapture: (pointerId: number) => {
        captured.delete(pointerId);
      },
    });
    const pointer = {
      pointerId: 1,
      isPrimary: true,
      button: 0,
      buttons: 1,
      clientX: 200,
      clientY: 100,
    };
    fireEvent.pointerDown(canvas, pointer);
    fireEvent.pointerMove(canvas, { ...pointer, pointerId: 2, clientX: 180 });
    expect(onPan).not.toHaveBeenCalled();
    fireEvent.pointerMove(canvas, { ...pointer, clientX: 170, clientY: 120 });
    expect(onPan).toHaveBeenLastCalledWith({ x: 30, y: -20 });
    fireEvent.blur(window);
    expect(captured.size).toBe(0);
    fireEvent.pointerMove(canvas, { ...pointer, clientX: 100 });
    expect(onPan).toHaveBeenCalledTimes(1);
    fireEvent.pointerDown(canvas, pointer);
    fireEvent.pointerCancel(canvas, pointer);
    fireEvent.pointerMove(canvas, { ...pointer, clientX: 100 });
    expect(onPan).toHaveBeenCalledTimes(1);
    fireEvent.pointerDown(canvas, pointer);
    captured.clear();
    fireEvent.lostPointerCapture(canvas, pointer);
    fireEvent.pointerMove(canvas, { ...pointer, clientX: 100 });
    expect(onPan).toHaveBeenCalledTimes(1);
    fireEvent.pointerDown(canvas, pointer);
    fireEvent.pointerMove(canvas, { ...pointer, buttons: 0 });
    expect(captured.size).toBe(0);
    fireEvent.pointerMove(canvas, { ...pointer, clientX: 100 });
    expect(onPan).toHaveBeenCalledTimes(1);
    fireEvent.pointerDown(canvas, pointer);
    view.rerender(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={21}
        onPan={onPan}
        camera={{ mode: 'follow', center: { x: 240, y: 300 } }}
        snapshot={goldenSnapshot}
      />,
    );
    expect(captured.size).toBe(0);
    fireEvent.pointerDown(canvas, pointer);
    fireEvent.pointerMove(canvas, { ...pointer, clientX: 100 });
    expect(onPan).toHaveBeenCalledTimes(1);
    view.rerender(
      <SimulationCanvas
        configuration={configuration}
        ownEntityId={21}
        onPan={onPan}
        snapshot={goldenSnapshot}
      />,
    );
    fireEvent.pointerDown(canvas, pointer);
    expect(captured.size).toBe(1);
    view.unmount();
    expect(captured.size).toBe(0);
  });
});
