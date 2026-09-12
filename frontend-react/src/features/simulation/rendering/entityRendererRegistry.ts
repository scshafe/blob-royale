import type { SessionComponentKind } from '../simulationProtocolTypes';
import { drawControllableLabel } from './controllableLabelRenderer';
import {
  ENTITY_RENDER_LAYERS,
  type EntityRendererRegistration,
  type VisualEntityRenderer,
  entityRenderer,
  nonVisualComponent,
} from './entityRendering';
import { drawHill } from './hillRenderer';
import { drawPhysicsBody } from './physicsBodyRenderer';
import { drawZone } from './zoneRenderer';
import { drawLethalOnContact } from './lethalOnContactRenderer';
import { drawZoneExposure } from './zoneExposureRenderer';

/**
 * @extension-point entity_renderer -- the client's registration point for one component kind.
 *
 * Adding a component kind to the client touches exactly two files:
 *   1. `src/features/simulation/rendering/<kind>Renderer.ts` -- one exported draw function that
 *      receives its own already-narrowed component, its entity, and the frame.
 *   2. this file -- one import and one entry keyed by the kind, either `entityRenderer(...)` or
 *      `nonVisualComponent(kind, reason)`.
 *
 * Nothing else changes. `SimulationCanvas` iterates this registry and never names a component kind,
 * so a new kind renders without the canvas being edited. The `satisfies` clause is the guard: a
 * kind added to `docs/protocol/schema/v3/common.schema.json` and regenerated but not registered
 * here fails the build rather than rendering as nothing, because an entity nobody draws is an
 * invisible entity and an invisible entity is an incorrect world.
 */
export const entityRendererRegistry = Object.freeze({
  controllable: entityRenderer(
    'controllable',
    ENTITY_RENDER_LAYERS.label,
    drawControllableLabel,
  ),
  hill: entityRenderer('hill', ENTITY_RENDER_LAYERS.zone, drawHill),
  hill_motion: nonVisualComponent(
    'hill_motion',
    'Committed velocity is observable state, not extra geometry; hill supplies the scoring circle.',
  ),
  hill_presence: nonVisualComponent(
    'hill_presence',
    'Progress toward the next point belongs to the HUD, as a ring against the published interval.',
  ),
  lifetime: nonVisualComponent(
    'lifetime',
    'A remaining-tick count has no geometry; it is reported in the debug panel.',
  ),
  lethal_on_contact: entityRenderer(
    'lethal_on_contact',
    ENTITY_RENDER_LAYERS.hazard,
    drawLethalOnContact,
  ),
  physics_body: entityRenderer(
    'physics_body',
    ENTITY_RENDER_LAYERS.body,
    drawPhysicsBody,
  ),
  race_progress: nonVisualComponent(
    'race_progress',
    'An ordered gate index has no geometry; the course supplies the gates and the HUD reports progress.',
  ),
  respawn_timer: nonVisualComponent(
    'respawn_timer',
    'A return countdown has no geometry; an entity carrying it has no body to draw, and the HUD counts it down.',
  ),
  score: nonVisualComponent(
    'score',
    'A scoreboard cell belongs to the HUD, not to the arena.',
  ),
  stun: nonVisualComponent(
    'stun',
    'Absolute status ticks lock input; stun presentation belongs to the later ability UI.',
  ),
  team: nonVisualComponent(
    'team',
    'No accepted mode fields teams yet; a team renderer without a mode to render is a guess.',
  ),
  zone: entityRenderer('zone', ENTITY_RENDER_LAYERS.zone, drawZone),
  zone_exposure: entityRenderer(
    'zone_exposure',
    ENTITY_RENDER_LAYERS.exposure,
    drawZoneExposure,
  ),
}) satisfies Readonly<Record<SessionComponentKind, EntityRendererRegistration>>;

function isVisual(
  registration: EntityRendererRegistration,
): registration is VisualEntityRenderer {
  return registration.renders;
}

/**
 * The visual renderers in draw order: ascending layer, then ascending kind name so two renderers
 * sharing a layer paint in an order that does not depend on object key iteration.
 */
export function visualEntityRenderers(): readonly VisualEntityRenderer[] {
  return Object.values(entityRendererRegistry)
    .filter(isVisual)
    .sort(
      (left, right) =>
        left.layer - right.layer || left.kind.localeCompare(right.kind),
    );
}
