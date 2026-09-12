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
  /**
   * The tick this frame's snapshot committed, or `null` when no snapshot has arrived to draw from.
   *
   * It is on the frame for the same reason `eliminationGraceTicks` is: a renderer receives what it
   * may read and never goes looking. Every ability window on the wire is a pair of absolute ticks,
   * so a renderer that cannot see the tick can only draw *component presence* -- and the published
   * shield schema explicitly forbids that reading, because a cancelled shield keeps publishing a
   * component whose protection has already ended. Resolving it once per frame is also what stops a
   * mark on a body and the caption describing the frame from being a tick apart. It is the
   * snapshot's tick and never elapsed browser time: no clock readies, unlocks or expires anything
   * here.
   */
  readonly tickSequence: number | null;
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
 *
 * `status` is one new layer rather than a second tenant of `exposure`, even though both mark a body
 * from outside it. `visualEntityRenderers()` sorts by layer and then by KIND NAME, so two kinds
 * sharing a layer paint in whatever order their spellings fall in: on `exposure`, `shield` and
 * `stun` both sort before `zone_exposure`, and every ability mark would end up under the exposure
 * ring for no reason but the alphabet. A layer boundary makes that a decision instead. `exposure` is
 * also named for zone exposure specifically, so an ability mark living there would make the layer's
 * own name a lie the next reader has to discover.
 *
 * The numbers are an internal ordering and nothing else: no test pins them, `label` renumbers 4 to 5
 * here, and the assertion that actually holds the order is the sorted kind list in
 * `entityRendererRegistry.test.ts`.
 */
export const ENTITY_RENDER_LAYERS = Object.freeze({
  body: 1,
  hazard: 2,
  exposure: 3,
  status: 4,
  label: 5,
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
