import { useEffect, useRef, useState } from 'react';

import { SimulationApiError } from './SimulationApiError';
import {
  CANVAS_MAX_HEIGHT_PIXELS,
  CANVAS_MAX_PIXEL_RATIO,
  CANVAS_MAX_WIDTH_PIXELS,
} from './simulationConstants';

function readPixelRatio(): number {
  const ratio = window.devicePixelRatio;
  if (!Number.isFinite(ratio) || ratio <= 0) {
    throw new SimulationApiError(
      'SIMULATION.CAMERA_VIEWPORT_INVALID',
      'Cannot size the camera backing buffer with an invalid device pixel ratio.',
      { context: { device_pixel_ratio: ratio } },
    );
  }
  return Math.min(ratio, CANVAS_MAX_PIXEL_RATIO);
}

/**
 * Measures the local CSS viewport, never the map. A zero-width hidden container has no drawable
 * view; the initial maximum is replaced by ResizeObserver's first layout delivery. Resize and
 * display-density changes only resize the backing buffer, not the camera's world-space state.
 */
export function useCanvasViewport() {
  const containerReference = useRef<HTMLDivElement>(null);
  const [viewport, setViewport] = useState(() => ({
    width: CANVAS_MAX_WIDTH_PIXELS,
    height: CANVAS_MAX_HEIGHT_PIXELS,
    pixelRatio: readPixelRatio(),
  }));

  useEffect(() => {
    const container = containerReference.current;
    if (container === null) return;
    const observer = new ResizeObserver((entries) => {
      const entry = entries.find((candidate) => candidate.target === container);
      if (entry === undefined) return;
      const availableWidth = entry.contentRect.width;
      if (!Number.isFinite(availableWidth) || availableWidth < 0) {
        throw new SimulationApiError(
          'SIMULATION.CAMERA_VIEWPORT_INVALID',
          'Cannot size the camera viewport from an invalid layout width.',
          { context: { available_width: availableWidth } },
        );
      }
      const width = Math.min(
        CANVAS_MAX_WIDTH_PIXELS,
        Math.floor(availableWidth),
      );
      const height = Math.floor(
        (width * CANVAS_MAX_HEIGHT_PIXELS) / CANVAS_MAX_WIDTH_PIXELS,
      );
      setViewport((previous) =>
        previous.width === width && previous.height === height
          ? previous
          : { ...previous, width, height },
      );
    });
    observer.observe(container);

    // Moving a window between screens may change DPR without changing its CSS dimensions.
    let densityQuery = window.matchMedia(
      `(resolution: ${window.devicePixelRatio}dppx)`,
    );
    function updateDensity() {
      const pixelRatio = readPixelRatio();
      setViewport((previous) =>
        previous.pixelRatio === pixelRatio
          ? previous
          : { ...previous, pixelRatio },
      );
      densityQuery.removeEventListener('change', updateDensity);
      densityQuery = window.matchMedia(
        `(resolution: ${window.devicePixelRatio}dppx)`,
      );
      densityQuery.addEventListener('change', updateDensity);
    }
    densityQuery.addEventListener('change', updateDensity);
    window.addEventListener('resize', updateDensity);
    return () => {
      observer.disconnect();
      densityQuery.removeEventListener('change', updateDensity);
      window.removeEventListener('resize', updateDensity);
    };
  }, []);

  return { containerReference, viewport };
}
