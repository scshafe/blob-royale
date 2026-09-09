import {
  SimulationApiError,
  type SimulationApiErrorCode,
} from './SimulationApiError';
import {
  COMMAND_MESSAGE_MAX_BYTES,
  CONFIGURATION_ENDPOINT_PATH,
  CONFIGURATION_FETCH_TIMEOUT_MILLISECONDS,
  CONFIGURATION_RESPONSE_MAX_BYTES,
  LOBBY_DIRECTORY_ENDPOINT_PATH,
  LOBBY_DIRECTORY_FETCH_TIMEOUT_MILLISECONDS,
  LOBBY_DIRECTORY_RESPONSE_MAX_BYTES,
  LOBBY_ID_PATTERN,
  ROOM_SESSION_ENDPOINT_PATH_PREFIX,
  ROOM_SESSION_ENDPOINT_PATH_SUFFIX,
  SESSION_FRAME_MAX_BYTES,
  SESSION_WEBSOCKET_SUBPROTOCOL,
  WEBSOCKET_CONNECT_TIMEOUT_MILLISECONDS,
} from './simulationConstants';
import type {
  SessionCommand,
  SessionCommandKind,
  SessionLobbyListing,
  SessionSnapshotMessage,
  SessionWelcomeMessage,
  SimulationConfiguration,
} from './simulationProtocolTypes';
import {
  validateSimulationConfigurationResponse,
  validateSimulationHttpErrorResponse,
} from './simulationProtocolValidation';
import {
  type SessionSequenceState,
  validateLobbyDirectoryMessage,
  validateSessionCommand,
  validateSessionHttpErrorResponse,
  validateSessionSnapshotMessage,
  validateSessionWelcomeMessage,
} from './sessionProtocolValidation';

const WEBSOCKET_CONNECTING = 0;
const WEBSOCKET_OPEN = 1;

export interface SimulationBrowserLocation {
  readonly host: string;
  readonly origin: string;
  readonly protocol: string;
}

export interface SimulationEndpoints {
  readonly configurationUrl: string;
  readonly lobbyDirectoryUrl: string;
  /** `ws:` or `wss:` on the page's own authority; a room's session target is appended per join. */
  readonly webSocketOrigin: string;
}

export interface SimulationWebSocket {
  readonly protocol: string;
  readonly readyState: number;
  onclose: ((event: CloseEvent) => void) | null;
  onerror: ((event: Event) => void) | null;
  onmessage: ((event: MessageEvent<unknown>) => void) | null;
  onopen: ((event: Event) => void) | null;
  close(code?: number, reason?: string): void;
  send(data: string): void;
}

export type SimulationWebSocketFactory = (
  url: string,
  subprotocol: string,
) => SimulationWebSocket;

export interface SimulationDisconnection {
  readonly code: number | null;
  readonly error: SimulationApiError | null;
  /**
   * Whether the socket had opened before it closed. A browser never sees the HTTP response a
   * declined upgrade was refused with, so "closed without ever opening" is the whole of what a
   * `404`, `409`, or `503` looks like from here; the caller reads the directory to say which.
   */
  readonly opened: boolean;
  readonly reason: string;
  readonly retryable: boolean;
  readonly wasClean: boolean;
}

export interface SimulationSessionCallbacks {
  readonly onConnected: () => void;
  readonly onDisconnected: (disconnection: SimulationDisconnection) => void;
  readonly onFailure: (error: SimulationApiError) => void;
  readonly onSnapshot: (snapshot: SessionSnapshotMessage) => void;
  readonly onWelcome: (welcome: SessionWelcomeMessage) => void;
}

export interface SimulationApiBoundary {
  loadConfiguration(signal: AbortSignal): Promise<SimulationConfiguration>;
  fetchLobbies(signal: AbortSignal): Promise<readonly SessionLobbyListing[]>;
  openSession(
    configuration: SimulationConfiguration,
    lobbyId: number,
    callbacks: SimulationSessionCallbacks,
  ): void;
  sendCommand(command: SessionCommand): boolean;
  dispose(): void;
}

export interface SimulationApiDependencies {
  readonly fetchImplementation?: typeof fetch;
  readonly location?: SimulationBrowserLocation;
  readonly webSocketFactory?: SimulationWebSocketFactory;
}

/**
 * Derives the exact configuration, directory, and WebSocket endpoints from one validated browser
 * authority. Configuration stays on protocol v1 because v2 deliberately adds no second source for
 * one set of numbers; the directory and the room session targets are v2's.
 */
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
  const lobbyDirectoryUrl = new URL(LOBBY_DIRECTORY_ENDPOINT_PATH, pageOrigin);
  const webSocketOrigin = new URL(pageOrigin);
  webSocketOrigin.protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';

  return Object.freeze({
    configurationUrl: configurationUrl.href,
    lobbyDirectoryUrl: lobbyDirectoryUrl.href,
    webSocketOrigin: webSocketOrigin.origin,
  });
}

/**
 * The session target of one room, `/api/v2/lobbies/<lobby_id>/session`. The id must already be
 * one the grammar admits: this client never builds a target from a string the server did not
 * publish, and a number outside `[1-9][0-9]{0,2}` is a defect in whatever produced it.
 */
export function roomSessionWebSocketUrl(
  endpoints: SimulationEndpoints,
  lobbyId: number,
): string {
  if (
    !Number.isSafeInteger(lobbyId) ||
    !LOBBY_ID_PATTERN.test(String(lobbyId))
  ) {
    throw new SimulationApiError(
      'SIMULATION.ENDPOINT_INVALID',
      'A room session target needs a lobby id in [1-9][0-9]{0,2}.',
      { context: { lobby_id: lobbyId } },
    );
  }
  return `${endpoints.webSocketOrigin}${ROOM_SESSION_ENDPOINT_PATH_PREFIX}${lobbyId}${ROOM_SESSION_ENDPOINT_PATH_SUFFIX}`;
}

function isRetryableCloseCode(closeCode: number): boolean {
  return [1001, 1006, 1011, 1012, 1013].includes(closeCode);
}

/** Which HTTP document a bounded read is reading, for the error it raises when it cannot. */
interface BoundedResponseSubject {
  readonly invalidCode: SimulationApiErrorCode;
  readonly label: string;
  readonly tooLargeCode: SimulationApiErrorCode;
}

const CONFIGURATION_RESPONSE_SUBJECT: BoundedResponseSubject = Object.freeze({
  invalidCode: 'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
  label: 'Configuration',
  tooLargeCode: 'SIMULATION.CONFIGURATION_RESPONSE_TOO_LARGE',
});

const LOBBY_DIRECTORY_RESPONSE_SUBJECT: BoundedResponseSubject = Object.freeze({
  invalidCode: 'SIMULATION.LOBBY_DIRECTORY_RESPONSE_INVALID',
  label: 'Lobby directory',
  tooLargeCode: 'SIMULATION.LOBBY_DIRECTORY_RESPONSE_TOO_LARGE',
});

function parseContentLength(
  contentLength: string | null,
  subject: BoundedResponseSubject,
): number | null {
  if (contentLength === null) {
    return null;
  }

  if (!/^(?:0|[1-9][0-9]*)$/.test(contentLength)) {
    throw new SimulationApiError(
      subject.invalidCode,
      `${subject.label} response Content-Length is invalid.`,
      { context: { content_length: contentLength } },
    );
  }

  const parsedContentLength = Number(contentLength);
  if (!Number.isSafeInteger(parsedContentLength)) {
    throw new SimulationApiError(
      subject.tooLargeCode,
      `${subject.label} response Content-Length exceeds the safe integer range.`,
      { context: { content_length: contentLength } },
    );
  }

  return parsedContentLength;
}

async function readBoundedResponseBytes(
  response: Response,
  maximumBytes: number,
  subject: BoundedResponseSubject,
): Promise<Uint8Array> {
  const declaredLength = parseContentLength(
    response.headers.get('content-length'),
    subject,
  );
  if (declaredLength !== null && declaredLength > maximumBytes) {
    throw new SimulationApiError(
      subject.tooLargeCode,
      `${subject.label} response exceeds the protocol body limit.`,
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
        subject.tooLargeCode,
        `${subject.label} response exceeds the protocol body limit.`,
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
        subject.invalidCode,
        `${subject.label} response stream returned an empty chunk.`,
      );
    }

    receivedBytes += readResult.value.byteLength;
    if (receivedBytes > maximumBytes) {
      try {
        await reader.cancel('configuration_response_too_large');
      } catch (cause) {
        throw new SimulationApiError(
          subject.tooLargeCode,
          `${subject.label} response exceeded the protocol body limit and stream cancellation failed.`,
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
        subject.tooLargeCode,
        `${subject.label} response exceeds the protocol body limit.`,
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
  subject: BoundedResponseSubject,
): Promise<unknown> {
  const contentType = response.headers
    .get('content-type')
    ?.split(';', 1)[0]
    ?.trim()
    .toLowerCase();
  if (contentType !== 'application/json') {
    throw new SimulationApiError(
      subject.invalidCode,
      `${subject.label} response must use application/json.`,
      { context: { content_type: contentType ?? null } },
    );
  }

  const responseBytes = await readBoundedResponseBytes(
    response,
    maximumBytes,
    subject,
  );
  let responseText: string;
  try {
    responseText = new TextDecoder('utf-8', {
      fatal: true,
      ignoreBOM: true,
    }).decode(responseBytes);
  } catch (cause) {
    throw new SimulationApiError(
      subject.invalidCode,
      `${subject.label} response is not valid UTF-8.`,
      { cause },
    );
  }

  if (responseText.startsWith('\uFEFF')) {
    throw new SimulationApiError(
      subject.invalidCode,
      `${subject.label} response must not contain a byte-order mark.`,
    );
  }

  try {
    return JSON.parse(responseText) as unknown;
  } catch (cause) {
    throw new SimulationApiError(
      subject.invalidCode,
      `${subject.label} response is not one complete JSON document.`,
      { cause },
    );
  }
}

interface SessionCloseIntent {
  readonly code: number;
  readonly reason: string;
}

/**
 * Maps a decode failure to the close the protocol names for it. Failing closed rather than ignoring
 * the frame is the accepted rule: an unrenderable frame means the client and the server disagree
 * about what the world is, and a client that keeps rendering is quietly wrong.
 */
function sessionCloseIntentFor(error: SimulationApiError): SessionCloseIntent {
  switch (error.code) {
    case 'SIMULATION.SESSION_FRAME_TOO_LARGE':
      return { code: 1009, reason: 'session_frame_too_large' };
    case 'SIMULATION.SESSION_VERSION_UNSUPPORTED':
      return { code: 1003, reason: 'client_version_unsupported' };
    case 'SIMULATION.SESSION_KIND_UNSUPPORTED':
      return { code: 1003, reason: 'client_kind_unsupported' };
    default:
      return { code: 1002, reason: 'protocol_error' };
  }
}

/** @canonical simulation_api -- owns all browser transport for protocol v2 sessions. */
export class SimulationApi implements SimulationApiBoundary {
  private readonly endpoints: SimulationEndpoints;
  private readonly fetchImplementation: typeof fetch;
  private readonly webSocketFactory: SimulationWebSocketFactory;
  private acceptedCommandKinds: ReadonlySet<SessionCommandKind> | null = null;
  private readonly activeAbortControllers = new Set<AbortController>();
  private configuration: SimulationConfiguration | null = null;
  private configurationPromise: Promise<SimulationConfiguration> | null = null;
  private disposed = false;
  private sequenceState: SessionSequenceState | null = null;
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

  /**
   * Reads the lobby directory: every room the server runs, as of the instant it was read. Never
   * cached, because it is advice about where to ask and a second's staleness is its whole value.
   */
  async fetchLobbies(
    externalSignal: AbortSignal,
  ): Promise<readonly SessionLobbyListing[]> {
    this.assertNotDisposed();
    return this.fetchWithTimeout(
      externalSignal,
      LOBBY_DIRECTORY_FETCH_TIMEOUT_MILLISECONDS,
      'SIMULATION.LOBBY_DIRECTORY_REQUEST_TIMED_OUT',
      'Lobby directory request did not complete before the timeout.',
      (signal) => this.fetchAndValidateLobbyDirectory(signal),
    );
  }

  /**
   * Joins one room. Connecting is joining and closing is leaving, so this method is the whole join
   * lifecycle; a reconnect is a new join with a new controller id and nothing resumed, and the room
   * is fixed for the socket's life.
   */
  openSession(
    configuration: SimulationConfiguration,
    lobbyId: number,
    callbacks: SimulationSessionCallbacks,
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
        'Joining a match session requires the validated configuration loaded by this SimulationApi.',
      );
    }

    const sessionWebSocketUrl = roomSessionWebSocketUrl(
      this.endpoints,
      lobbyId,
    );
    let socket: SimulationWebSocket;
    try {
      socket = this.webSocketFactory(
        sessionWebSocketUrl,
        SESSION_WEBSOCKET_SUBPROTOCOL,
      );
    } catch (cause) {
      throw new SimulationApiError(
        'SIMULATION.SOCKET_TRANSPORT_FAILED',
        'Failed to construct the session WebSocket.',
        { cause, retryable: true },
      );
    }

    this.socket = socket;
    this.sequenceState = null;
    this.acceptedCommandKinds = null;
    let opened = false;

    socket.onopen = () => {
      if (this.socket !== socket) {
        return;
      }
      if (socket.protocol !== SESSION_WEBSOCKET_SUBPROTOCOL) {
        const error = new SimulationApiError(
          'SIMULATION.SOCKET_SUBPROTOCOL_INVALID',
          'Session WebSocket did not negotiate the required subprotocol.',
          { context: { negotiated_subprotocol: socket.protocol } },
        );
        this.releaseSocket(socket, 1002, 'protocol_error');
        callbacks.onFailure(error);
        return;
      }
      this.clearSocketConnectTimeout();
      opened = true;
      // An open socket with no frames is a joiner the mode has deferred until the next lobby, not a
      // stalled connection: the welcome cannot exist before the session owns a body.
      callbacks.onConnected();
    };

    socket.onmessage = (event) => {
      if (this.socket !== socket) {
        return;
      }

      try {
        this.handleSessionFrame(event, callbacks);
      } catch (error) {
        const simulationError =
          error instanceof SimulationApiError
            ? error
            : new SimulationApiError(
                'SIMULATION.SESSION_FRAME_INVALID',
                'Session frame validation failed unexpectedly.',
                { cause: error },
              );
        const closeIntent = sessionCloseIntentFor(simulationError);
        this.releaseSocket(socket, closeIntent.code, closeIntent.reason);
        callbacks.onFailure(simulationError);
      }
    };

    socket.onerror = () => {
      if (this.socket !== socket) {
        return;
      }
      const error = new SimulationApiError(
        'SIMULATION.SOCKET_TRANSPORT_FAILED',
        'Session WebSocket transport failed.',
        { retryable: true },
      );
      this.releaseSocket(socket, 4000, 'transport_failure');
      callbacks.onDisconnected(
        Object.freeze({
          code: null,
          error,
          opened,
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
      this.acceptedCommandKinds = null;
      callbacks.onDisconnected(
        Object.freeze({
          code: event.code,
          error: null,
          opened,
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
        'Session WebSocket did not connect before the timeout.',
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
          opened: false,
          reason: 'connect_timeout',
          retryable: true,
          wasClean: false,
        }),
      );
    }, WEBSOCKET_CONNECT_TIMEOUT_MILLISECONDS);
  }

  /**
   * Sends one command envelope. It is a no-op that reports `false` unless the session is open and
   * has been welcomed with this kind accepted: a command the server would refuse closes the
   * connection, so a client that cannot name a kind never puts it on the wire.
   */
  sendCommand(command: SessionCommand): boolean {
    const socket = this.socket;
    if (
      this.disposed ||
      socket === null ||
      socket.readyState !== WEBSOCKET_OPEN ||
      this.sequenceState === null ||
      this.acceptedCommandKinds === null
    ) {
      return false;
    }
    if (!this.acceptedCommandKinds.has(command.kind)) {
      return false;
    }

    let payload: string;
    try {
      validateSessionCommand(command);
      payload = JSON.stringify(command);
    } catch (error) {
      console.warn(
        JSON.stringify({
          event: 'protocol.v2.command_refused',
          command_kind: command.kind,
          reason:
            error instanceof SimulationApiError ? error.code : 'unexpected',
        }),
      );
      return false;
    }

    if (
      new TextEncoder().encode(payload).byteLength > COMMAND_MESSAGE_MAX_BYTES
    ) {
      console.warn(
        JSON.stringify({
          event: 'protocol.v2.command_refused',
          command_kind: command.kind,
          reason: 'command_message_too_large',
        }),
      );
      return false;
    }

    try {
      socket.send(payload);
    } catch {
      return false;
    }
    return true;
  }

  private handleSessionFrame(
    event: MessageEvent<unknown>,
    callbacks: SimulationSessionCallbacks,
  ): void {
    if (typeof event.data !== 'string') {
      throw new SimulationApiError(
        'SIMULATION.SESSION_FRAME_INVALID',
        'Session WebSocket frames must be UTF-8 text.',
      );
    }

    const frameBytes = new TextEncoder().encode(event.data).byteLength;
    if (frameBytes > SESSION_FRAME_MAX_BYTES) {
      throw new SimulationApiError(
        'SIMULATION.SESSION_FRAME_TOO_LARGE',
        'Session frame exceeds the protocol byte limit.',
        {
          context: {
            actual_bytes: frameBytes,
            maximum_bytes: SESSION_FRAME_MAX_BYTES,
          },
        },
      );
    }

    let untrustedDocument: unknown;
    try {
      untrustedDocument = JSON.parse(event.data) as unknown;
    } catch (cause) {
      throw new SimulationApiError(
        'SIMULATION.SESSION_FRAME_INVALID',
        'Session frame is not one complete JSON document.',
        { cause },
      );
    }

    if (this.sequenceState === null) {
      const welcome = validateSessionWelcomeMessage(untrustedDocument, null);
      this.sequenceState = Object.freeze({
        messageSequence: welcome.meta.message_sequence,
        requestId: welcome.meta.request_id,
        tickSequence: null,
      });
      this.acceptedCommandKinds = new Set(welcome.data.accepted_command_kinds);
      callbacks.onWelcome(welcome);
      return;
    }

    const snapshot = validateSessionSnapshotMessage(
      untrustedDocument,
      this.sequenceState,
    );
    this.sequenceState = Object.freeze({
      messageSequence: snapshot.meta.message_sequence,
      requestId: snapshot.meta.request_id,
      tickSequence: snapshot.data.tick_sequence,
    });
    callbacks.onSnapshot(snapshot);
  }

  dispose(): void {
    if (this.disposed) {
      return;
    }
    this.disposed = true;
    for (const abortController of this.activeAbortControllers) {
      abortController.abort('disposed');
    }
    this.activeAbortControllers.clear();

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

  private fetchConfiguration(
    externalSignal: AbortSignal,
  ): Promise<SimulationConfiguration> {
    return this.fetchWithTimeout(
      externalSignal,
      CONFIGURATION_FETCH_TIMEOUT_MILLISECONDS,
      'SIMULATION.CONFIGURATION_REQUEST_TIMED_OUT',
      'Configuration request did not complete before the timeout.',
      (signal) => this.fetchAndValidateConfiguration(signal),
    );
  }

  /**
   * One HTTP read under one deadline and one abort: the caller's signal, disposal, and the timeout
   * all abort the same internal controller, and the timeout is the retryable error it names.
   */
  private async fetchWithTimeout<T>(
    externalSignal: AbortSignal,
    timeoutMilliseconds: number,
    timeoutCode: SimulationApiErrorCode,
    timeoutMessage: string,
    operation: (signal: AbortSignal) => Promise<T>,
  ): Promise<T> {
    const internalAbortController = new AbortController();
    this.activeAbortControllers.add(internalAbortController);
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

    let requestTimeout: ReturnType<typeof setTimeout> | null = null;
    const timeoutPromise = new Promise<never>((_resolve, reject) => {
      requestTimeout = setTimeout(() => {
        reject(
          new SimulationApiError(timeoutCode, timeoutMessage, {
            context: { timeout_milliseconds: timeoutMilliseconds },
            retryable: true,
          }),
        );
        internalAbortController.abort('request_timeout');
      }, timeoutMilliseconds);
    });

    try {
      return await Promise.race([
        operation(internalAbortController.signal),
        timeoutPromise,
      ]);
    } finally {
      if (requestTimeout !== null) {
        clearTimeout(requestTimeout);
      }
      externalSignal.removeEventListener('abort', abortInternalRequest);
      this.activeAbortControllers.delete(internalAbortController);
    }
  }

  private async fetchAndValidateLobbyDirectory(
    signal: AbortSignal,
  ): Promise<readonly SessionLobbyListing[]> {
    let response: Response;
    try {
      response = await this.fetchImplementation(
        this.endpoints.lobbyDirectoryUrl,
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
          'SIMULATION.LOBBY_DIRECTORY_REQUEST_ABORTED',
          'Lobby directory request was aborted.',
          { cause },
        );
      }
      throw new SimulationApiError(
        'SIMULATION.LOBBY_DIRECTORY_REQUEST_FAILED',
        'Lobby directory request failed before a response was received.',
        { cause, retryable: true },
      );
    }

    let responseDocument: unknown;
    try {
      responseDocument = await readBoundedJsonDocument(
        response,
        LOBBY_DIRECTORY_RESPONSE_MAX_BYTES,
        LOBBY_DIRECTORY_RESPONSE_SUBJECT,
      );
    } catch (error) {
      if (error instanceof SimulationApiError) {
        throw error;
      }
      if (signal.aborted) {
        throw new SimulationApiError(
          'SIMULATION.LOBBY_DIRECTORY_REQUEST_ABORTED',
          'Lobby directory response read was aborted.',
          { cause: error },
        );
      }
      throw new SimulationApiError(
        'SIMULATION.LOBBY_DIRECTORY_REQUEST_FAILED',
        'Lobby directory response body could not be read.',
        { cause: error, retryable: true },
      );
    }
    if (response.status !== 200) {
      // A `/api/v2/` target fails in the v2 envelope, whose registry is v1's plus the lobby rows.
      const errorResponse = validateSessionHttpErrorResponse(
        responseDocument,
        response.status,
        response.headers.get('x-request-id'),
      );
      throw new SimulationApiError(
        'SIMULATION.LOBBY_DIRECTORY_REQUEST_FAILED',
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

    return validateLobbyDirectoryMessage(
      responseDocument,
      response.headers.get('x-request-id'),
    ).data.lobbies;
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
        CONFIGURATION_RESPONSE_SUBJECT,
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
    this.acceptedCommandKinds = null;
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
