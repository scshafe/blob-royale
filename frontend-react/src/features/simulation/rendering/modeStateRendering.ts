import type { SessionMatchSection } from '../simulationProtocolTypes';
import type { EntityRenderFrame } from './entityRendering';

/** A mode's declared geometry uses the same projection and surface as every entity layer. */
export type ModeStateRenderFrame = Pick<
  EntityRenderFrame,
  'projection' | 'surface'
>;

export type ModeStateSchemaId = SessionMatchSection['mode_state']['schema_id'];

export interface VisualModeStateRenderer {
  readonly renders: true;
  readonly drawModeState: (
    match: SessionMatchSection,
    frame: ModeStateRenderFrame,
  ) => void;
}

export interface NonVisualModeState {
  readonly renders: false;
  readonly reason: string;
}

export type ModeStateRendererRegistration =
  VisualModeStateRenderer | NonVisualModeState;
