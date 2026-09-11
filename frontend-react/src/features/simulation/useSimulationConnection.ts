import { useCallback, useEffect, useMemo, useReducer, useRef } from 'react';

import {
  SimulationApi,
  type SimulationApiBoundary,
  type SimulationDisconnection,
} from './SimulationApi';
import { SimulationApiError } from './SimulationApiError';
import {
  describeRoomRefusal,
  findLobbyListing,
  lobbyJoinRefusal,
  type LobbyJoinRefusal,
  type RoomRefusal,
} from './lobbyDirectorySelectors';
import { RECONNECT_BACKOFF_MILLISECONDS } from './simulationConstants';
import type {
  SessionCommand,
  SessionCommandKind,
  SessionEntitySnapshot,
  SessionMatchSection,
  SessionSnapshotMessage,
  SessionTerrain,
  SessionWelcomeMessage,
  SimulationConfiguration,
} from './simulationProtocolTypes';
import { findOwnEntityId } from './sessionSelectors';

export type SimulationConnectionStatus =
  | 'idle'
  | 'loading_configuration'
  | 'connecting'
  | 'awaiting_match'
  | 'connected'
  | 'retrying'
  | 'refused'
  | 'failed';

/** What the welcome told this session about itself. Fixed for the life of one connection. */
export interface SimulationSessionIdentity {
  readonly acceptedCommandKinds: readonly SessionCommandKind[];
  readonly controllerId: number;
  readonly displayName: string;
  readonly firstEntityId: number;
  /** The room this session was admitted into: the number the directory lists it under. */
  readonly lobbyId: number;
  readonly map: string;
  readonly mode: string;
  /** The NPC kinds a `seat_npc` may name, in the server's registry order: the whole bot menu. */
  readonly npcControllerKinds: readonly string[];
  /** The most seats the room's map can seat, which is what bounds a seat-count control. */
  readonly seatCountMaximum: number;
  readonly terrain: SessionTerrain;
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
  | { readonly type: 'refused'; readonly error: SimulationApiError }
  | { readonly type: 'failed'; readonly error: SimulationApiError }
  | { readonly type: 'left' };

export type SimulationApiFactory = () => SimulationApiBoundary;

/** The stable close reason of `1013 lobby_full`, the one refusal the tick makes after admission. */
const LOBBY_FULL_CLOSE_REASON = 'lobby_full';

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
    status: 'idle',
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
          lobbyId: action.welcome.data.lobby_id,
          map: action.welcome.data.map,
          mode: action.welcome.data.mode,
          npcControllerKinds: action.welcome.data.npc_controller_kinds,
          seatCountMaximum: action.welcome.data.seat_count_maximum,
          terrain: action.welcome.data.terrain,
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
    case 'refused':
      return Object.freeze({
        ...state,
        configuration: null,
        entities: NO_ENTITIES,
        error: action.error,
        match: null,
        ownEntityId: null,
        session: null,
        snapshot: null,
        status: 'refused',
      });
    case 'failed':
      return Object.freeze({ ...state, error: action.error, status: 'failed' });
    case 'left':
      return initialSimulationConnectionState;
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

function roomRefusedError(
  refusal: RoomRefusal,
  lobbyId: number,
  cause: SimulationApiError | null,
): SimulationApiError {
  return new SimulationApiError(
    'SIMULATION.ROOM_REFUSED',
    describeRoomRefusal(refusal, lobbyId),
    {
      cause: cause ?? undefined,
      context: { lobby_id: lobbyId, refusal },
      retryable: false,
    },
  );
}

/**
 * @canonical simulation_connection -- owns config, join, retry, refusal, and cleanup policy.
 *
 * `lobbyId` is the room to be in, or `null` to be in none. Changing it is a leave and a join: the
 * old socket is disposed and a fresh attempt is made against the new room, and nothing carries
 * over, because a socket is bound to one room for its life (`docs/protocol/v3.md` § "The lobby
 * directory").
 *
 * **A refusal is not a failure and is never retried.** The server answers a join it will not admit
 * with `404`, `409`, or `503`, and the tick answers the last-seat race with `1013 lobby_full`; the
 * player's next move after any of them is to choose another room, which the backoff would only
 * delay. A browser sees the close reason but never the HTTP response a declined upgrade was refused
 * with, so a socket that closed before it opened is explained by reading the directory once: the
 * listing says full, unavailable, or missing, and if it says none of those -- or cannot be read --
 * the close was transport and keeps the existing backoff.
 */
export function useSimulationConnection(
  lobbyId: number | null,
  apiFactory: SimulationApiFactory = createDefaultSimulationApi,
): SimulationConnection {
  const [state, dispatch] = useReducer(
    simulationConnectionReducer,
    initialSimulationConnectionState,
  );
  const sendingApi = useRef<SimulationApiBoundary | null>(null);

  useEffect(() => {
    if (lobbyId === null) {
      dispatch({ type: 'left' });
      return undefined;
    }
    const roomId = lobbyId;

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

    const refuse = (error: SimulationApiError): void => {
      if (!mounted || terminal) {
        return;
      }
      terminal = true;
      disposeActiveAttempt();
      dispatch({ error, type: 'refused' });
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

    // The socket closed before it ever opened, which is what a declined upgrade looks like from a
    // browser. One read of the directory says which refusal it was, if it was one at all.
    const explainCloseBeforeOpen = async (
      apiForAttempt: SimulationApiBoundary,
      attemptId: number,
      signal: AbortSignal,
      error: SimulationApiError,
    ): Promise<void> => {
      let refusal: LobbyJoinRefusal | null = null;
      try {
        const listings = await apiForAttempt.fetchLobbies(signal);
        refusal = lobbyJoinRefusal(findLobbyListing(listings, roomId));
      } catch {
        // The directory is unreachable too: the server, not the room, is what is not answering.
      }
      if (!isCurrentAttempt(apiForAttempt, attemptId)) {
        return;
      }
      if (refusal !== null) {
        refuse(roomRefusedError(refusal, roomId, error));
        return;
      }
      scheduleRetry(apiForAttempt, attemptId, error);
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
        apiForAttempt.openSession(configuration, roomId, {
          onConnected: () => {
            if (isCurrentAttempt(apiForAttempt, attemptId)) {
              sendingApi.current = apiForAttempt;
              dispatch({ type: 'socket_opened' });
            }
          },
          onDisconnected: (disconnection) => {
            if (!isCurrentAttempt(apiForAttempt, attemptId)) {
              return;
            }
            if (disconnection.reason === LOBBY_FULL_CLOSE_REASON) {
              refuse(roomRefusedError('lobby_full', roomId, null));
              return;
            }
            const error = errorFromDisconnection(disconnection);
            if (!disconnection.opened && error.retryable) {
              void explainCloseBeforeOpen(
                apiForAttempt,
                attemptId,
                abortControllerForAttempt.signal,
                error,
              );
              return;
            }
            scheduleRetry(apiForAttempt, attemptId, error);
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
  }, [apiFactory, lobbyId]);

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
