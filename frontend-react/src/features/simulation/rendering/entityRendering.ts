import type {
  SessionComponentKind,
  SessionComponentOfKind,
  SessionEntitySnapshot,
} from '../simulationProtocolTypes';
import type { WorldProjection } from './worldProjection';

export type { WorldProjection } from './worldProjection';

/** Everything a renderer may read that is not its own component or its own entity. */
export interface EntityRenderFrame {
  /**
   * `G` from this frame's mode-state block, or `null` when the running mode published no grace.
   *
   * It is on the frame rather than reached for by the one renderer that wants it, for the same
   * reason `ownEntityId` is: a renderer receives what it may read and never goes looking. It is
   * match-wide, so resolving it once per frame is also the only way a renderer cannot disagree with
   * the HUD about the same number (`sessionSelectors.eliminationGraceTicks`).
   */
  readonly eliminationGraceTicks: number | null;
  readonly ownEntityId: number | null;
  readonly projection: WorldProjection;
  readonly surface: CanvasRenderingContext2D;
}

export interface EntityRenderInput<Kind extends SessionComponentKind> {
  readonly component: SessionComponentOfKind<Kind>;
  readonly entity: SessionEntitySnapshot;
  readonly frame: EntityRenderFrame;
  readonly isOwnEntity: boolean;
}

export type EntityComponentDraw<Kind extends SessionComponentKind> = (
  input: EntityRenderInput<Kind>,
) => void;

/**
 * Draw order. Every renderer of one layer runs across every entity before the next layer starts, so
 * a label is never painted under a body that happens to arrive later in the snapshot.
 */
export const ENTITY_RENDER_LAYERS = Object.freeze({
  body: 1,
  hazard: 2,
  exposure: 3,
  label: 4,
  zone: 0,
});

export interface VisualEntityRenderer {
  readonly renders: true;
  readonly kind: SessionComponentKind;
  readonly layer: number;
  readonly drawEntity: (
    entity: SessionEntitySnapshot,
    frame: EntityRenderFrame,
  ) => void;
}

export interface NonVisualComponent {
  readonly renders: false;
  readonly kind: SessionComponentKind;
  readonly reason: string;
}

export type EntityRendererRegistration =
  VisualEntityRenderer | NonVisualComponent;

/**
 * Binds one draw function to one component kind. The kind-to-value correlation is resolved here,
 * once, so a renderer receives its own component already narrowed and the canvas never names a
 * kind or reaches into a component map.
 */
export function entityRenderer<Kind extends SessionComponentKind>(
  kind: Kind,
  layer: number,
  draw: EntityComponentDraw<Kind>,
): VisualEntityRenderer {
  return Object.freeze({
    renders: true,
    kind,
    layer,
    drawEntity: (entity: SessionEntitySnapshot, frame: EntityRenderFrame) => {
      const component = entity.components[kind];
      if (component === undefined) {
        return;
      }
      draw({
        component,
        entity,
        frame,
        isOwnEntity: entity.entity_id === frame.ownEntityId,
      });
    },
  });
}

/**
 * Registers a component kind that carries no pixels. The reason is required: a kind with neither a
 * renderer nor a stated reason is how an entity becomes invisible without anyone deciding it should.
 */
export function nonVisualComponent(
  kind: SessionComponentKind,
  reason: string,
): NonVisualComponent {
  return Object.freeze({ renders: false, kind, reason });
}
