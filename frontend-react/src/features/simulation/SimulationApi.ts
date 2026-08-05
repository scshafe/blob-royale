import { SimulationApiError } from './SimulationApiError';
import {
  CONFIGURATION_ENDPOINT_PATH,
  CONFIGURATION_FETCH_TIMEOUT_MILLISECONDS,
  CONFIGURATION_RESPONSE_MAX_BYTES,
  SNAPSHOT_ENDPOINT_PATH,
  SNAPSHOT_FRAME_MAX_BYTES,
  SNAPSHOT_WEBSOCKET_SUBPROTOCOL,
  WEBSOCKET_CONNECT_TIMEOUT_MILLISECONDS,
} from './simulationConstants';
import type {
  SimulationConfiguration,
  SimulationSnapshotMessage,
} from './simulationProtocolTypes';
import {
  type SnapshotSequenceState,
  validateSimulationConfigurationResponse,
  validateSimulationHttpErrorResponse,
  validateSimulationSnapshotMessage,
} from './simulationProtocolValidation';

const WEBSOCKET_CONNECTING = 0;
const WEBSOCKET_OPEN = 1;

export interface SimulationBrowserLocation {
  readonly host: string;
  readonly origin: string;
  readonly protocol: string;
}

export interface SimulationEndpoints {
  readonly configurationUrl: string;
  readonly snapshotWebSocketUrl: string;
}

export interface SimulationWebSocket {
  readonly protocol: string;
  readonly readyState: number;
  onclose: ((event: CloseEvent) => void) | null;
  onerror: ((event: Event) => void) | null;
  onmessage: ((event: MessageEvent<unknown>) => void) | null;
  onopen: ((event: Event) => void) | null;
  close(code?: number, reason?: string): void;
}

export type SimulationWebSocketFactory = (
  url: string,
  subprotocol: string,
) => SimulationWebSocket;

export interface SimulationDisconnection {
  readonly code: number | null;
  readonly error: SimulationApiError | null;
  readonly reason: string;
  readonly retryable: boolean;
  readonly wasClean: boolean;
}

export interface SimulationSnapshotCallbacks {
  readonly onConnected: () => void;
  readonly onDisconnected: (disconnection: SimulationDisconnection) => void;
  readonly onFailure: (error: SimulationApiError) => void;
  readonly onSnapshot: (snapshot: SimulationSnapshotMessage) => void;
}

export interface SimulationApiBoundary {
  loadConfiguration(signal: AbortSignal): Promise<SimulationConfiguration>;
  openSnapshotStream(
    configuration: SimulationConfiguration,
    callbacks: SimulationSnapshotCallbacks,
  ): void;
  dispose(): void;
}

export interface SimulationApiDependencies {
  readonly fetchImplementation?: typeof fetch;
  readonly location?: SimulationBrowserLocation;
  readonly webSocketFactory?: SimulationWebSocketFactory;
}

/** Derives the two exact v1 endpoints from one validated browser authority. */
export function deriveSimulationEndpoints(
  location: SimulationBrowserLocation,
): SimulationEndpoints {
  if (location.protocol !== 'http:' && location.protocol !== 'https:') {
    throw new SimulationApiError(
      'SIMULATION.ENDPOINT_INVALID',
      'Simulation endpoints require an HTTP or HTTPS page origin.',
      { context: { page_protocol: location.protocol } },
    );
  }

  let pageOrigin: URL;
  try {
    pageOrigin = new URL(location.origin);
  } catch (cause) {
    throw new SimulationApiError(
      'SIMULATION.ENDPOINT_INVALID',
      'Browser origin is not a valid absolute URL.',
      { cause, context: { page_origin: location.origin } },
    );
  }

  if (
    pageOrigin.origin !== location.origin ||
    pageOrigin.protocol !== location.protocol ||
    pageOrigin.host !== location.host ||
    pageOrigin.username !== '' ||
    pageOrigin.password !== ''
  ) {
    throw new SimulationApiError(
      'SIMULATION.ENDPOINT_INVALID',
      'Browser origin, protocol, and authority are inconsistent.',
      {
        context: {
          page_host: location.host,
          page_origin: location.origin,
          page_protocol: location.protocol,
        },
      },
    );
  }

  const configurationUrl = new URL(CONFIGURATION_ENDPOINT_PATH, pageOrigin);
  const snapshotWebSocketUrl = new URL(SNAPSHOT_ENDPOINT_PATH, pageOrigin);
  snapshotWebSocketUrl.protocol =
    location.protocol === 'https:' ? 'wss:' : 'ws:';

  return Object.freeze({
    configurationUrl: configurationUrl.href,
    snapshotWebSocketUrl: snapshotWebSocketUrl.href,
  });
}

function isRetryableCloseCode(closeCode: number): boolean {
  return [1001, 1006, 1011, 1012, 1013].includes(closeCode);
}

function parseContentLength(contentLength: string | null): number | null {
  if (contentLength === null) {
    return null;
  }

  if (!/^(?:0|[1-9][0-9]*)$/.test(contentLength)) {
    throw new SimulationApiError(
      'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
      'Configuration response Content-Length is invalid.',
      { context: { content_length: contentLength } },
    );
  }

  const parsedContentLength = Number(contentLength);
  if (!Number.isSafeInteger(parsedContentLength)) {
    throw new SimulationApiError(
      'SIMULATION.CONFIGURATION_RESPONSE_TOO_LARGE',
      'Configuration response Content-Length exceeds the safe integer range.',
      { context: { content_length: contentLength } },
    );
  }

  return parsedContentLength;
}

async function readBoundedResponseBytes(
  response: Response,
  maximumBytes: number,
): Promise<Uint8Array> {
  const declaredLength = parseContentLength(
    response.headers.get('content-length'),
  );
  if (declaredLength !== null && declaredLength > maximumBytes) {
    throw new SimulationApiError(
      'SIMULATION.CONFIGURATION_RESPONSE_TOO_LARGE',
      'Configuration response exceeds the protocol body limit.',
      {
        context: {
          declared_bytes: declaredLength,
          maximum_bytes: maximumBytes,
        },
      },
    );
  }

  if (response.body === null) {
    const responseBytes = new Uint8Array(await response.arrayBuffer());
    if (responseBytes.byteLength > maximumBytes) {
      throw new SimulationApiError(
        'SIMULATION.CONFIGURATION_RESPONSE_TOO_LARGE',
        'Configuration response exceeds the protocol body limit.',
        {
          context: {
            actual_bytes: responseBytes.byteLength,
            maximum_bytes: maximumBytes,
          },
        },
      );
    }
    return responseBytes;
  }

  const reader = response.body.getReader();
  const chunks: Uint8Array[] = [];
  let receivedBytes = 0;

  while (true) {
    const readResult = await reader.read();
    if (readResult.done) {
      break;
    }
    if (readResult.value === undefined) {
      throw new SimulationApiError(
        'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
        'Configuration response stream returned an empty chunk.',
      );
    }

    receivedBytes += readResult.value.byteLength;
    if (receivedBytes > maximumBytes) {
      try {
        await reader.cancel('configuration_response_too_large');
      } catch (cause) {
        throw new SimulationApiError(
          'SIMULATION.CONFIGURATION_RESPONSE_TOO_LARGE',
          'Configuration response exceeded the protocol body limit and stream cancellation failed.',
          {
            cause,
            context: {
              actual_bytes: receivedBytes,
              maximum_bytes: maximumBytes,
            },
          },
        );
      }

      throw new SimulationApiError(
        'SIMULATION.CONFIGURATION_RESPONSE_TOO_LARGE',
        'Configuration response exceeds the protocol body limit.',
        {
          context: { actual_bytes: receivedBytes, maximum_bytes: maximumBytes },
        },
      );
    }
    chunks.push(readResult.value);
  }

  const responseBytes = new Uint8Array(receivedBytes);
  let writeOffset = 0;
  for (const chunk of chunks) {
    responseBytes.set(chunk, writeOffset);
    writeOffset += chunk.byteLength;
  }
  return responseBytes;
}

async function readBoundedJsonDocument(
  response: Response,
  maximumBytes: number,
): Promise<unknown> {
  const contentType = response.headers
    .get('content-type')
    ?.split(';', 1)[0]
    ?.trim()
    .toLowerCase();
  if (contentType !== 'application/json') {
    throw new SimulationApiError(
      'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
      'Configuration response must use application/json.',
      { context: { content_type: contentType ?? null } },
    );
  }

  const responseBytes = await readBoundedResponseBytes(response, maximumBytes);
  let responseText: string;
  try {
    responseText = new TextDecoder('utf-8', {
      fatal: true,
      ignoreBOM: true,
    }).decode(responseBytes);
  } catch (cause) {
    throw new SimulationApiError(
      'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
      'Configuration response is not valid UTF-8.',
      { cause },
    );
  }

  if (responseText.startsWith('\uFEFF')) {
    throw new SimulationApiError(
      'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
      'Configuration response must not contain a byte-order mark.',
    );
  }

  try {
    return JSON.parse(responseText) as unknown;
  } catch (cause) {
    throw new SimulationApiError(
      'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
      'Configuration response is not one complete JSON document.',
      { cause },
    );
  }
}

/** @canonical simulation_api -- owns all browser transport for protocol v1. */
export class SimulationApi implements SimulationApiBoundary {
  private readonly endpoints: SimulationEndpoints;
  private readonly fetchImplementation: typeof fetch;
  private readonly webSocketFactory: SimulationWebSocketFactory;
  private activeConfigurationAbortController: AbortController | null = null;
  private configuration: SimulationConfiguration | null = null;
  private configurationPromise: Promise<SimulationConfiguration> | null = null;
  private disposed = false;
  private socket: SimulationWebSocket | null = null;
  private socketConnectTimeout: ReturnType<typeof setTimeout> | null = null;

  constructor(dependencies: SimulationApiDependencies = {}) {
    const browserLocation = dependencies.location ?? window.location;
    this.endpoints = deriveSimulationEndpoints(browserLocation);
    this.fetchImplementation =
      dependencies.fetchImplementation ?? window.fetch.bind(window);
    this.webSocketFactory =
      dependencies.webSocketFactory ??
      ((url, subprotocol) => new WebSocket(url, subprotocol));
  }

  async loadConfiguration(
    externalSignal: AbortSignal,
  ): Promise<SimulationConfiguration> {
    this.assertNotDisposed();
    if (this.configuration !== null) {
      return this.configuration;
    }
    if (this.configurationPromise !== null) {
      return this.configurationPromise;
    }

    const requestPromise = this.fetchConfiguration(externalSignal);
    this.configurationPromise = requestPromise;
    try {
      const configuration = await requestPromise;
      this.assertNotDisposed();
      this.configuration = configuration;
      return configuration;
    } finally {
      if (this.configurationPromise === requestPromise) {
        this.configurationPromise = null;
      }
    }
  }

  openSnapshotStream(
    configuration: SimulationConfiguration,
    callbacks: SimulationSnapshotCallbacks,
  ): void {
    this.assertNotDisposed();
    if (this.socket !== null) {
      throw new SimulationApiError(
        'SIMULATION.SOCKET_ALREADY_OPEN',
        'SimulationApi permits only one WebSocket at a time.',
      );
    }
    if (this.configuration === null || configuration !== this.configuration) {
      throw new SimulationApiError(
        'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
        'Snapshot streaming requires the validated configuration loaded by this SimulationApi.',
      );
    }

    let socket: SimulationWebSocket;
    try {
      socket = this.webSocketFactory(
        this.endpoints.snapshotWebSocketUrl,
        SNAPSHOT_WEBSOCKET_SUBPROTOCOL,
      );
    } catch (cause) {
      throw new SimulationApiError(
        'SIMULATION.SOCKET_TRANSPORT_FAILED',
        'Failed to construct the snapshot WebSocket.',
        { cause, retryable: true },
      );
    }

    this.socket = socket;
    let previousSequence: SnapshotSequenceState | null = null;

    socket.onopen = () => {
      if (this.socket !== socket) {
        return;
      }
      if (socket.protocol !== SNAPSHOT_WEBSOCKET_SUBPROTOCOL) {
        const error = new SimulationApiError(
          'SIMULATION.SOCKET_SUBPROTOCOL_INVALID',
          'Snapshot WebSocket did not negotiate the required subprotocol.',
          { context: { negotiated_subprotocol: socket.protocol } },
        );
        this.releaseSocket(socket, 1002, 'protocol_error');
        callbacks.onFailure(error);
        return;
      }
      this.clearSocketConnectTimeout();
      callbacks.onConnected();
    };

    socket.onmessage = (event) => {
      if (this.socket !== socket) {
        return;
      }

      try {
        if (typeof event.data !== 'string') {
          throw new SimulationApiError(
            'SIMULATION.SNAPSHOT_FRAME_INVALID',
            'Snapshot WebSocket frames must be UTF-8 text.',
          );
        }

        const frameBytes = new TextEncoder().encode(event.data).byteLength;
        const maximumFrameBytes = Math.min(
          configuration.presentation.snapshot_frame_max_bytes,
          SNAPSHOT_FRAME_MAX_BYTES,
        );
        if (frameBytes > maximumFrameBytes) {
          throw new SimulationApiError(
            'SIMULATION.SNAPSHOT_FRAME_TOO_LARGE',
            'Snapshot frame exceeds the protocol byte limit.',
            {
              context: {
                actual_bytes: frameBytes,
                maximum_bytes: maximumFrameBytes,
              },
            },
          );
        }

        let untrustedDocument: unknown;
        try {
          untrustedDocument = JSON.parse(event.data) as unknown;
        } catch (cause) {
          throw new SimulationApiError(
            'SIMULATION.SNAPSHOT_FRAME_INVALID',
            'Snapshot frame is not one complete JSON document.',
            { cause },
          );
        }

        const snapshot = validateSimulationSnapshotMessage(
          untrustedDocument,
          configuration,
          previousSequence,
        );
        previousSequence = Object.freeze({
          messageSequence: snapshot.meta.message_sequence,
          requestId: snapshot.meta.request_id,
          tickSequence: snapshot.data.tick_sequence,
        });
        callbacks.onSnapshot(snapshot);
      } catch (error) {
        const simulationError =
          error instanceof SimulationApiError
            ? error
            : new SimulationApiError(
                'SIMULATION.SNAPSHOT_FRAME_INVALID',
                'Snapshot frame validation failed unexpectedly.',
                { cause: error },
              );
        const frameTooLarge =
          simulationError.code === 'SIMULATION.SNAPSHOT_FRAME_TOO_LARGE';
        this.releaseSocket(
          socket,
          frameTooLarge ? 1009 : 1002,
          frameTooLarge ? 'snapshot_frame_too_large' : 'protocol_error',
        );
        callbacks.onFailure(simulationError);
      }
    };

    socket.onerror = () => {
      if (this.socket !== socket) {
        return;
      }
      const error = new SimulationApiError(
        'SIMULATION.SOCKET_TRANSPORT_FAILED',
        'Snapshot WebSocket transport failed.',
        { retryable: true },
      );
      this.releaseSocket(socket, 4000, 'transport_failure');
      callbacks.onDisconnected(
        Object.freeze({
          code: null,
          error,
          reason: 'transport_failure',
          retryable: true,
          wasClean: false,
        }),
      );
    };

    socket.onclose = (event) => {
      if (this.socket !== socket) {
        return;
      }
      this.clearSocketConnectTimeout();
      this.detachSocket(socket);
      this.socket = null;
      callbacks.onDisconnected(
        Object.freeze({
          code: event.code,
          error: null,
          reason: event.reason,
          retryable: isRetryableCloseCode(event.code),
          wasClean: event.wasClean,
        }),
      );
    };

    this.socketConnectTimeout = setTimeout(() => {
      if (this.socket !== socket) {
        return;
      }
      const error = new SimulationApiError(
        'SIMULATION.SOCKET_CONNECT_TIMED_OUT',
        'Snapshot WebSocket did not connect before the timeout.',
        {
          context: {
            timeout_milliseconds: WEBSOCKET_CONNECT_TIMEOUT_MILLISECONDS,
          },
          retryable: true,
        },
      );
      this.releaseSocket(socket, 4000, 'connect_timeout');
      callbacks.onDisconnected(
        Object.freeze({
          code: null,
          error,
          reason: 'connect_timeout',
          retryable: true,
          wasClean: false,
        }),
      );
    }, WEBSOCKET_CONNECT_TIMEOUT_MILLISECONDS);
  }

  dispose(): void {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    this.activeConfigurationAbortController?.abort();
    this.activeConfigurationAbortController = null;

    if (this.socket !== null) {
      this.releaseSocket(this.socket, 1000, 'normal');
    }
  }

  private assertNotDisposed(): void {
    if (this.disposed) {
      throw new SimulationApiError(
        'SIMULATION.SOCKET_DISPOSED',
        'SimulationApi cannot be used after disposal.',
      );
    }
  }

  private async fetchConfiguration(
    externalSignal: AbortSignal,
  ): Promise<SimulationConfiguration> {
    const internalAbortController = new AbortController();
    this.activeConfigurationAbortController = internalAbortController;
    const abortInternalRequest = () => {
      internalAbortController.abort(externalSignal.reason);
    };
    if (externalSignal.aborted) {
      abortInternalRequest();
    } else {
      externalSignal.addEventListener('abort', abortInternalRequest, {
        once: true,
      });
    }

    let configurationTimeout: ReturnType<typeof setTimeout> | null = null;
    const timeoutPromise = new Promise<never>((_resolve, reject) => {
      configurationTimeout = setTimeout(() => {
        reject(
          new SimulationApiError(
            'SIMULATION.CONFIGURATION_REQUEST_TIMED_OUT',
            'Configuration request did not complete before the timeout.',
            {
              context: {
                timeout_milliseconds: CONFIGURATION_FETCH_TIMEOUT_MILLISECONDS,
              },
              retryable: true,
            },
          ),
        );
        internalAbortController.abort('configuration_request_timeout');
      }, CONFIGURATION_FETCH_TIMEOUT_MILLISECONDS);
    });

    try {
      return await Promise.race([
        this.fetchAndValidateConfiguration(internalAbortController.signal),
        timeoutPromise,
      ]);
    } finally {
      if (configurationTimeout !== null) {
        clearTimeout(configurationTimeout);
      }
      externalSignal.removeEventListener('abort', abortInternalRequest);
      if (this.activeConfigurationAbortController === internalAbortController) {
        this.activeConfigurationAbortController = null;
      }
    }
  }

  private async fetchAndValidateConfiguration(
    signal: AbortSignal,
  ): Promise<SimulationConfiguration> {
    let response: Response;
    try {
      response = await this.fetchImplementation(
        this.endpoints.configurationUrl,
        {
          cache: 'no-store',
          credentials: 'omit',
          headers: { Accept: 'application/json' },
          method: 'GET',
          redirect: 'error',
          signal,
        },
      );
    } catch (cause) {
      if (signal.aborted) {
        throw new SimulationApiError(
          'SIMULATION.CONFIGURATION_REQUEST_ABORTED',
          'Configuration request was aborted.',
          { cause },
        );
      }
      throw new SimulationApiError(
        'SIMULATION.CONFIGURATION_REQUEST_FAILED',
        'Configuration request failed before a response was received.',
        { cause, retryable: true },
      );
    }

    let responseDocument: unknown;
    try {
      responseDocument = await readBoundedJsonDocument(
        response,
        CONFIGURATION_RESPONSE_MAX_BYTES,
      );
    } catch (error) {
      if (error instanceof SimulationApiError) {
        throw error;
      }
      if (signal.aborted) {
        throw new SimulationApiError(
          'SIMULATION.CONFIGURATION_REQUEST_ABORTED',
          'Configuration response read was aborted.',
          { cause: error },
        );
      }
      throw new SimulationApiError(
        'SIMULATION.CONFIGURATION_REQUEST_FAILED',
        'Configuration response body could not be read.',
        { cause: error, retryable: true },
      );
    }
    if (response.status !== 200) {
      const errorResponse = validateSimulationHttpErrorResponse(
        responseDocument,
        response.status,
        response.headers.get('x-request-id'),
      );
      throw new SimulationApiError(
        'SIMULATION.CONFIGURATION_REQUEST_FAILED',
        errorResponse.error.message,
        {
          context: {
            http_status: response.status,
            protocol_error_code: errorResponse.error.code,
          },
          retryable: errorResponse.error.retryable,
        },
      );
    }

    return validateSimulationConfigurationResponse(
      responseDocument,
      response.headers.get('x-request-id'),
    ).data;
  }

  private detachSocket(socket: SimulationWebSocket): void {
    socket.onclose = null;
    socket.onerror = null;
    socket.onmessage = null;
    socket.onopen = null;
  }

  private releaseSocket(
    socket: SimulationWebSocket,
    closeCode: number,
    closeReason: string,
  ): void {
    if (this.socket !== socket) {
      return;
    }
    this.clearSocketConnectTimeout();
    this.detachSocket(socket);
    this.socket = null;
    if (
      socket.readyState === WEBSOCKET_CONNECTING ||
      socket.readyState === WEBSOCKET_OPEN
    ) {
      socket.close(closeCode, closeReason);
    }
  }

  private clearSocketConnectTimeout(): void {
    if (this.socketConnectTimeout === null) {
      return;
    }
    clearTimeout(this.socketConnectTimeout);
    this.socketConnectTimeout = null;
  }
}
