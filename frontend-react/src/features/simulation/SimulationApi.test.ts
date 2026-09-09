import { afterEach, describe, expect, it, vi } from 'vitest';

import {
  SimulationApi,
  deriveSimulationEndpoints,
  type SimulationSessionCallbacks,
  type SimulationWebSocket,
} from './SimulationApi';
import {
  configurationResponseExample,
  errorResponseExample,
} from './fixtures/protocolV1Examples';
import {
  firstEntity,
  snapshotDocument,
  welcomeDocument,
} from './fixtures/sessionFrames';
import {
  CONFIGURATION_FETCH_TIMEOUT_MILLISECONDS,
  SESSION_FRAME_MAX_BYTES,
  WEBSOCKET_CONNECT_TIMEOUT_MILLISECONDS,
} from './simulationConstants';

class FakeSimulationWebSocket implements SimulationWebSocket {
  protocol = 'blob-royale.session.v2';
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
  };
}

afterEach(() => {
  vi.useRealTimers();
});

describe('deriveSimulationEndpoints', () => {
  it('derives the exact HTTP and WebSocket paths on one HTTPS authority', () => {
    expect(deriveSimulationEndpoints(secureLocation)).toEqual({
      configurationUrl: 'https://game.example.test:8443/api/v1/config',
      sessionWebSocketUrl: 'wss://game.example.test:8443/api/v2/session',
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
      sessionWebSocketUrl: 'ws://127.0.0.1:5173/api/v2/session',
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
  async function createJoinedApi() {
    const sockets: FakeSimulationWebSocket[] = [];
    const api = new SimulationApi({
      fetchImplementation: createFetchMock(
        configurationJsonResponse(
          structuredClone(configurationResponseExample),
        ),
      ),
      location: secureLocation,
      webSocketFactory: (url, subprotocol) => {
        expect(url).toBe('wss://game.example.test:8443/api/v2/session');
        expect(subprotocol).toBe('blob-royale.session.v2');
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

  it('owns one socket and delivers a welcome before validated monotonic snapshots', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, callbacks);
    const socket = requireSocket(sockets);

    expect(() => api.openSession(configuration, callbacks)).toThrow(
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

  it('stays open and frameless for a joiner the match has deferred', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, callbacks);
    const socket = requireSocket(sockets);

    socket.open();

    expect(callbacks.onConnected).toHaveBeenCalledTimes(1);
    expect(callbacks.onWelcome).not.toHaveBeenCalled();
    expect(callbacks.onSnapshot).not.toHaveBeenCalled();
    expect(callbacks.onFailure).not.toHaveBeenCalled();
    expect(callbacks.onDisconnected).not.toHaveBeenCalled();
    expect(socket.close).not.toHaveBeenCalled();
  });

  it('refuses a snapshot that arrives before the welcome', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, callbacks);
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
    api.openSession(configuration, callbacks);
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
    api.openSession(configuration, callbacks);
    const socket = requireSocket(sockets);
    socket.open();

    const welcome = welcomeDocument();
    // One minor ahead of whatever the client supports. This moved 2.1 -> 2.2 when the server
    // published `lethal_on_contact`, 2.2 -> 2.3 when the royale mode-state block gained
    // `elimination_grace_ticks`, and 2.3 -> 2.4 when the lobby commands and the match seat roster
    // landed, because a test named "a newer protocol minor" that names the current one stops
    // testing anything.
    welcome.meta.protocol_version = '2.4';
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
    api.openSession(configuration, callbacks);
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
    api.openSession(configuration, callbacks);
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
    api.openSession(configuration, callbacks);
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

  it('refuses a command whose payload the closed schema would reject', async () => {
    vi.spyOn(console, 'warn').mockImplementation(() => undefined);
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, callbacks);
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
    api.openSession(configuration, callbacks);
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
    api.openSession(configuration, firstCallbacks);
    const firstSocket = requireSocket(sockets);
    firstSocket.open();
    firstSocket.serverClose(1013, 'slow_consumer', false);

    expect(firstCallbacks.onDisconnected).toHaveBeenCalledWith(
      expect.objectContaining({ code: 1013, retryable: true }),
    );
    api.dispose();
  });

  it('rejects a socket that did not negotiate the exact subprotocol', async () => {
    const { api, configuration, sockets } = await createJoinedApi();
    const callbacks = createCallbacks();
    api.openSession(configuration, callbacks);
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
    api.openSession(configuration, callbacks);
    const socket = requireSocket(sockets);

    await vi.advanceTimersByTimeAsync(WEBSOCKET_CONNECT_TIMEOUT_MILLISECONDS);

    expect(callbacks.onConnected).not.toHaveBeenCalled();
    expect(callbacks.onDisconnected).toHaveBeenCalledOnce();
    const disconnection = vi.mocked(callbacks.onDisconnected).mock
      .calls[0]?.[0];
    expect(disconnection).toMatchObject({
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
    api.openSession(configuration, callbacks);
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
