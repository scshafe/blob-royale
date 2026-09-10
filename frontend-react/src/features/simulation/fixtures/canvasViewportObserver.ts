import {
  CANVAS_MAX_HEIGHT_PIXELS,
  CANVAS_MAX_WIDTH_PIXELS,
} from '../simulationConstants';

/** Deterministic layout delivery for jsdom; individual tests can explicitly resize an observed view. */
export class CanvasViewportObserver implements ResizeObserver {
  static readonly active = new Set<CanvasViewportObserver>();
  private readonly targets = new Set<Element>();
  constructor(private readonly callback: ResizeObserverCallback) {
    CanvasViewportObserver.active.add(this);
  }
  observe(target: Element): void {
    this.targets.add(target);
    this.resize(CANVAS_MAX_WIDTH_PIXELS);
  }
  unobserve(target: Element): void {
    this.targets.delete(target);
  }
  disconnect(): void {
    this.targets.clear();
    CanvasViewportObserver.active.delete(this);
  }
  resize(width: number): void {
    const height = (width * CANVAS_MAX_HEIGHT_PIXELS) / CANVAS_MAX_WIDTH_PIXELS;
    this.callback(
      [...this.targets].map((target) => ({
        target,
        contentRect: { width, height } as DOMRectReadOnly,
        borderBoxSize: [],
        contentBoxSize: [],
        devicePixelContentBoxSize: [],
      })),
      this,
    );
  }
}

/** Density-query event target; resizing tests dispatch window resize after changing DPR. */
export function canvasDensityQuery(query: string): MediaQueryList {
  return Object.assign(new EventTarget(), {
    matches: true,
    media: query,
    onchange: null,
    addListener: () => undefined,
    removeListener: () => undefined,
  }) as MediaQueryList;
}
