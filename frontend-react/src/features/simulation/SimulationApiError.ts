export type SimulationApiErrorCode =
  | 'SIMULATION.CAMERA_OFFSET_INVALID'
  | 'SIMULATION.CAMERA_PROJECTION_INVALID'
  | 'SIMULATION.CAMERA_VIEWPORT_INVALID'
  | 'SIMULATION.COMMAND_REJECTED'
  | 'SIMULATION.CONFIGURATION_REQUEST_ABORTED'
  | 'SIMULATION.CONFIGURATION_REQUEST_FAILED'
  | 'SIMULATION.CONFIGURATION_REQUEST_TIMED_OUT'
  | 'SIMULATION.CONFIGURATION_RESPONSE_INVALID'
  | 'SIMULATION.CONFIGURATION_RESPONSE_TOO_LARGE'
  | 'SIMULATION.ENDPOINT_INVALID'
  | 'SIMULATION.HTTP_ERROR_RESPONSE_INVALID'
  | 'SIMULATION.LOBBY_DIRECTORY_REQUEST_ABORTED'
  | 'SIMULATION.LOBBY_DIRECTORY_REQUEST_FAILED'
  | 'SIMULATION.LOBBY_DIRECTORY_REQUEST_TIMED_OUT'
  | 'SIMULATION.LOBBY_DIRECTORY_RESPONSE_INVALID'
  | 'SIMULATION.LOBBY_DIRECTORY_RESPONSE_TOO_LARGE'
  | 'SIMULATION.RECONNECT_EXHAUSTED'
  | 'SIMULATION.ROOM_REFUSED'
  | 'SIMULATION.SESSION_FRAME_INVALID'
  | 'SIMULATION.SESSION_FRAME_TOO_LARGE'
  | 'SIMULATION.SESSION_INVARIANT_VIOLATION'
  | 'SIMULATION.SESSION_KIND_UNSUPPORTED'
  | 'SIMULATION.SESSION_VERSION_UNSUPPORTED'
  | 'SIMULATION.SOCKET_ALREADY_OPEN'
  | 'SIMULATION.SOCKET_CONNECT_TIMED_OUT'
  | 'SIMULATION.SOCKET_DISPOSED'
  | 'SIMULATION.SOCKET_SUBPROTOCOL_INVALID'
  | 'SIMULATION.SOCKET_TRANSPORT_FAILED';

export interface SimulationApiErrorOptions {
  readonly cause?: unknown;
  readonly context?: Readonly<Record<string, unknown>>;
  readonly retryable?: boolean;
}

/** A structured failure raised by the simulation client boundary. */
export class SimulationApiError extends Error {
  readonly code: SimulationApiErrorCode;
  readonly context: Readonly<Record<string, unknown>>;
  readonly retryable: boolean;

  constructor(
    code: SimulationApiErrorCode,
    message: string,
    options: SimulationApiErrorOptions = {},
  ) {
    super(message, { cause: options.cause });
    this.name = 'SimulationApiError';
    this.code = code;
    this.context = Object.freeze({ ...(options.context ?? {}) });
    this.retryable = options.retryable ?? false;
  }
}
