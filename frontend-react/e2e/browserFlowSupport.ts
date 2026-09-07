import {
  expect,
  type APIRequestContext,
  type Locator,
  type Page,
} from '@playwright/test';

import type { BlobRoyaleServerProcess } from './BlobRoyaleServerProcess';
import { BrowserE2EError } from './BrowserE2EError';

export const PRODUCTION_ORIGIN = 'http://127.0.0.1:5173';

/** The two connection labels these flows read. Kept together so a copy of one cannot drift. */
export const CONNECTED_STATUS = 'Connected to the match session.';
export const RETRYING_STATUS =
  'The match session disconnected. Retrying with bounded backoff…';

const READINESS_PATH = '/api/v1/health/ready';
const READINESS_TIMEOUT_MILLISECONDS = 10_000;
const READINESS_RETRY_INTERVAL_MILLISECONDS = 50;
// One HTTP token refills every 500 ms, so a probe that spent the bucket waits for a token rather
// than hammering an empty one.
const READINESS_THROTTLED_INTERVAL_MILLISECONDS = 600;

export function delay(milliseconds: number): Promise<void> {
  return new Promise((resolve) => {
    setTimeout(resolve, milliseconds);
  });
}

function isRetryableReadinessStatus(responseStatus: number): boolean {
  return responseStatus === 502 || responseStatus === 503;
}

/**
 * Waits for the exact server through the production same-origin proxy, asserting the readiness
 * envelope rather than only its status code. Fails immediately if the server process has died.
 */
export async function waitForReadyServer(
  request: APIRequestContext,
  server: BlobRoyaleServerProcess,
): Promise<void> {
  const deadline = Date.now() + READINESS_TIMEOUT_MILLISECONDS;
  let lastTransportError: unknown = null;

  while (Date.now() < deadline) {
    await server.assertRunning();
    try {
      const response = await request.get(READINESS_PATH, {
        headers: {
          Accept: 'application/json',
          Origin: PRODUCTION_ORIGIN,
          'X-Request-ID': 'browser-e2e-readiness',
        },
        timeout: 1_000,
      });
      if (response.status() === 429) {
        // A rate-limited probe reports the request budget, not readiness. Backing off is the
        // documented client behaviour; the outer deadline still bounds the wait.
        await delay(READINESS_THROTTLED_INTERVAL_MILLISECONDS);
        continue;
      }
      if (isRetryableReadinessStatus(response.status())) {
        await delay(READINESS_RETRY_INTERVAL_MILLISECONDS);
        continue;
      }
      if (response.status() !== 200) {
        throw new BrowserE2EError(
          'BROWSER_E2E.READINESS_STATUS_INVALID',
          'Readiness through the production same-origin proxy returned an unexpected status.',
          { response_status: response.status() },
        );
      }

      expect(response.headers()['content-type']).toMatch(
        /^application\/json(?:;|$)/,
      );
      expect(await response.json()).toEqual({
        data: {
          snapshot_available: true,
          status: 'ready',
        },
        error: null,
        meta: {
          protocol_version: '1.0',
          request_id: 'browser-e2e-readiness',
          schema_id: 'blob-royale://protocol/v1/readiness-response',
        },
      });
      return;
    } catch (error) {
      if (error instanceof BrowserE2EError) {
        throw error;
      }
      lastTransportError = error;
      await delay(READINESS_RETRY_INTERVAL_MILLISECONDS);
    }
  }

  throw new BrowserE2EError(
    'BROWSER_E2E.READINESS_TIMEOUT',
    'The exact server did not become ready through the production same-origin proxy.',
    { timeout_milliseconds: READINESS_TIMEOUT_MILLISECONDS },
    lastTransportError,
  );
}

/** The value cell of one row of the player-facing match HUD, addressed by its row header. */
export function matchHudCell(page: Page, rowHeader: string): Locator {
  return page
    .getByRole('table', { name: 'Match status' })
    .getByRole('row')
    .filter({
      has: page.getByRole('rowheader', { exact: true, name: rowHeader }),
    })
    .getByRole('cell');
}

/** One `arc` that was filled, with the fill colour the canvas actually serialized. */
export interface RecordedArc {
  readonly fillStyle: string;
  readonly radius: number;
  readonly x: number;
  readonly y: number;
}

/** One `fillText`, which for this client is exactly one entity's display name. */
export interface RecordedLabel {
  readonly text: string;
  readonly x: number;
  readonly y: number;
}

/** One completed canvas frame: everything drawn between two `clearRect` calls. */
export interface RecordedFrame {
  readonly arcs: readonly RecordedArc[];
  readonly index: number;
  readonly labels: readonly RecordedLabel[];
}

interface CanvasRecorderState {
  readonly completed: RecordedFrame | null;
}

/**
 * Records what the production bundle actually paints.
 *
 * Display names and the safe zone exist for the player only as pixels: `controllableLabelRenderer`
 * writes a name with `fillText` and `zoneRenderer` draws the zone with `arc`, and neither reaches
 * the DOM. Reading React state instead would prove the client's model is coherent and prove
 * nothing about what the canvas was told to draw, which is the boundary these flows exist to
 * observe. This wraps `CanvasRenderingContext2D` from an init script, so the bundle under test is
 * unmodified and the recorded values are the exact arguments it passed.
 *
 * `SimulationCanvas` starts every frame with `clearRect`, so a `clearRect` both closes the previous
 * frame and opens the next one; `completed` is therefore always a whole frame, never a partial one.
 */
export async function installCanvasRecorder(page: Page): Promise<void> {
  await page.addInitScript(() => {
    type SurfaceMethod = (
      this: CanvasRenderingContext2D,
      ...methodArguments: unknown[]
    ) => unknown;
    interface MutableSurface {
      arc: SurfaceMethod;
      clearRect: SurfaceMethod;
      fill: SurfaceMethod;
      fillText: SurfaceMethod;
    }

    // Cast once, and only to make four overloaded prototype methods assignable; every recorded
    // value below is read back out of the arguments the bundle actually passed.
    const surface =
      CanvasRenderingContext2D.prototype as unknown as MutableSurface;
    const state: { completed: RecordedFrame | null } = { completed: null };
    let pending: {
      arcs: RecordedArc[];
      index: number;
      labels: RecordedLabel[];
    } | null = null;
    let pendingArc: { radius: number; x: number; y: number } | null = null;
    let nextIndex = 0;

    (window as unknown as Record<string, unknown>).blobRoyaleCanvasRecorder =
      state;

    const originalClearRect = surface.clearRect;
    const originalArc = surface.arc;
    const originalFill = surface.fill;
    const originalFillText = surface.fillText;

    surface.clearRect = function patchedClearRect(this, ...clearRectArguments) {
      if (pending !== null) {
        state.completed = pending;
      }
      pending = { arcs: [], index: nextIndex, labels: [] };
      pendingArc = null;
      nextIndex += 1;
      return originalClearRect.apply(this, clearRectArguments);
    };

    surface.arc = function patchedArc(this, ...arcArguments) {
      pendingArc = {
        radius: arcArguments[2] as number,
        x: arcArguments[0] as number,
        y: arcArguments[1] as number,
      };
      return originalArc.apply(this, arcArguments);
    };

    surface.fill = function patchedFill(this, ...fillArguments) {
      if (pending !== null && pendingArc !== null) {
        // The fill style is read here rather than at `arc`, because both renderers set it between
        // the two calls; this is the colour the player sees under that circle.
        pending.arcs.push({
          fillStyle: String(this.fillStyle),
          radius: pendingArc.radius,
          x: pendingArc.x,
          y: pendingArc.y,
        });
        pendingArc = null;
      }
      return originalFill.apply(this, fillArguments);
    };

    surface.fillText = function patchedFillText(this, ...fillTextArguments) {
      pending?.labels.push({
        text: fillTextArguments[0] as string,
        x: fillTextArguments[1] as number,
        y: fillTextArguments[2] as number,
      });
      return originalFillText.apply(this, fillTextArguments);
    };
  });
}

/** The newest whole frame, or null before the client has drawn two. */
export async function readCanvasFrame(
  page: Page,
): Promise<RecordedFrame | null> {
  return page.evaluate(() => {
    const recorder = (window as unknown as Record<string, unknown>)
      .blobRoyaleCanvasRecorder as CanvasRecorderState | undefined;
    return recorder?.completed ?? null;
  });
}

/** The newest whole frame, insisting there is one. */
export async function requireCanvasFrame(page: Page): Promise<RecordedFrame> {
  const frame = await readCanvasFrame(page);
  if (frame === null) {
    throw new BrowserE2EError(
      'BROWSER_E2E.CANVAS_FRAME_UNAVAILABLE',
      'The page has not completed a canvas frame.',
      { page_url: page.url() },
    );
  }
  return frame;
}

export function findLabel(
  frame: RecordedFrame,
  text: string,
): RecordedLabel | null {
  return frame.labels.find((label) => label.text === text) ?? null;
}

export function requireLabel(
  frame: RecordedFrame,
  text: string,
): RecordedLabel {
  const label = findLabel(frame, text);
  if (label === null) {
    throw new BrowserE2EError(
      'BROWSER_E2E.CANVAS_LABEL_ABSENT',
      'The canvas frame carries no label with the requested display name.',
      {
        drawn_labels: frame.labels.map((entry) => entry.text).join('|'),
        frame_index: frame.index,
        requested_text: text,
      },
    );
  }
  return label;
}
