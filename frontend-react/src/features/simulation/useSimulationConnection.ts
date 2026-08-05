import { useEffect, useReducer } from 'react';

import {
  SimulationApi,
  type SimulationApiBoundary,
  type SimulationDisconnection,
} from './SimulationApi';
import { SimulationApiError } from './SimulationApiError';
import { RECONNECT_BACKOFF_MILLISECONDS } from './simulationConstants';
import type {
  SimulationConfiguration,
  SimulationSnapshotMessage,
} from './simulationProtocolTypes';

export type SimulationConnectionStatus =
  'loading_configuration' | 'connecting' | 'connected' | 'retrying' | 'failed';

export interface SimulationConnectionState {
  readonly configuration: SimulationConfiguration | null;
  readonly error: SimulationApiError | null;
  readonly reconnectAttempt: number;
  readonly snapshot: SimulationSnapshotMessage | null;
  readonly status: SimulationConnectionStatus;
}

type SimulationConnectionAction =
  | { readonly type: 'attempt_started' }
  | {
      readonly type: 'configuration_loaded';
      readonly configuration: SimulationConfiguration;
    }
  | { readonly type: 'connected' }
  | {
      readonly type: 'snapshot_received';
      readonly snapshot: SimulationSnapshotMessage;
    }
  | {
      readonly type: 'retry_scheduled';
      readonly attempt: number;
      readonly error: SimulationApiError;
    }
  | { readonly type: 'failed'; readonly error: SimulationApiError };

export type SimulationApiFactory = () => SimulationApiBoundary;

export const initialSimulationConnectionState: SimulationConnectionState =
  Object.freeze({
    configuration: null,
    error: null,
    reconnectAttempt: 0,
    snapshot: null,
    status: 'loading_configuration',
  });

/** Reduces the complete connection state so incompatible booleans cannot exist. */
export function simulationConnectionReducer(
  state: SimulationConnectionState,
  action: SimulationConnectionAction,
): SimulationConnectionState {
  switch (action.type) {
    case 'attempt_started':
      return Object.freeze({
        ...state,
        configuration: null,
        error: null,
        snapshot: null,
        status: 'loading_configuration',
      });
    case 'configuration_loaded':
      return Object.freeze({
        ...state,
        configuration: action.configuration,
        error: null,
        status: 'connecting',
      });
    case 'connected':
      return Object.freeze({ ...state, error: null, status: 'connected' });
    case 'snapshot_received':
      return Object.freeze({
        ...state,
        error: null,
        reconnectAttempt: 0,
        snapshot: action.snapshot,
        status: 'connected',
      });
    case 'retry_scheduled':
      return Object.freeze({
        ...state,
        configuration: null,
        error: action.error,
        reconnectAttempt: action.attempt,
        snapshot: null,
        status: 'retrying',
      });
    case 'failed':
      return Object.freeze({ ...state, error: action.error, status: 'failed' });
  }
}

function createDefaultSimulationApi(): SimulationApiBoundary {
  return new SimulationApi();
}

function normalizeConnectionError(error: unknown): SimulationApiError {
  return error instanceof SimulationApiError
    ? error
    : new SimulationApiError(
        'SIMULATION.SOCKET_TRANSPORT_FAILED',
        'Simulation connection failed unexpectedly.',
        { cause: error, retryable: false },
      );
}

function errorFromDisconnection(
  disconnection: SimulationDisconnection,
): SimulationApiError {
  if (disconnection.error !== null) {
    return disconnection.error;
  }

  return new SimulationApiError(
    'SIMULATION.SOCKET_TRANSPORT_FAILED',
    disconnection.reason === ''
      ? 'Snapshot WebSocket closed.'
      : `Snapshot WebSocket closed: ${disconnection.reason}`,
    {
      context: {
        close_code: disconnection.code,
        was_clean: disconnection.wasClean,
      },
      retryable: disconnection.retryable,
    },
  );
}

/** @canonical simulation_connection -- owns config, retry, and cleanup policy. */
export function useSimulationConnection(
  apiFactory: SimulationApiFactory = createDefaultSimulationApi,
): SimulationConnectionState {
  const [state, dispatch] = useReducer(
    simulationConnectionReducer,
    initialSimulationConnectionState,
  );

  useEffect(() => {
    let activeAbortController: AbortController | null = null;
    let activeApi: SimulationApiBoundary | null = null;
    let activeAttemptId = 0;
    let mounted = true;
    let reconnectAttempts = 0;
    let retryTimer: ReturnType<typeof setTimeout> | null = null;
    let terminal = false;

    const disposeActiveAttempt = (): void => {
      activeAbortController?.abort();
      activeAbortController = null;
      activeApi?.dispose();
      activeApi = null;
    };

    const isCurrentAttempt = (
      apiForAttempt: SimulationApiBoundary,
      attemptId: number,
    ): boolean =>
      mounted &&
      !terminal &&
      activeApi === apiForAttempt &&
      activeAttemptId === attemptId;

    const fail = (error: SimulationApiError): void => {
      if (!mounted || terminal) {
        return;
      }
      terminal = true;
      disposeActiveAttempt();
      dispatch({ error, type: 'failed' });
    };

    const scheduleRetry = (
      apiForAttempt: SimulationApiBoundary,
      attemptId: number,
      error: SimulationApiError,
    ): void => {
      if (!isCurrentAttempt(apiForAttempt, attemptId)) {
        return;
      }
      if (!error.retryable) {
        fail(error);
        return;
      }

      const retryDelay = RECONNECT_BACKOFF_MILLISECONDS[reconnectAttempts];
      if (retryDelay === undefined) {
        fail(
          new SimulationApiError(
            'SIMULATION.RECONNECT_EXHAUSTED',
            'Simulation reconnect budget was exhausted.',
            {
              cause: error,
              context: {
                last_error_code: error.code,
                reconnect_attempts: reconnectAttempts,
              },
            },
          ),
        );
        return;
      }

      reconnectAttempts += 1;
      disposeActiveAttempt();
      dispatch({
        attempt: reconnectAttempts,
        error,
        type: 'retry_scheduled',
      });
      retryTimer = setTimeout(() => {
        retryTimer = null;
        if (!mounted || terminal) {
          return;
        }
        void beginConnectionAttempt();
      }, retryDelay);
    };

    const beginConnectionAttempt = async (): Promise<void> => {
      if (!mounted || terminal) {
        return;
      }

      disposeActiveAttempt();
      const apiForAttempt = apiFactory();
      const abortControllerForAttempt = new AbortController();
      activeApi = apiForAttempt;
      activeAbortController = abortControllerForAttempt;
      activeAttemptId += 1;
      const attemptId = activeAttemptId;
      dispatch({ type: 'attempt_started' });

      try {
        const configuration = await apiForAttempt.loadConfiguration(
          abortControllerForAttempt.signal,
        );
        if (!isCurrentAttempt(apiForAttempt, attemptId)) {
          return;
        }
        dispatch({ configuration, type: 'configuration_loaded' });

        apiForAttempt.openSnapshotStream(configuration, {
          onConnected: () => {
            if (isCurrentAttempt(apiForAttempt, attemptId)) {
              dispatch({ type: 'connected' });
            }
          },
          onDisconnected: (disconnection) => {
            scheduleRetry(
              apiForAttempt,
              attemptId,
              errorFromDisconnection(disconnection),
            );
          },
          onFailure: (error) => {
            if (isCurrentAttempt(apiForAttempt, attemptId)) {
              fail(error);
            }
          },
          onSnapshot: (snapshot) => {
            if (isCurrentAttempt(apiForAttempt, attemptId)) {
              reconnectAttempts = 0;
              dispatch({ snapshot, type: 'snapshot_received' });
            }
          },
        });
      } catch (error) {
        scheduleRetry(
          apiForAttempt,
          attemptId,
          normalizeConnectionError(error),
        );
      }
    };

    void beginConnectionAttempt();

    return () => {
      mounted = false;
      if (retryTimer !== null) {
        clearTimeout(retryTimer);
        retryTimer = null;
      }
      disposeActiveAttempt();
    };
  }, [apiFactory]);

  return state;
}
