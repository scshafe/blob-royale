import { SimulationApiError } from '../SimulationApiError';
import { raceModeState } from '../sessionSelectors';
import type {
  ModeStateRendererRegistration,
  ModeStateSchemaId,
  NonVisualModeState,
} from './modeStateRendering';
import { drawRaceCourse } from './raceCourseRenderer';

function nonVisualModeState(reason: string): NonVisualModeState {
  return Object.freeze({ renders: false, reason });
}

/**
 * @extension-point mode_state_renderer -- one registration per accepted mode-state schema id.
 *
 * A mode-state block is one declared object, so a visual entry draws once per frame before every
 * entity layer. A new block adds its renderer and its entry here; the canvas names no mode. The
 * generated schema-id union makes an omitted registration a build failure, while protocol
 * validation rejects unknown wire ids before the frame can reach this registry.
 *
 * Non-visual registrations require a reason: absent geometry is a decision, never an accidental
 * omission. Schema-to-value narrowing remains in sessionSelectors alongside the HUD's reads.
 */
export const modeStateRendererRegistry = Object.freeze({
  'blob-royale://protocol/v2/mode-state/none': nonVisualModeState(
    'The empty block declares no geometry; this mode is drawn entirely through its entities.',
  ),
  'blob-royale://protocol/v2/mode-state/royale': nonVisualModeState(
    'The shrinking zone is an entity; the block carries lifecycle rules and no course geometry.',
  ),
  'blob-royale://protocol/v2/mode-state/king-of-the-hill': nonVisualModeState(
    'The touring hill is an entity; the block carries scoring denominators for the HUD.',
  ),
  'blob-royale://protocol/v2/mode-state/race': {
    renders: true,
    drawModeState: (match, frame) => {
      const value = raceModeState(match);
      if (value === null) {
        throw new SimulationApiError(
          'SIMULATION.SESSION_INVARIANT_VIOLATION',
          'Race course renderer received a frame from another mode-state schema.',
          { context: { mode_state_schema_id: match.mode_state.schema_id } },
        );
      }
      drawRaceCourse({ frame, value });
    },
  },
} satisfies Readonly<Record<ModeStateSchemaId, ModeStateRendererRegistration>>);
