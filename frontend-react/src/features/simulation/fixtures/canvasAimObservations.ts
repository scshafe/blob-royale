interface AimSurfaceOrigin {
  readonly left: number;
  readonly top: number;
}
export const AIM_SURFACE_OFFSET: AimSurfaceOrigin = Object.freeze({
  left: 100,
  top: 50,
});

/** Primary mouse coordinates in the viewport, independent of device-pixel backing dimensions. */
export function aimPointer(
  x = 680,
  y = 370,
  overrides: PointerEventInit = {},
): PointerEventInit {
  return {
    pointerId: 1,
    pointerType: 'mouse',
    isPrimary: true,
    button: 0,
    buttons: 0,
    clientX: x,
    clientY: y,
    ...overrides,
  };
}

/** jsdom has no layout or pointer capture. Supply explicit CSS bounds and the normal capture API. */
export function installCanvasAimSurface(
  canvas: HTMLCanvasElement,
  initial = AIM_SURFACE_OFFSET,
) {
  let origin = initial;
  const captured = new Set<number>();
  Object.assign(canvas, {
    getBoundingClientRect: () =>
      new DOMRect(
        origin.left,
        origin.top,
        Number.parseFloat(canvas.style.width),
        Number.parseFloat(canvas.style.height),
      ),
    setPointerCapture: (id: number) => {
      captured.add(id);
    },
    hasPointerCapture: (id: number) => captured.has(id),
    releasePointerCapture: (id: number) => {
      captured.delete(id);
    },
  });
  return {
    captured,
    moveTo: (left: number, top: number) => {
      origin = { left, top };
    },
  };
}
