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

/** The connection status, distinct from the tuning panel's request-outcome status. */
export function connectionStatus(page: Page): Locator {
  return page.locator(
    '[aria-labelledby="simulation-viewer-heading"] > [role="status"]',
  );
}

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

/** One room's card on the directory, addressed by the room's heading. */
export function roomCard(page: Page, lobbyId: number): Locator {
  return page.getByRole('listitem').filter({
    has: page.getByRole('heading', { level: 3, name: `Room ${lobbyId}` }),
  });
}

/** The value of one fact on a room's card, addressed by its term: `Mode`, `Phase`, `Seats`, ... */
export function roomFact(card: Locator, term: string): Locator {
  return card
    .locator('.LobbyFacts div')
    .filter({ has: card.page().getByText(term, { exact: true }) })
    .locator('dd');
}

/** The directory's Join control for one room, disabled with a reason when the room refuses. */
export function joinRoomButton(page: Page, lobbyId: number): Locator {
  return page.getByRole('button', { name: `Join Room ${lobbyId}` });
}

/** The lobby's Start control, which is enabled exactly when every seat is filled. */
export function lobbyStartButton(page: Page): Locator {
  return page.getByRole('button', { name: 'Start match' });
}

/** The lobby's seat-count control, floored one above the highest occupied seat. */
export function seatCountControl(page: Page): Locator {
  return page.getByRole('spinbutton', { name: 'Seats' });
}

/**
 * Presses Start through the lobby UI once it is enabled. Since protocol 2.3 a match starts only
 * when every seat is filled and somebody presses Start, and the lobby panel is the client that
 * does: a flow that wants a running match earns it the way a player does, with no wire seam.
 */
export async function startMatchFromLobby(page: Page): Promise<void> {
  const start = lobbyStartButton(page);
  await expect(start).toBeEnabled();
  await start.click();
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
  readonly drawOrder: number;
  readonly fillStyle: string;
  readonly radius: number;
  readonly x: number;
  readonly y: number;
}

/** One `fillText`: a participant's display name or a course gate's label. */
export interface RecordedLabel {
  readonly drawOrder: number;
  readonly text: string;
  readonly x: number;
  readonly y: number;
}

/** One stroked polyline in canvas pixels, including the stroke geometry the player sees. */
export interface RecordedPath {
  readonly closed: boolean;
  readonly drawOrder: number;
  readonly lineCap: CanvasLineCap;
  readonly lineJoin: CanvasLineJoin;
  readonly lineWidth: number;
  readonly points: readonly {
    readonly kind: 'move' | 'line';
    readonly x: number;
    readonly y: number;
  }[];
  readonly strokeStyle: string;
}

/** One completed canvas frame: everything drawn between two `clearRect` calls. */
export interface RecordedFrame {
  readonly arcs: readonly RecordedArc[];
  readonly cssHeight: number;
  readonly cssWidth: number;
  readonly height: number;
  readonly index: number;
  readonly labels: readonly RecordedLabel[];
  readonly paths: readonly RecordedPath[];
  readonly width: number;
  readonly worldBoundary: {
    readonly x: number;
    readonly y: number;
    readonly width: number;
    readonly height: number;
  } | null;
}

/** Real transport observations, never a replacement WebSocket or an injected command sender. */
export interface RecordedSessionTraffic {
  readonly sentFrames: string[];
  readonly receivedFrames: string[];
  readonly webSocketUrls: string[];
}

export function recordSessionTraffic(page: Page): RecordedSessionTraffic {
  const traffic: RecordedSessionTraffic = {
    sentFrames: [],
    receivedFrames: [],
    webSocketUrls: [],
  };
  page.on('websocket', (socket) => {
    traffic.webSocketUrls.push(socket.url());
    socket.on('framesent', ({ payload }) => {
      traffic.sentFrames.push(
        typeof payload === 'string' ? payload : payload.toString(),
      );
    });
    socket.on('framereceived', ({ payload }) => {
      traffic.receivedFrames.push(
        typeof payload === 'string' ? payload : payload.toString(),
      );
    });
  });
  return traffic;
}

/** A body's painted centre, not the display-name baseline below it. */
export function requirePaintedBody(
  frame: RecordedFrame,
  displayName: string,
  radiusWorldUnits: number,
): RecordedArc {
  const label = requireLabel(frame, displayName);
  const horizontalRatio = frame.width / frame.cssWidth;
  const verticalRatio = frame.height / frame.cssHeight;
  const bodies = frame.arcs.filter(
    (arc) =>
      Math.abs(arc.x - label.x) < 0.001 &&
      Math.abs(arc.radius - radiusWorldUnits * horizontalRatio) < 0.001 &&
      Math.abs(label.y - arc.y - (radiusWorldUnits + 4) * verticalRatio) <
        0.001,
  );
  const body = bodies[0];
  if (bodies.length !== 1 || body === undefined) {
    throw new BrowserE2EError(
      'BROWSER_E2E.OWN_BODY_NOT_DRAWN',
      'The frame must paint exactly one body beneath the requested participant label.',
      {
        display_name: displayName,
        body_count: bodies.length,
        frame_index: frame.index,
      },
    );
  }
  return body;
}

/** Deliberate arena focus; merely hovering must not steal a sidebar control's keyboard focus. */
export async function focusSimulationCanvas(page: Page): Promise<void> {
  const canvas = page.getByRole('img', {
    name: 'Blob Royale simulation world',
  });
  await canvas.scrollIntoViewIfNeeded();
  await canvas.click();
  await expect(canvas).toBeFocused();
}

/**
 * One real pointer placement relative to the actual raw painted body, in displayed CSS pixels.
 * Never follows a moving body: subsequent stationary-cursor updates must come from production.
 */
export async function aimFromPaintedBody(
  page: Page,
  displayName: string,
  offset: { readonly x: number; readonly y: number },
  radiusWorldUnits: number,
): Promise<{ readonly x: number; readonly y: number }> {
  const canvas = page.getByRole('img', {
    name: 'Blob Royale simulation world',
  });
  const box = await canvas.boundingBox();
  if (box === null) {
    throw new BrowserE2EError(
      'BROWSER_E2E.AIM_CANVAS_NOT_VISIBLE',
      'Cursor steering requires the actual visible canvas.',
    );
  }
  const frame = await requireCanvasFrame(page);
  const body = requirePaintedBody(frame, displayName, radiusWorldUnits);
  const point = {
    x: box.x + (body.x * box.width) / frame.width + offset.x,
    y: box.y + (body.y * box.height) / frame.height + offset.y,
  };
  if (
    point.x < box.x ||
    point.x >= box.x + box.width ||
    point.y < box.y ||
    point.y >= box.y + box.height
  ) {
    throw new BrowserE2EError(
      'BROWSER_E2E.AIM_OUTSIDE_CANVAS',
      'The authored cursor offset must remain inside the canvas.',
      {
        pointer_x: point.x,
        pointer_y: point.y,
        canvas_x: box.x,
        canvas_y: box.y,
      },
    );
  }
  await page.mouse.move(point.x, point.y);
  return point;
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
 * unmodified. Coordinates and stroke widths include the current canvas transform, so mode-state
 * geometry drawn in world space is observed at the same pixel boundary as entity renderers.
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
      beginPath: SurfaceMethod;
      clearRect: SurfaceMethod;
      closePath: SurfaceMethod;
      fill: SurfaceMethod;
      fillText: SurfaceMethod;
      lineTo: SurfaceMethod;
      moveTo: SurfaceMethod;
      stroke: SurfaceMethod;
      strokeRect: SurfaceMethod;
    }

    // Cast once, and only to make overloaded prototype methods assignable; every recorded
    // value below is read back out of the arguments the bundle actually passed.
    const surface =
      CanvasRenderingContext2D.prototype as unknown as MutableSurface;
    const state: { completed: RecordedFrame | null } = { completed: null };
    let pending: {
      arcs: RecordedArc[];
      cssHeight: number;
      cssWidth: number;
      height: number;
      index: number;
      labels: RecordedLabel[];
      paths: RecordedPath[];
      width: number;
      worldBoundary: RecordedFrame['worldBoundary'];
    } | null = null;
    let pendingArc: { radius: number; x: number; y: number } | null = null;
    let pendingPath: { kind: 'move' | 'line'; x: number; y: number }[] = [];
    let pendingPathClosed = false;
    let nextDrawOrder = 0;
    let nextIndex = 0;

    (window as unknown as Record<string, unknown>).blobRoyaleCanvasRecorder =
      state;

    const originalClearRect = surface.clearRect;
    const originalArc = surface.arc;
    const originalBeginPath = surface.beginPath;
    const originalClosePath = surface.closePath;
    const originalFill = surface.fill;
    const originalFillText = surface.fillText;
    const originalLineTo = surface.lineTo;
    const originalMoveTo = surface.moveTo;
    const originalStroke = surface.stroke;
    const originalStrokeRect = surface.strokeRect;

    surface.clearRect = function patchedClearRect(this, ...clearRectArguments) {
      if (pending !== null) {
        state.completed = pending;
      }
      const rectangle = this.canvas.getBoundingClientRect();
      pending = {
        arcs: [],
        cssHeight: rectangle.height,
        cssWidth: rectangle.width,
        height: this.canvas.height,
        index: nextIndex,
        labels: [],
        paths: [],
        width: this.canvas.width,
        worldBoundary: null,
      };
      pendingArc = null;
      pendingPath = [];
      pendingPathClosed = false;
      nextDrawOrder = 0;
      nextIndex += 1;
      return originalClearRect.apply(this, clearRectArguments);
    };

    surface.strokeRect = function patchedStrokeRect(
      this,
      ...rectangleArguments
    ) {
      if (pending !== null && this.strokeStyle === '#334155') {
        if (pending.worldBoundary !== null) {
          throw new Error('BROWSER_E2E.MULTIPLE_WORLD_BOUNDARIES');
        }
        const transform = this.getTransform();
        const origin = transform.transformPoint({
          x: rectangleArguments[0] as number,
          y: rectangleArguments[1] as number,
        });
        const opposite = transform.transformPoint({
          x:
            (rectangleArguments[0] as number) +
            (rectangleArguments[2] as number),
          y:
            (rectangleArguments[1] as number) +
            (rectangleArguments[3] as number),
        });
        pending.worldBoundary = {
          x: origin.x,
          y: origin.y,
          width: opposite.x - origin.x,
          height: opposite.y - origin.y,
        };
      }
      return originalStrokeRect.apply(this, rectangleArguments);
    };

    surface.beginPath = function patchedBeginPath(this, ...pathArguments) {
      pendingArc = null;
      pendingPath = [];
      pendingPathClosed = false;
      return originalBeginPath.apply(this, pathArguments);
    };

    surface.closePath = function patchedClosePath(this, ...pathArguments) {
      pendingPathClosed = true;
      return originalClosePath.apply(this, pathArguments);
    };

    surface.moveTo = function patchedMoveTo(this, ...pointArguments) {
      const point = this.getTransform().transformPoint({
        x: pointArguments[0] as number,
        y: pointArguments[1] as number,
      });
      pendingPath.push({ kind: 'move', x: point.x, y: point.y });
      return originalMoveTo.apply(this, pointArguments);
    };

    surface.lineTo = function patchedLineTo(this, ...pointArguments) {
      const point = this.getTransform().transformPoint({
        x: pointArguments[0] as number,
        y: pointArguments[1] as number,
      });
      pendingPath.push({ kind: 'line', x: point.x, y: point.y });
      return originalLineTo.apply(this, pointArguments);
    };

    surface.stroke = function patchedStroke(this, ...strokeArguments) {
      if (pending !== null && pendingPath.length > 0) {
        const transform = this.getTransform();
        pending.paths.push({
          closed: pendingPathClosed,
          drawOrder: nextDrawOrder,
          lineCap: this.lineCap,
          lineJoin: this.lineJoin,
          lineWidth: this.lineWidth * Math.hypot(transform.a, transform.b),
          points: [...pendingPath],
          strokeStyle: String(this.strokeStyle),
        });
        nextDrawOrder += 1;
      }
      return originalStroke.apply(this, strokeArguments);
    };

    surface.arc = function patchedArc(this, ...arcArguments) {
      const transform = this.getTransform();
      const point = transform.transformPoint({
        x: arcArguments[0] as number,
        y: arcArguments[1] as number,
      });
      pendingArc = {
        radius:
          (arcArguments[2] as number) * Math.hypot(transform.a, transform.b),
        x: point.x,
        y: point.y,
      };
      return originalArc.apply(this, arcArguments);
    };

    surface.fill = function patchedFill(this, ...fillArguments) {
      if (pending !== null && pendingArc !== null) {
        // The fill style is read here rather than at `arc`, because both renderers set it between
        // the two calls; this is the colour the player sees under that circle.
        pending.arcs.push({
          drawOrder: nextDrawOrder,
          fillStyle: String(this.fillStyle),
          radius: pendingArc.radius,
          x: pendingArc.x,
          y: pendingArc.y,
        });
        nextDrawOrder += 1;
        pendingArc = null;
      }
      return originalFill.apply(this, fillArguments);
    };

    surface.fillText = function patchedFillText(this, ...fillTextArguments) {
      const point = this.getTransform().transformPoint({
        x: fillTextArguments[1] as number,
        y: fillTextArguments[2] as number,
      });
      pending?.labels.push({
        drawOrder: nextDrawOrder,
        text: fillTextArguments[0] as string,
        x: point.x,
        y: point.y,
      });
      nextDrawOrder += 1;
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

/**
 * Recover world positions from what was painted, not from React or a protocol-state seam.
 *
 * The real map boundary is the world origin projected by the same frame as every body/course.
 * Removing its observed translation and the actual backing/CSS ratio preserves motion checks
 * across independently moving cameras. All fixtures using this helper declare 1920×1280, so the
 * measured boundary MUST occupy that many CSS pixels: normalizing must not hide a fit-all scale.
 * Labels retain their four-CSS-pixel gap below a body's world-radius offset.
 */
function worldCoordinates(frame: RecordedFrame): RecordedFrame {
  const boundary = frame.worldBoundary;
  if (boundary === null || frame.cssWidth <= 0 || frame.cssHeight <= 0) {
    throw new BrowserE2EError(
      'BROWSER_E2E.WORLD_PROJECTION_UNAVAILABLE',
      'A completed gameplay frame must paint its world boundary in a positive CSS viewport.',
      { frame_index: frame.index },
    );
  }
  const horizontalRatio = frame.width / frame.cssWidth;
  const verticalRatio = frame.height / frame.cssHeight;
  expect(boundary.width / horizontalRatio).toBeCloseTo(1920, 8);
  expect(boundary.height / verticalRatio).toBeCloseTo(1280, 8);
  const point = (value: { x: number; y: number }) => ({
    x: (value.x - boundary.x) / horizontalRatio,
    y: (value.y - boundary.y) / verticalRatio,
  });
  return {
    ...frame,
    arcs: frame.arcs.map((arc) => ({
      ...arc,
      ...point(arc),
      radius: arc.radius / horizontalRatio,
    })),
    labels: frame.labels.map((label) => ({ ...label, ...point(label) })),
    paths: frame.paths.map((path) => ({
      ...path,
      lineWidth: path.lineWidth / horizontalRatio,
      points: path.points.map((entry) => ({ ...entry, ...point(entry) })),
    })),
  };
}

/** The actual painted frame with camera translation removed for existing world-motion proofs. */
export async function readWorldCanvasFrame(
  page: Page,
): Promise<RecordedFrame | null> {
  const frame = await readCanvasFrame(page);
  return frame === null ? null : worldCoordinates(frame);
}

/** The same world-normalized geometry, requiring a complete painted frame. */
export async function requireWorldCanvasFrame(
  page: Page,
): Promise<RecordedFrame> {
  return worldCoordinates(await requireCanvasFrame(page));
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
