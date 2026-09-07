import { useCallback, useEffect, useMemo, useReducer, useRef } from 'react';

import {
  SimulationApi,
  type SimulationApiBoundary,
  type SimulationDisconnection,
} from './SimulationApi';
import { SimulationApiError } from './SimulationApiError';
import { RECONNECT_BACKOFF_MILLISECONDS } from './simulationConstants';
import type {
  SessionCommand,
  SessionCommandKind,
  SessionEntitySnapshot,
  SessionMatchSection,
  SessionSnapshotMessage,
  SessionWelcomeMessage,
  SimulationConfiguration,
} from './simulationProtocolTypes';
import { findOwnEntityId } from './sessionSelectors';

export type SimulationConnectionStatus =
  | 'loading_configuration'
  | 'connecting'
  | 'awaiting_match'
  | 'connected'
  | 'retrying'
  | 'failed';

/** What the welcome told this session about itself. Fixed for the life of one connection. */
export interface SimulationSessionIdentity {
  readonly acceptedCommandKinds: readonly SessionCommandKind[];
  readonly controllerId: number;
  readonly displayName: string;
  readonly firstEntityId: number;
  readonly map: string;
  readonly mode: string;
}

export interface SimulationConnectionState {
  readonly configuration: SimulationConfiguration | null;
  readonly entities: readonly SessionEntitySnapshot[];
  readonly error: SimulationApiError | null;
  readonly match: SessionMatchSection | null;
  readonly ownEntityId: number | null;
  readonly reconnectAttempt: number;
  readonly session: SimulationSessionIdentity | null;
  readonly snapshot: SessionSnapshotMessage | null;
  readonly status: SimulationConnectionStatus;
}

export type SimulationCommandSender = (command: SessionCommand) => boolean;

export interface SimulationConnection extends SimulationConnectionState {
  readonly sendCommand: SimulationCommandSender;
}

type SimulationConnectionAction =
  | { readonly type: 'attempt_started' }
  | {
      readonly type: 'configuration_loaded';
      readonly configuration: SimulationConfiguration;
    }
  | { readonly type: 'socket_opened' }
  | {
      readonly type: 'welcome_received';
      readonly welcome: SessionWelcomeMessage;
    }
  | {
      readonly type: 'snapshot_received';
      readonly snapshot: SessionSnapshotMessage;
    }
  | {
      readonly type: 'retry_scheduled';
      readonly attempt: number;
      readonly error: SimulationApiError;
    }
  | { readonly type: 'failed'; readonly error: SimulationApiError };

export type SimulationApiFactory = () => SimulationApiBoundary;

const NO_ENTITIES: readonly SessionEntitySnapshot[] = Object.freeze([]);

export const initialSimulationConnectionState: SimulationConnectionState =
  Object.freeze({
    configuration: null,
    entities: NO_ENTITIES,
    error: null,
    match: null,
    ownEntityId: null,
    reconnectAttempt: 0,
    session: null,
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
        entities: NO_ENTITIES,
        error: null,
        match: null,
        ownEntityId: null,
        session: null,
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
    case 'socket_opened':
      // An open socket carrying no frames is a joiner the mode has deferred to the next lobby, not
      // a stalled connection: the spawn policy defers a joiner while a match runs, and the welcome
      // cannot be written before the session owns a body.
      return Object.freeze({ ...state, error: null, status: 'awaiting_match' });
    case 'welcome_received':
      return Object.freeze({
        ...state,
        error: null,
        session: Object.freeze({
          acceptedCommandKinds: action.welcome.data.accepted_command_kinds,
          controllerId: action.welcome.data.controller_id,
          displayName: action.welcome.data.display_name,
          firstEntityId: action.welcome.data.entity_id,
          map: action.welcome.data.map,
          mode: action.welcome.data.mode,
        }),
        status: 'connected',
      });
    case 'snapshot_received':
      return Object.freeze({
        ...state,
        entities: action.snapshot.data.entities,
        error: null,
        match: action.snapshot.data.match,
        ownEntityId: findOwnEntityId(
          action.snapshot.data.entities,
          state.session?.controllerId ?? null,
        ),
        reconnectAttempt: 0,
        snapshot: action.snapshot,
        status: 'connected',
      });
    case 'retry_scheduled':
      return Object.freeze({
        ...state,
        configuration: null,
        entities: NO_ENTITIES,
        error: action.error,
        match: null,
        ownEntityId: null,
        reconnectAttempt: action.attempt,
        session: null,
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
      ? 'Match session WebSocket closed.'
      : `Match session WebSocket closed: ${disconnection.reason}`,
    {
      context: {
        close_code: disconnection.code,
        was_clean: disconnection.wasClean,
      },
      retryable: disconnection.retryable,
    },
  );
}

/** @canonical simulation_connection -- owns config, join, retry, and cleanup policy. */
export function useSimulationConnection(
  apiFactory: SimulationApiFactory = createDefaultSimulationApi,
): SimulationConnection {
  const [state, dispatch] = useReducer(
    simulationConnectionReducer,
    initialSimulationConnectionState,
  );
  const sendingApi = useRef<SimulationApiBoundary | null>(null);

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
      if (sendingApi.current === activeApi) {
        sendingApi.current = null;
      }
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

        // A reconnect is a new join by contract: new request id, new controller id, new entity id,
        // and message_sequence restarting at one. Nothing is resumed.
        apiForAttempt.openSession(configuration, {
          onConnected: () => {
            if (isCurrentAttempt(apiForAttempt, attemptId)) {
              sendingApi.current = apiForAttempt;
              dispatch({ type: 'socket_opened' });
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
          onWelcome: (welcome) => {
            if (isCurrentAttempt(apiForAttempt, attemptId)) {
              dispatch({ type: 'welcome_received', welcome });
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

  /**
   * Stable for the life of the hook and a no-op unless a session is open and welcomed: the boundary
   * refuses a command whose kind this match does not accept, so an input handler can call it every
   * time it wants to without knowing the connection state.
   */
  const sendCommand = useCallback<SimulationCommandSender>(
    (command) => sendingApi.current?.sendCommand(command) ?? false,
    [],
  );

  // Not frozen here: `sendCommand` reads a ref, and handing it to a function during render is
  // exactly what the React lint forbids. Every value inside `state` is already deeply frozen.
  return useMemo(() => ({ ...state, sendCommand }), [sendCommand, state]);
}
