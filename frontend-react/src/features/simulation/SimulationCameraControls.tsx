import {
  CAMERA_PAN_STEP_PIXELS,
  CAMERA_PIXELS_PER_WORLD_UNIT,
} from './simulationConstants';
import type { WorldPoint } from './rendering/worldProjection';

interface SimulationCameraControlsProps {
  readonly mode: 'follow' | 'manual';
  readonly setMode: (mode: 'follow' | 'manual') => void;
  readonly panByWorldOffset: (offset: WorldPoint) => void;
}

/** Local presentation controls. Ordinary buttons reserve arrow keys for gameplay steering. */
export function SimulationCameraControls({
  mode,
  setMode,
  panByWorldOffset,
}: SimulationCameraControlsProps) {
  const step = CAMERA_PAN_STEP_PIXELS / CAMERA_PIXELS_PER_WORLD_UNIT;
  return (
    <section aria-label="Camera controls" className="CameraControls">
      <div className="CameraModeButtons">
        <button
          aria-pressed={mode === 'follow'}
          onClick={() => setMode('follow')}
          type="button"
        >
          Follow player
        </button>
        <button
          aria-pressed={mode === 'manual'}
          onClick={() => setMode('manual')}
          type="button"
        >
          Manual view
        </button>
      </div>
      <p>
        {mode === 'follow'
          ? 'Following your blob. The view waits in place while you have no body.'
          : 'Drag the map or use the pan buttons. Your blob still steers with WASD or arrows.'}
      </p>
      <div aria-label="Pan camera" className="CameraPanButtons" role="group">
        <button
          disabled={mode !== 'manual'}
          onClick={() => panByWorldOffset({ x: -step, y: 0 })}
          type="button"
        >
          Pan left
        </button>
        <button
          disabled={mode !== 'manual'}
          onClick={() => panByWorldOffset({ x: 0, y: -step })}
          type="button"
        >
          Pan up
        </button>
        <button
          disabled={mode !== 'manual'}
          onClick={() => panByWorldOffset({ x: 0, y: step })}
          type="button"
        >
          Pan down
        </button>
        <button
          disabled={mode !== 'manual'}
          onClick={() => panByWorldOffset({ x: step, y: 0 })}
          type="button"
        >
          Pan right
        </button>
      </div>
    </section>
  );
}
