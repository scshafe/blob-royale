import { afterEach, describe, expect, it, vi } from 'vitest';

import {
  SimulationApi,
  deriveSimulationEndpoints,
  roomSessionWebSocketUrl,
  type SimulationSessionCallbacks,
  type SimulationWebSocket,
} from './SimulationApi';
import {
  configurationResponseExample,
  errorResponseExample,
} from './fixtures/protocolV1Examples';
import {
  lobbyDirectoryMessageExample,
  sessionErrorResponseExample,
} from './fixtures/protocolV3Examples';
import {
  firstEntity,
  raceSnapshotDocument,
  snapshotDocument,
  welcomeDocument,
} from './fixtures/sessionFrames';
import { namedRaceTerrain, raceTerrain } from './fixtures/terrainFrames';
import {
  STUN_INPUT_GENERATION,
  THRUST_GENERATION_COMMAND_CASES,
} from './fixtures/stunInputFrames';
import {
  tuningCommand,
  tuningSnapshotDocument,
  TUNING_RESULT_STATUSES,
} from './fixtures/tuningFrames';
import {
  CONFIGURATION_FETCH_TIMEOUT_MILLISECONDS,
  SESSION_FRAME_MAX_BYTES,
  WEBSOCKET_CONNECT_TIMEOUT_MILLISECONDS,
} from './simulationConstants';

class FakeSimulationWebSocket implements SimulationWebSocket {
  protocol = 'blob-royale.session.v3';
  readyState = 0;
  onclose: ((event: CloseEvent) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;
  onmessage: ((event: MessageEvent<unknown>) => void) | null = null;
  onopen: ((event: Event) => void) | null = null;
  readonly sentMessages: string[] = [];
  readonly close = vi.fn((code?: number, reason?: string) => {
    void code;
    void reason;
    this.readyState = 3;
  });
  readonly send = vi.fn((data: string) => {
    this.sentMessages.push(data);
  });

  open(): void {
    this.readyState = 1;
    this.onopen?.(new Event('open'));
  }

  receive(data: unknown): void {
    this.onmessage?.(new MessageEvent('message', { data }));
  }

  serverClose(code: number, reason: string, wasClean: boolean): void {
    this.readyState = 3;
    this.onclose?.(new CloseEvent('close', { code, reason, wasClean }));
  }
}

const secureLocation = Object.freeze({
  host: 'game.example.test:8443',
  origin: 'https://game.example.test:8443',
  protocol: 'https:',
});

function jsonResponse(
  document: unknown,
  status = 200,
  requestId?: string,
): Response {
  const headers = new Headers({
    'Content-Type': 'application/json; charset=utf-8',
  });
  if (requestId !== undefined) {
    headers.set('X-Request-ID', requestId);
  }
  return new Response(JSON.stringify(document), {
    headers,
    status,
  });
}

function configurationJsonResponse(
  document: typeof configurationResponseExample,
): Response {
  return jsonResponse(document, 200, document.meta.request_id);
}

function createFetchMock(response: Response) {
  return vi.fn(
    (input: RequestInfo | URL, init?: RequestInit): Promise<Response> => {
      void input;
      void init;
      return Promise.resolve(response);
    },
  );
}

function createCallbacks(): SimulationSessionCallbacks {
  return {
    onConnected: vi.fn(),
    onDisconnected: vi.fn(),
    onFailure: vi.fn(),
    onSnapshot: vi.fn(),
    onWelcome: vi.fn(),
    onMovementTuningState: vi.fn(),
  };
}

afterEach(() => {
  vi.useRealTimers();
});

describe('deriveSimulationEndpoints', () => {
  it('derives the exact HTTP and WebSocket paths on one HTTPS authority', () => {
    expect(deriveSimulationEndpoints(secureLocation)).toEqual({
      configurationUrl: 'https://game.example.test:8443/api/v1/config',
      lobbyDirectoryUrl: 'https://game.example.test:8443/api/v3/lobbies',
      webSocketOrigin: 'wss://game.example.test:8443',
    });
  });

  it('maps an HTTP page only to same-authority HTTP and WS endpoints', () => {
    expect(
      deriveSimulationEndpoints({
        host: '127.0.0.1:5173',
        origin: 'http://127.0.0.1:5173',
        protocol: 'http:',
      }),
    ).toEqual({
      configurationUrl: 'http://127.0.0.1:5173/api/v1/config',
      lobbyDirectoryUrl: 'http://127.0.0.1:5173/api/v3/lobbies',
      webSocketOrigin: 'ws://127.0.0.1:5173',
    });
  });

  it('rejects non-HTTP origins and inconsistent authorities', () => {
    expect(() =>
      deriveSimulationEndpoints({
        host: '',
        origin: 'file://',
        protocol: 'file:',
      }),
    ).toThrow(/HTTP or HTTPS/);

    expect(() =>
      deriveSimulationEndpoints({
        host: 'evil.example',
        origin: 'https://game.example.test',
        protocol: 'https:',
      }),
    ).toThrow(/inconsistent/);
  });
});

describe('roomSessionWebSocketUrl', () => {
  it('builds the parametric room target only for ids the grammar admits', () => {
    const endpoints = deriveSimulationEndpoints(secureLocation);
    expect(roomSessionWebSocketUrl(endpoints, 1)).toBe(
      'wss://game.example.test:8443/api/v3/lobbies/1/session',
    );
    expect(roomSessionWebSocketUrl(endpoints, 999)).toBe(
      'wss://game.example.test:8443/api/v3/lobbies/999/session',
    );
    for (const lobbyId of [0, -1, 1.5, 1_000, Number.NaN]) {
      expect(() => roomSessionWebSocketUrl(endpoints, lobbyId)).toThrow(
        /lobby id/,
      );
    }
  });
});

describe('SimulationApi lobby directory', () => {
  function directoryResponse(requestId?: string): Response {
    return jsonResponse(
      structuredClone(lobbyDirectoryMessageExample),
      200,
      requestId ?? lobbyDirectoryMessageExample.meta.request_id,
    );
  }

  function createDirectoryApi(response: Response) {
    const fetchMock = createFetchMock(response);
    const api = new SimulationApi({
      fetchImplementation: fetchMock,
      location: secureLocation,
      webSocketFactory: () => new FakeSimulationWebSocket(),
    });
    return { api, fetchMock };
  }

  it('reads the exact same-origin directory and returns its frozen listings', async () => {
    const { api, fetchMock } = createDirectoryApi(directoryResponse());

    const listings = await api.fetchLobbies(new AbortController().signal);

    expect(fetchMock).toHaveBeenCalledWith(
      'https://game.example.test:8443/api/v3/lobbies',
      expect.objectContaining({
        cache: 'no-store',
        credentials: 'omit',
        method: 'GET',
        redirect: 'error',
      }),
    );
    expect(listings.map((listing) => listing.lobby_id)).toEqual([1, 2]);
    expect(Object.isFrozen(listings)).toBe(true);
    expect(Object.isFrozen(listings[0])).toBe(true);
  });

  it('reads the directory again on every call rather than caching it', async () => {
    const { api, fetchMock } = createDirectoryApi(directoryResponse());
    fetchMock.mockImplementation(() => Promise.resolve(directoryResponse()));

    await api.fetchLobbies(new AbortController().signal);
    await api.fetchLobbies(new AbortController().signal);

    expect(fetchMock).toHaveBeenCalledTimes(2);
  });

  it('rejects a directory whose X-Request-ID does not echo the envelope', async () => {
    const { api } = createDirectoryApi(directoryResponse('some-other-request'));

    await expect(
      api.fetchLobbies(new AbortController().signal),
    ).rejects.toMatchObject({
      code: 'SIMULATION.LOBBY_DIRECTORY_RESPONSE_INVALID',
    });
  });

  it('reports a v3 failure envelope with its registered retryability', async () => {
    const { api } = createDirectoryApi(
      jsonResponse(
        structuredClone(sessionErrorResponseExample),
        400,
        sessionErrorResponseExample.meta.request_id,
      ),
    );

    await expect(
      api.fetchLobbies(new AbortController().signal),
    ).rejects.toMatchObject({
      code: 'SIMULATION.LOBBY_DIRECTORY_REQUEST_FAILED',
      context: {
        http_status: 400,
        protocol_error_code: 'PROTOCOL.INVALID_FORWARDED_CLIENT',
      },
      retryable: false,
    });
  });

  it('rejects a failure envelope whose status disagrees with its code', async () => {
    const { api } = createDirectoryApi(
      jsonResponse(
        structuredClone(sessionErrorResponseExample),
        503,
        sessionErrorResponseExample.meta.request_id,
      ),
    );

    await expect(
      api.fetchLobbies(new AbortController().signal),
    ).rejects.toMatchObject({ code: 'SIMULATION.HTTP_ERROR_RESPONSE_INVALID' });
  });

  it('refuses to read after disposal', async () => {
    const { api } = createDirectoryApi(directoryResponse());
    api.dispose();

    await expect(
      api.fetchLobbies(new AbortController().signal),
    ).rejects.toMatchObject({ code: 'SIMULATION.SOCKET_DISPOSED' });
  });
});

describe('SimulationApi configuration', () => {
  it('fetches the exact same-origin config endpoint once and freezes the result', async () => {
    const fetchMock = createFetchMock(
      configurationJsonResponse(structuredClone(configurationResponseExample)),
    );
    const api = new SimulationApi({
      fetchImplementation: fetchMock,
      location: secureLocation,
      webSocketFactory: () => new FakeSimulationWebSocket(),
    });
    const abortController = new AbortController();

    const firstConfiguration = await api.loadConfiguration(
      abortController.signal,
    );
    const secondConfiguration = await api.loadConfiguration(
      abortController.signal,
    );

    expect(fetchMock).toHaveBeenCalledTimes(1);
    expect(fetchMock).toHaveBeenCalledWith(
      'https://game.example.test:8443/api/v1/config',
      expect.objectContaining({
        cache: 'no-store',
        credentials: 'omit',
        method: 'GET',
        redirect: 'error',
      }),
    );
    expect(secondConfiguration).toBe(firstConfiguration);
    expect(Object.isFrozen(firstConfiguration.presentation)).toBe(true);
  });

  it('rejects a successful config response with no X-Request-ID header', async () => {
    const configurationResponse = structuredClone(configurationResponseExample);
    const api = new SimulationApi({
      fetchImplementation: createFetchMock(jsonResponse(configurationResponse)),
      location: secureLocation,
      webSocketFactory: () => new FakeSimulationWebSocket(),
    });

    await expect(
      api.loadConfiguration(new AbortController().signal),
    ).rejects.toMatchObject({
      code: 'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
      context: {
        envelope_request_id: configurationResponse.meta.request_id,
        response_request_id: null,
      },
    });
  });

  it('rejects a successful config response with a mismatched X-Request-ID header', async () => {
    const configurationResponse = structuredClone(configurationResponseExample);
    const api = new SimulationApi({
      fetchImplementation: createFetchMock(
        jsonResponse(configurationResponse, 200, 'different-request-id'),
      ),
      location: secureLocation,
      webSocketFactory: () => new FakeSimulationWebSocket(),
    });

    await expect(
      api.loadConfiguration(new AbortController().signal),
    ).rejects.toMatchObject({
      code: 'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
      context: {
        envelope_request_id: configurationResponse.meta.request_id,
        response_request_id: 'different-request-id',
      },
    });
  });

  it('aborts and reports a retryable timeout when config fetch never settles', async () => {
    vi.useFakeTimers();
    const fetchMock = vi.fn(
      () =>
        new Promise<Response>(() => {
          // Deliberately pending so the bounded client timeout is observable.
        }),
    );
    const api = new SimulationApi({
      fetchImplementation: fetchMock,
      location: secureLocation,
      webSocketFactory: () => new FakeSimulationWebSocket(),
    });

    const configurationPromise = api.loadConfiguration(
      new AbortController().signal,
    );
    const rejection = expect(configurationPromise).rejects.toMatchObject({
      code: 'SIMULATION.CONFIGURATION_REQUEST_TIMED_OUT',
      retryable: true,
    });
    await vi.advanceTimersByTimeAsync(CONFIGURATION_FETCH_TIMEOUT_MILLISECONDS);

    await rejection;
    expect(fetchMock).toHaveBeenCalledTimes(1);
  });

  it('rejects a declared body larger than the HTTP response limit', async () => {
    const response = new Response('', {
      headers: {
        'Content-Length': '65537',
        'Content-Type': 'application/json',
      },
    });
    const api = new SimulationApi({
      fetchImplementation: createFetchMock(response),
      location: secureLocation,
      webSocketFactory: () => new FakeSimulationWebSocket(),
    });

    await expect(
      api.loadConfiguration(new AbortController().signal),
    ).rejects.toMatchObject({
      code: 'SIMULATION.CONFIGURATION_RESPONSE_TOO_LARGE',
    });
  });

  it('enforces the body limit when Content-Length is absent', async () => {
    const response = new Response('x'.repeat(65_537), {
      headers: { 'Content-Type': 'application/json' },
    });
    const api = new SimulationApi({
      fetchImplementation: createFetchMock(response),
      location: secureLocation,
      webSocketFactory: () => new FakeSimulationWebSocket(),
    });

    await expect(
      api.loadConfiguration(new AbortController().signal),
    ).rejects.toMatchObject({
      code: 'SIMULATION.CONFIGURATION_RESPONSE_TOO_LARGE',
    });
  });

  it('rejects malformed success data before returning configuration', async () => {
    const malformedResponse = structuredClone(configurationResponseExample);
    Object.assign(malformedResponse.data, { private_thread_count: 8 });
    const api = new SimulationApi({
      fetchImplementation: createFetchMock(
        configurationJsonResponse(malformedResponse),
      ),
      location: secureLocation,
      webSocketFactory: () => new FakeSimulationWebSocket(),
    });

    await expect(
      api.loadConfiguration(new AbortController().signal),
    ).rejects.toMatchObject({
      code: 'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
    });
  });

  it('accepts only an HTTP error with its registered status, retryability, and request ID', async () => {
    const errorResponse = structuredClone(errorResponseExample);
    const api = new SimulationApi({
      fetchImplementation: createFetchMock(
        jsonResponse(errorResponse, 405, errorResponse.meta.request_id),
      ),
      location: secureLocation,
      webSocketFactory: () => new FakeSimulationWebSocket(),
    });

    await expect(
      api.loadConfiguration(new AbortController().signal),
    ).rejects.toMatchObject({
      code: 'SIMULATION.CONFIGURATION_REQUEST_FAILED',
      context: {
        http_status: 405,
        protocol_error_code: 'PROTOCOL.METHOD_NOT_ALLOWED',
      },
      retryable: false,
    });
  });

  it('rejects an HTTP error when X-Request-ID differs from the envelope', async () => {
    const errorResponse = structuredClone(errorResponseExample);
    const api = new SimulationApi({
      fetchImplementation: createFetchMock(
        jsonResponse(errorResponse, 405, 'different-request-id'),
      ),
      location: secureLocation,
      webSocketFactory: () => new FakeSimulationWebSocket(),
    });

    await expect(
      api.loadConfiguration(new AbortController().signal),
    ).rejects.toMatchObject({
      code: 'SIMULATION.HTTP_ERROR_RESPONSE_INVALID',
    });
  });

  it('rejects an HTTP error whose status disagrees with its stable protocol code', async () => {
    const errorResponse = structuredClone(errorResponseExample);
    const api = new SimulationApi({
      fetchImplementation: createFetchMock(
        jsonResponse(errorResponse, 503, errorResponse.meta.request_id),
      ),
      location: secureLocation,
      webSocketFactory: () => new FakeSimulationWebSocket(),
    });

    await expect(
      api.loadConfiguration(new AbortController().signal),
    ).rejects.toMatchObject({
      code: 'SIMULATION.HTTP_ERROR_RESPONSE_INVALID',
    });
  });

  it('preserves registered retryability for a valid service-not-ready response', async () => {
    const errorResponse = structuredClone(errorResponseExample);
    errorResponse.error.code = 'SERVICE.NOT_READY';
    errorResponse.error.retryable = true;
    const api = new SimulationApi({
      fetchImplementation: createFetchMock(
        jsonResponse(errorResponse, 503, errorResponse.meta.request_id),
      ),
      location: secureLocation,
      webSocketFactory: () => new FakeSimulationWebSocket(),
    });

    await expect(
      api.loadConfiguration(new AbortController().signal),
    ).rejects.toMatchObject({
      code: 'SIMULATION.CONFIGURATION_REQUEST_FAILED',
      retryable: true,
    });
  });
});

describe('SimulationApi session lifecycle', () => {
  async function createJoinedApi(allowedLobbyIds: readonly number[] = [1]) {
    const sockets: FakeSimulationWebSocket[] = [];
    const api = new SimulationApi({
      fetchImplementation: createFetchMock(
        configurationJsonResponse(
          structuredClone(configurationResponseExample),
        ),
      ),
      location: secureLocation,
      webSocketFactory: (url, subprotocol) => {
        expect(
          allowedLobbyIds.map(
            (lobbyId) =>
              `wss://game.example.test:8443/api/v3/lobbies/${lobbyId}/session`,
          ),
        ).toContain(url);
        expect(subprotocol).toBe('blob-royale.session.v3');
        const socket = new FakeSimulationWebSocket();
        sockets.push(socket);
        return socket;
      },
    });
    const configuration = await api.loadConfiguration(
      new AbortController().signal,
    );
    return { api, configuration, sockets };
  }

  function requireSocket(sockets: FakeSimulationWebSocket[]) {
    const socket = sockets[0];
    if (socket === undefined) {
      throw new Error('TEST.SOCKET_NOT_CREATED');
    }
    return socket;
  }

  async function openTuningApi() {
    const setup = await createJoinedApi();
    const callbacks = createCallbacks();
    setup.api.openSession(setup.configuration, 1, callbacks);
    const socket = requireSocket(setup.sockets);
    socket.open();
    socket.receive(JSON.stringify(welcomeDocument()));
    return { ...setup, callbacks, socket };
  }

  it.each(TUNING_RESULT_STATUSES)(
    'resolves only the matching %s outcome',
    async (status) => {
      const { api, callbacks, socket } = await openTuningApi();
      expect(api.sendCommand(tuningCommand())).toBe(true);
      expect(callbacks.onMovementTuningState).toHaveBeenLastCalledWith({
        status: 'pending',
        request: tuningCommand().payload,
      });
      socket.receive(JSON.stringify(tuningSnapshotDocument(status)));
      expect(
        vi.mocked(callbacks.onMovementTuningState).mock.lastCall?.[0],
      ).toMatchObject({
        status: 'resolved',
        result: { status, tuning_request_id: 1 },
      });
      expect(callbacks.onSnapshot).toHaveBeenCalledTimes(1);
      expect(callbacks.onFailure).not.toHaveBeenCalled();
      api.dispose();
    },
  );

  it('does not infer success from a shared revision or a successful send', async () => {
    const { api, callbacks, socket } = await openTuningApi();
    expect(api.sendCommand(tuningCommand())).toBe(true);
    const shared = tuningSnapshotDocument();
    Reflect.set(shared.data, 'tuning_result', null);
    socket.receive(JSON.stringify(shared));
    expect(callbacks.onMovementTuningState).toHaveBeenCalledTimes(1);
    expect(callbacks.onMovementTuningState).toHaveBeenLastCalledWith(
      expect.objectContaining({ status: 'pending' }),
    );
    socket.receive(JSON.stringify(tuningSnapshotDocument('applied', 1, 3)));
    expect(callbacks.onMovementTuningState).toHaveBeenLastCalledWith(
      expect.objectContaining({ status: 'resolved' }),
    );
    api.dispose();
  });

  it('preserves an older applied result when the covering snapshot has a later shared revision', async () => {
    const { api, callbacks, socket } = await openTuningApi();
    api.sendCommand(tuningCommand());
    const document = tuningSnapshotDocument();
    document.data.match.movement.revision = 2;
    document.data.match.movement.current.acceleration_world_units_per_second_squared = 900;
    socket.receive(JSON.stringify(document));
    expect(
      vi.mocked(callbacks.onMovementTuningState).mock.lastCall?.[0],
    ).toMatchObject({
      status: 'resolved',
      result: { revision: 1 },
    });
    expect(callbacks.onSnapshot).toHaveBeenCalledTimes(1);
    api.dispose();
  });

  it.each([
    'foreign',
    'future_tick',
    'future_revision',
    'wrong_applied_revision',
    'unknown',
    'extra',
  ])('fails before snapshot acceptance for a %s result', async (mutation) => {
    const { api, callbacks, socket } = await openTuningApi();
    api.sendCommand(tuningCommand());
    const document = tuningSnapshotDocument();
    // Mutate a new, untrusted input specimen, never the readonly validated result contract.
    const result = { ...document.data.tuning_result };
    if (mutation === 'foreign') result.tuning_request_id = 2;
    if (mutation === 'future_tick')
      result.decision_tick = document.data.tick_sequence + 1;
    if (mutation === 'future_revision') result.revision = 2;
    if (mutation === 'wrong_applied_revision') {
      result.revision = 2;
      document.data.match.movement.revision = 2;
    }
    if (mutation === 'unknown') Reflect.set(result, 'status', 'unknown');
    if (mutation === 'extra') Reflect.set(result, 'controller_id', 3);
    document.data.tuning_result = result;
    socket.receive(JSON.stringify(document));
    expect(callbacks.onSnapshot).not.toHaveBeenCalled();
    expect(callbacks.onFailure).toHaveBeenCalledTimes(1);
    expect(callbacks.onMovementTuningState).toHaveBeenLastCalledWith(
      expect.objectContaining({ status: 'unknown' }),
    );
    api.dispose();
  });

  it('refuses unsolicited and duplicate results instead of resolving a different exchange', async () => {
    const { api, callbacks, socket } = await openTuningApi();
    api.sendCommand(tuningCommand());
    socket.receive(JSON.stringify(tuningSnapshotDocument()));
    api.sendCommand(tuningCommand(2, 1));
    socket.receive(JSON.stringify(tuningSnapshotDocument('applied', 1, 3)));
    expect(callbacks.onSnapshot).toHaveBeenCalledTimes(1);
    expect(callbacks.onFailure).toHaveBeenCalledTimes(1);
    expect(callbacks.onMovementTuningState).toHaveBeenLastCalledWith({
      status: 'unknown',
      request: tuningCommand(2, 1).payload,
    });
    api.dispose();
    const other = await openTuningApi();
    other.socket.receive(JSON.stringify(tuningSnapshotDocument()));
    expect(other.callbacks.onSnapshot).not.toHaveBeenCalled();
    expect(other.callbacks.onFailure).toHaveBeenCalledTimes(1);
    other.api.dispose();
  });

  it('locally refuses overlapping or reused IDs without inventing a server rejection', async () => {
    const { api, callbacks, socket } = await openTuningApi();
    expect(api.sendCommand(tuningCommand())).toBe(true);
    expect(api.sendCommand(tuningCommand(2))).toBe(false);
    expect(
      api.sendCommand({ kind: 'set_thrust', payload: { x: 1, y: 0 } }),
    ).toBe(true);
    socket.receive(JSON.stringify(tuningSnapshotDocument()));
    expect(api.sendCommand(tuningCommand())).toBe(false);
    expect(callbacks.onMovementTuningState).toHaveBeenCalledTimes(2);
    expect(api.sendCommand(tuningCommand(5, 1))).toBe(true);
    api.dispose();
  });

  it('reserves pending before synchronous delivery and frees it before result callbacks send again', async () => {
    const { api, callbacks, socket } = await openTuningApi();
    vi.mocked(callbacks.onMovementTuningState).mockImplementation((state) => {
      if (state.status === 'resolved' && state.result.tuning_request_id === 1) {
        expect(api.sendCommand(tuningCommand(2, 1))).toBe(true);
      }
    });
    socket.send.mockImplementationOnce(() => {
      socket.receive(JSON.stringify(tuningSnapshotDocument()));
    });
    expect(api.sendCommand(tuningCommand())).toBe(true);
    expect(socket.send).toHaveBeenCalledTimes(2);
    expect(callbacks.onMovementTuningState).toHaveBeenLastCalledWith({
      status: 'pending',
      request: tuningCommand(2, 1).payload,
    });
    const second = tuningSnapshotDocument('applied', 2, 3);
    second.data.match.movement.revision = 2;
    second.data.tuning_result = { ...second.data.tuning_result, revision: 2 };
    socket.receive(JSON.stringify(second));
    expect(
      vi.mocked(callbacks.onMovementTuningState).mock.lastCall?.[0],
    ).toMatchObject({
      status: 'resolved',
      result: { tuning_request_id: 2 },
    });
    api.dispose();
  });

  it('marks an uncertain send exception unknown and closes without automatic replay', async () => {
    const { api, callbacks, socket } = await openTuningApi();
    socket.send.mockImplementationOnce(() => {
      throw new Error('TEST.TRANSPORT_SEND_FAILED');
    });
    expect(api.sendCommand(tuningCommand())).toBe(false);
    expect(callbacks.onMovementTuningState).toHaveBeenLastCalledWith({
      status: 'unknown',
      request: tuningCommand().payload,
    });
    expect(callbacks.onDisconnected).toHaveBeenCalledTimes(1);
    expect(socket.close).toHaveBeenCalledWith(4000, 'transport_failure');
    expect(socket.send).toHaveBeenCalledTimes(1);
    api.dispose();
  });

  it('makes transport unavailable before an unknown-state callback tries to send again', async () => {
    const { api, callbacks, socket } = await openTuningApi();
    vi.mocked(callbacks.onMovementTuningState).mockImplementation((state) => {
      if (state.status === 'unknown')
        expect(api.sendCommand(tuningCommand(2))).toBe(false);
    });
    api.sendCommand(tuningCommand());
    socket.receive(JSON.stringify(tuningSnapshotDocument('applied', 99)));
    expect(socket.send).toHaveBeenCalledTimes(1);
    expect(callbacks.onMovementTuningState).toHaveBeenLastCalledWith({
      status: 'unknown',
      request: tuningCommand().payload,
    });
    api.dispose();
  });

  it('publishes snapshots and latest tuning state in order under nested synchronous results', async () => {
    const { api, callbacks, socket } = await openTuningApi();
    vi.mocked(callbacks.onSnapshot).mockImplementation((snapshot) => {
      if (snapshot.data.tuning_result?.tuning_request_id === 1)
        api.sendCommand(tuningCommand(2, 1));
    });
    socket.send.mockImplementation((encoded) => {
      const id = (
        JSON.parse(encoded) as { payload: { tuning_request_id: number } }
      ).payload.tuning_request_id;
      const document = tuningSnapshotDocument('applied', id, id + 1);
      document.data.match.movement.revision = id;
      document.data.tuning_result = {
        ...document.data.tuning_result,
        revision: id,
      };
      socket.receive(JSON.stringify(document));
    });
    api.sendCommand(tuningCommand());
    expect(
      vi
        .mocked(callbacks.onSnapshot)
        .mock.calls.map(([snapshot]) => snapshot.meta.message_sequence),
    ).toEqual([2, 3]);
    expect(
      vi.mocked(callbacks.onMovementTuningState).mock.lastCall?.[0],
    ).toMatchObject({
      status: 'resolved',
      result: { tuning_request_id: 2 },
    });
    api.dispose();
  });

  it('keeps interrupted requests unknown and starts fresh correlation only on a new connection', async () => {
    const { api, callbacks, socket, configuration, sockets } =
      await openTuningApi();
    api.sendCommand(tuningCommand());
    const staleMessage = socket.onmessage;
    socket.serverClose(1006, 'lost', false);
    expect(callbacks.onMovementTuningState).toHaveBeenLastCalledWith({
      status: 'unknown',
      request: tuningCommand().payload,
    });
    const nextCallbacks = createCallbacks();
    api.openSession(configuration, 1, nextCallbacks);
    const nextSocket = sockets[1]!;
    nextSocket.open();
    const nextWelcome = welcomeDocument();
    nextWelcome.data.controller_id = 99;
    nextSocket.receive(JSON.stringify(nextWelcome));
    expect(nextSocket.sentMessages).toEqual([]);
    staleMessage?.(
      new MessageEvent('message', {
        data: JSON.stringify(tuningSnapshotDocument()),
      }),
    );
    expect(nextCallbacks.onSnapshot).not.toHaveBeenCalled();
    expect(api.sendCommand(tuningCommand())).toBe(true);
    api.dispose();
  });

  it('does not expose tuning when the welcome mode mask omits it', async () => {
    const { api, callbacks, socket } = await openTuningApi();
    api.dispose();
    const other = await createJoinedApi();
    other.api.openSession(other.configuration, 1, callbacks);
    const sandbox = requireSocket(other.sockets);
    sandbox.open();
    const welcome = welcomeDocument();
    welcome.data.accepted_command_kinds = ['set_thrust'];
    sandbox.receive(JSON.stringify(welcome));
    expect(other.api.sendCommand(tuningCommand())).toBe(false);
    expect(sandbox.sentMessages).toEqual([]);
    expect(socket.sentMessages).toEqual([]);
    other.api.dispose();
  });

  it('owns one socket and delivers a welcome before validated monotonic snapshots', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);

    expect(() => api.openSession(configuration, 1, callbacks)).toThrow(
      /only one WebSocket/,
    );

    socket.open();
    socket.receive(JSON.stringify(welcomeDocument()));
    socket.receive(JSON.stringify(snapshotDocument(2)));
    const nextSnapshot = snapshotDocument(3);
    nextSnapshot.data.tick_sequence += 1;
    socket.receive(JSON.stringify(nextSnapshot));

    expect(callbacks.onConnected).toHaveBeenCalledTimes(1);
    expect(callbacks.onWelcome).toHaveBeenCalledTimes(1);
    expect(callbacks.onSnapshot).toHaveBeenCalledTimes(2);
    expect(
      Object.isFrozen(
        vi.mocked(callbacks.onSnapshot).mock.calls[0]?.[0].data.entities,
      ),
    ).toBe(true);
    expect(callbacks.onFailure).not.toHaveBeenCalled();
  });

  it('rejects invalid random draw counts before publishing a snapshot', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);
    socket.open();
    socket.receive(JSON.stringify(welcomeDocument()));
    const invalid = snapshotDocument();
    invalid.data.random_draw_counts.hazards = Number.MAX_SAFE_INTEGER + 1;
    socket.receive(JSON.stringify(invalid));
    expect(callbacks.onSnapshot).not.toHaveBeenCalled();
    expect(callbacks.onFailure).toHaveBeenCalledWith(
      expect.objectContaining({
        code: 'SIMULATION.SESSION_FRAME_INVALID',
      }),
    );
    expect(socket.close).toHaveBeenCalledWith(1002, 'protocol_error');
  });

  it('stays open and frameless for a joiner the match has deferred', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);

    socket.open();

    expect(callbacks.onConnected).toHaveBeenCalledTimes(1);
    expect(callbacks.onWelcome).not.toHaveBeenCalled();
    expect(callbacks.onSnapshot).not.toHaveBeenCalled();
    expect(callbacks.onFailure).not.toHaveBeenCalled();
    expect(callbacks.onDisconnected).not.toHaveBeenCalled();
    expect(socket.close).not.toHaveBeenCalled();
  });

  it('retains welcome terrain across snapshots and rejects a changed road before publication', async () => {
    const { api, configuration, sockets } = await createJoinedApi([1, 2]);
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);
    socket.open();
    const welcome = welcomeDocument();
    socket.receive(
      JSON.stringify({
        ...welcome,
        data: {
          ...welcome.data,
          mode: 'race',
          terrain: namedRaceTerrain('alternate_road'),
        },
      }),
    );
    const first = raceSnapshotDocument(2);
    first.data.match.mode_state.value.road = 'alternate_road';
    socket.receive(JSON.stringify(first));
    const invalid = raceSnapshotDocument(3);
    invalid.data.tick_sequence += 1;
    socket.receive(JSON.stringify(invalid));
    expect(callbacks.onSnapshot).toHaveBeenCalledTimes(1);
    expect(callbacks.onFailure).toHaveBeenCalledWith(
      expect.objectContaining({
        code: 'SIMULATION.SESSION_INVARIANT_VIOLATION',
      }),
    );
    expect(socket.close).toHaveBeenCalledWith(1002, 'protocol_error');
    invalid.data.match.mode_state.value.road = 'alternate_road';
    socket.receive(JSON.stringify(invalid));
    expect(callbacks.onSnapshot).toHaveBeenCalledTimes(1);

    const nextCallbacks = createCallbacks();
    api.openSession(configuration, 2, nextCallbacks);
    const nextSocket = sockets[1];
    if (nextSocket === undefined) throw new Error('TEST.SOCKET_NOT_CREATED');
    nextSocket.open();
    nextSocket.receive(
      JSON.stringify({
        ...welcome,
        data: {
          ...welcome.data,
          lobby_id: 2,
          mode: 'race',
          terrain: raceTerrain,
        },
      }),
    );
    nextSocket.receive(JSON.stringify(raceSnapshotDocument(2)));
    expect(nextCallbacks.onSnapshot).toHaveBeenCalledTimes(1);
    expect(nextCallbacks.onFailure).not.toHaveBeenCalled();
  });

  it('refuses a gate wider than the welcome-selected corridor before accepting any snapshot', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);
    socket.open();
    const welcome = welcomeDocument();
    socket.receive(
      JSON.stringify({
        ...welcome,
        data: { ...welcome.data, mode: 'race', terrain: raceTerrain },
      }),
    );
    const snapshot = raceSnapshotDocument();
    snapshot.data.match.mode_state.value.checkpoint_radius = 61;
    socket.receive(JSON.stringify(snapshot));
    expect(callbacks.onSnapshot).not.toHaveBeenCalled();
    expect(callbacks.onFailure).toHaveBeenCalledWith(
      expect.objectContaining({
        code: 'SIMULATION.SESSION_INVARIANT_VIOLATION',
      }),
    );
    expect(socket.close).toHaveBeenCalledWith(1002, 'protocol_error');
  });

  it.each(['width_world_units', 'height_world_units'] as const)(
    'rejects terrain %s disagreement before publishing welcome',
    async (field) => {
      const { api, configuration, sockets } = await createJoinedApi();
      const callbacks = createCallbacks();
      api.openSession(configuration, 1, callbacks);
      const socket = requireSocket(sockets);
      socket.open();
      const welcome = welcomeDocument();
      welcome.data.terrain.bounds[field] += 1;
      socket.receive(JSON.stringify(welcome));
      expect(callbacks.onWelcome).not.toHaveBeenCalled();
      expect(callbacks.onSnapshot).not.toHaveBeenCalled();
      expect(callbacks.onFailure).toHaveBeenCalledWith(
        expect.objectContaining({
          code: 'SIMULATION.SESSION_INVARIANT_VIOLATION',
        }),
      );
      expect(socket.close).toHaveBeenCalledWith(1002, 'protocol_error');
    },
  );

  it('refuses a snapshot that arrives before the welcome', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);
    socket.open();

    socket.receive(JSON.stringify(snapshotDocument(2)));

    expect(callbacks.onSnapshot).not.toHaveBeenCalled();
    expect(callbacks.onFailure).toHaveBeenCalledWith(
      expect.objectContaining({ code: 'SIMULATION.SESSION_FRAME_INVALID' }),
    );
    expect(socket.close).toHaveBeenCalledWith(1002, 'protocol_error');
  });

  it('fails closed with client_kind_unsupported on an unknown component kind', async () => {
    vi.spyOn(console, 'warn').mockImplementation(() => undefined);
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);
    socket.open();
    socket.receive(JSON.stringify(welcomeDocument()));

    const snapshot = snapshotDocument(2);
    Reflect.set(firstEntity(snapshot).components, 'gravity_well', {
      strength: 1,
    });
    socket.receive(JSON.stringify(snapshot));

    expect(callbacks.onSnapshot).not.toHaveBeenCalled();
    expect(callbacks.onFailure).toHaveBeenCalledWith(
      expect.objectContaining({ code: 'SIMULATION.SESSION_KIND_UNSUPPORTED' }),
    );
    expect(socket.close).toHaveBeenCalledWith(1003, 'client_kind_unsupported');
  });

  it('fails closed with client_version_unsupported on a newer protocol minor', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);
    socket.open();

    const welcome = welcomeDocument();
    // One minor ahead of the active 3.0 contract, before ordinary schema validation.
    welcome.meta.protocol_version = '3.1';
    socket.receive(JSON.stringify(welcome));

    expect(callbacks.onWelcome).not.toHaveBeenCalled();
    expect(callbacks.onFailure).toHaveBeenCalledWith(
      expect.objectContaining({
        code: 'SIMULATION.SESSION_VERSION_UNSUPPORTED',
      }),
    );
    expect(socket.close).toHaveBeenCalledWith(
      1003,
      'client_version_unsupported',
    );
  });

  it('rejects an oversized frame before parsing or rendering', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);
    socket.open();

    socket.receive('x'.repeat(SESSION_FRAME_MAX_BYTES + 1));

    expect(callbacks.onWelcome).not.toHaveBeenCalled();
    expect(callbacks.onFailure).toHaveBeenCalledWith(
      expect.objectContaining({
        code: 'SIMULATION.SESSION_FRAME_TOO_LARGE',
      }),
    );
    expect(socket.close).toHaveBeenCalledOnce();
    expect(socket.close).toHaveBeenCalledWith(1009, 'session_frame_too_large');
  });

  it('closes malformed protocol payloads with a protocol-error close', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);
    socket.open();

    socket.receive('{');

    expect(callbacks.onFailure).toHaveBeenCalledWith(
      expect.objectContaining({
        code: 'SIMULATION.SESSION_FRAME_INVALID',
      }),
    );
    expect(socket.close).toHaveBeenCalledWith(1002, 'protocol_error');
  });

  it('sends an accepted command only on an open welcomed session', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);

    const thrust = { kind: 'set_thrust', payload: { x: 1, y: 0 } } as const;
    expect(api.sendCommand(thrust)).toBe(false);

    socket.open();
    expect(api.sendCommand(thrust)).toBe(false);

    socket.receive(JSON.stringify(welcomeDocument()));
    expect(api.sendCommand(thrust)).toBe(true);
    expect(socket.sentMessages).toEqual([
      '{"kind":"set_thrust","payload":{"x":1,"y":0}}',
    ]);

    api.dispose();
    expect(api.sendCommand(thrust)).toBe(false);
    expect(socket.send).toHaveBeenCalledTimes(1);
  });

  it.each(THRUST_GENERATION_COMMAND_CASES)(
    'serializes $name verbatim through the API',
    async ({ command, serialized }) => {
      const { api, configuration, sockets } = await createJoinedApi();
      api.openSession(configuration, 1, createCallbacks());
      const socket = requireSocket(sockets);
      socket.open();
      socket.receive(JSON.stringify(welcomeDocument()));
      expect(api.sendCommand(command)).toBe(true);
      expect(socket.sentMessages).toEqual([serialized]);
    },
  );

  it('refuses a present zero generation rather than serializing it as absent', async () => {
    vi.spyOn(console, 'warn').mockImplementation(() => undefined);
    const { api, configuration, sockets } = await createJoinedApi();
    api.openSession(configuration, 1, createCallbacks());
    const socket = requireSocket(sockets);
    socket.open();
    socket.receive(JSON.stringify(welcomeDocument()));
    const command = THRUST_GENERATION_COMMAND_CASES.find(
      ({ name }) => name === 'present generation zero release',
    )?.command;
    if (command === undefined)
      throw new Error('TEST.STUN_INPUT_COMMAND_MISSING');
    expect(
      api.sendCommand({
        ...command,
        payload: { ...command.payload, input_generation: 0 },
      }),
    ).toBe(false);
    expect(socket.send).not.toHaveBeenCalled();
    expect(
      api.sendCommand({
        ...command,
        payload: {
          ...command.payload,
          input_generation: STUN_INPUT_GENERATION,
        },
      }),
    ).toBe(true);
  });

  it('refuses a command whose payload the closed schema would reject', async () => {
    vi.spyOn(console, 'warn').mockImplementation(() => undefined);
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);
    socket.open();
    socket.receive(JSON.stringify(welcomeDocument()));

    expect(
      api.sendCommand({ kind: 'set_thrust', payload: { x: 4, y: 0 } }),
    ).toBe(false);
    expect(socket.send).not.toHaveBeenCalled();
    expect(socket.close).not.toHaveBeenCalled();
  });

  it('refuses a command kind this match did not accept', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);
    socket.open();
    const welcome = welcomeDocument();
    welcome.data.accepted_command_kinds = [];
    socket.receive(JSON.stringify(welcome));

    expect(
      api.sendCommand({ kind: 'set_thrust', payload: { x: 0, y: 0 } }),
    ).toBe(false);
    expect(socket.send).not.toHaveBeenCalled();
  });

  it('reports retryable server loss after releasing the active socket', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const firstCallbacks = createCallbacks();
    api.openSession(configuration, 1, firstCallbacks);
    const firstSocket = requireSocket(sockets);
    firstSocket.open();
    firstSocket.serverClose(1013, 'slow_consumer', false);

    expect(firstCallbacks.onDisconnected).toHaveBeenCalledWith(
      expect.objectContaining({ code: 1013, opened: true, retryable: true }),
    );
    api.dispose();
  });

  it('rejects a socket that did not negotiate the exact subprotocol', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);
    socket.protocol = 'blob-royale.snapshot.v1';
    socket.open();

    expect(callbacks.onFailure).toHaveBeenCalledWith(
      expect.objectContaining({
        code: 'SIMULATION.SOCKET_SUBPROTOCOL_INVALID',
      }),
    );
    expect(callbacks.onConnected).not.toHaveBeenCalled();
    expect(socket.close).toHaveBeenCalledWith(1002, 'protocol_error');
  });

  it('closes and reports a retryable timeout when the socket never opens', async () => {
    vi.useFakeTimers();
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);

    await vi.advanceTimersByTimeAsync(WEBSOCKET_CONNECT_TIMEOUT_MILLISECONDS);

    expect(callbacks.onConnected).not.toHaveBeenCalled();
    expect(callbacks.onDisconnected).toHaveBeenCalledOnce();
    const disconnection = vi.mocked(callbacks.onDisconnected).mock
      .calls[0]?.[0];
    expect(disconnection).toMatchObject({
      opened: false,
      reason: 'connect_timeout',
      retryable: true,
    });
    expect(disconnection?.error).toMatchObject({
      code: 'SIMULATION.SOCKET_CONNECT_TIMED_OUT',
      retryable: true,
    });
    expect(socket.close).toHaveBeenCalledWith(4000, 'connect_timeout');
  });

  it('cleans up idempotently and ignores callbacks from the released socket', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, 1, callbacks);
    const socket = requireSocket(sockets);
    const staleMessageHandler = socket.onmessage;

    api.dispose();
    api.dispose();
    staleMessageHandler?.(
      new MessageEvent('message', {
        data: JSON.stringify(welcomeDocument()),
      }),
    );

    expect(socket.close).toHaveBeenCalledTimes(1);
    expect(callbacks.onWelcome).not.toHaveBeenCalled();
  });
});
