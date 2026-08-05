export type SimulationApiErrorCode =
  | 'SIMULATION.CONFIGURATION_REQUEST_ABORTED'
  | 'SIMULATION.CONFIGURATION_REQUEST_FAILED'
  | 'SIMULATION.CONFIGURATION_REQUEST_TIMED_OUT'
  | 'SIMULATION.CONFIGURATION_RESPONSE_INVALID'
  | 'SIMULATION.CONFIGURATION_RESPONSE_TOO_LARGE'
  | 'SIMULATION.ENDPOINT_INVALID'
  | 'SIMULATION.HTTP_ERROR_RESPONSE_INVALID'
  | 'SIMULATION.RECONNECT_EXHAUSTED'
  | 'SIMULATION.SNAPSHOT_FRAME_INVALID'
  | 'SIMULATION.SNAPSHOT_FRAME_TOO_LARGE'
  | 'SIMULATION.SNAPSHOT_INVARIANT_VIOLATION'
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

/** A structured failure raised by the read-only simulation client boundary. */
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
