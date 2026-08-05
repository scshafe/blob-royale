import { afterEach, describe, expect, it, vi } from 'vitest';

import {
  SimulationApi,
  deriveSimulationEndpoints,
  type SimulationSnapshotCallbacks,
  type SimulationWebSocket,
} from './SimulationApi';
import {
  configurationResponseExample,
  errorResponseExample,
  snapshotMessageExample,
} from './fixtures/protocolV1Examples';
import {
  CONFIGURATION_FETCH_TIMEOUT_MILLISECONDS,
  SNAPSHOT_FRAME_MAX_BYTES,
  WEBSOCKET_CONNECT_TIMEOUT_MILLISECONDS,
} from './simulationConstants';

class FakeSimulationWebSocket implements SimulationWebSocket {
  protocol = 'blob-royale.snapshot.v1';
  readyState = 0;
  onclose: ((event: CloseEvent) => void) | null = null;
  onerror: ((event: Event) => void) | null = null;
  onmessage: ((event: MessageEvent<unknown>) => void) | null = null;
  onopen: ((event: Event) => void) | null = null;
  readonly close = vi.fn((code?: number, reason?: string) => {
    void code;
    void reason;
    this.readyState = 3;
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

function createCallbacks(): SimulationSnapshotCallbacks {
  return {
    onConnected: vi.fn(),
    onDisconnected: vi.fn(),
    onFailure: vi.fn(),
    onSnapshot: vi.fn(),
  };
}

afterEach(() => {
  vi.useRealTimers();
});

describe('deriveSimulationEndpoints', () => {
  it('derives the exact HTTP and WebSocket paths on one HTTPS authority', () => {
    expect(deriveSimulationEndpoints(secureLocation)).toEqual({
      configurationUrl: 'https://game.example.test:8443/api/v1/config',
      snapshotWebSocketUrl: 'wss://game.example.test:8443/api/v1/snapshots',
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
      snapshotWebSocketUrl: 'ws://127.0.0.1:5173/api/v1/snapshots',
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

describe('SimulationApi snapshot lifecycle', () => {
  async function createConnectedApi() {
    const sockets: FakeSimulationWebSocket[] = [];
    const api = new SimulationApi({
      fetchImplementation: createFetchMock(
        configurationJsonResponse(
          structuredClone(configurationResponseExample),
        ),
      ),
      location: secureLocation,
      webSocketFactory: (url, subprotocol) => {
        expect(url).toBe('wss://game.example.test:8443/api/v1/snapshots');
        expect(subprotocol).toBe('blob-royale.snapshot.v1');
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

  it('owns one socket and delivers only validated monotonic snapshots', async () => {
    const { api, configuration, sockets } = await createConnectedApi();
    const callbacks = createCallbacks();
    api.openSnapshotStream(configuration, callbacks);
    const socket = sockets[0];
    if (socket === undefined) {
      throw new Error('TEST.SOCKET_NOT_CREATED');
    }

    expect(() => api.openSnapshotStream(configuration, callbacks)).toThrow(
      /only one WebSocket/,
    );

    socket.open();
    const firstSnapshot = structuredClone(snapshotMessageExample);
    firstSnapshot.meta.message_sequence = 1;
    socket.receive(JSON.stringify(firstSnapshot));

    const secondSnapshot = structuredClone(firstSnapshot);
    secondSnapshot.meta.message_sequence = 2;
    secondSnapshot.data.tick_sequence += 1;
    socket.receive(JSON.stringify(secondSnapshot));

    expect(callbacks.onConnected).toHaveBeenCalledTimes(1);
    expect(callbacks.onSnapshot).toHaveBeenCalledTimes(2);
    expect(
      Object.isFrozen(
        vi.mocked(callbacks.onSnapshot).mock.calls[0]?.[0].data.players,
      ),
    ).toBe(true);
    expect(callbacks.onFailure).not.toHaveBeenCalled();
  });

  it('rejects an oversized frame before parsing or rendering', async () => {
    const { api, configuration, sockets } = await createConnectedApi();
    const callbacks = createCallbacks();
    api.openSnapshotStream(configuration, callbacks);
    const socket = sockets[0];
    if (socket === undefined) {
      throw new Error('TEST.SOCKET_NOT_CREATED');
    }
    socket.open();

    socket.receive('x'.repeat(SNAPSHOT_FRAME_MAX_BYTES + 1));

    expect(callbacks.onSnapshot).not.toHaveBeenCalled();
    expect(callbacks.onFailure).toHaveBeenCalledWith(
      expect.objectContaining({
        code: 'SIMULATION.SNAPSHOT_FRAME_TOO_LARGE',
      }),
    );
    expect(socket.close).toHaveBeenCalledOnce();
    expect(socket.close).toHaveBeenCalledWith(1009, 'snapshot_frame_too_large');
  });

  it('closes malformed protocol payloads with a protocol-error close', async () => {
    const { api, configuration, sockets } = await createConnectedApi();
    const callbacks = createCallbacks();
    api.openSnapshotStream(configuration, callbacks);
    const socket = sockets[0];
    if (socket === undefined) {
      throw new Error('TEST.SOCKET_NOT_CREATED');
    }
    socket.open();

    socket.receive('{');

    expect(callbacks.onFailure).toHaveBeenCalledWith(
      expect.objectContaining({
        code: 'SIMULATION.SNAPSHOT_FRAME_INVALID',
      }),
    );
    expect(socket.close).toHaveBeenCalledWith(1002, 'protocol_error');
  });

  it('reports retryable server loss after releasing the active socket', async () => {
    const { api, configuration, sockets } = await createConnectedApi();
    const firstCallbacks = createCallbacks();
    api.openSnapshotStream(configuration, firstCallbacks);
    const firstSocket = sockets[0];
    if (firstSocket === undefined) {
      throw new Error('TEST.SOCKET_NOT_CREATED');
    }
    firstSocket.open();
    firstSocket.serverClose(1013, 'slow_consumer', false);

    expect(firstCallbacks.onDisconnected).toHaveBeenCalledWith(
      expect.objectContaining({ code: 1013, retryable: true }),
    );
    api.dispose();
  });

  it('rejects a socket that did not negotiate the exact subprotocol', async () => {
    const { api, configuration, sockets } = await createConnectedApi();
    const callbacks = createCallbacks();
    api.openSnapshotStream(configuration, callbacks);
    const socket = sockets[0];
    if (socket === undefined) {
      throw new Error('TEST.SOCKET_NOT_CREATED');
    }
    socket.protocol = '';
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
    const { api, configuration, sockets } = await createConnectedApi();
    const callbacks = createCallbacks();
    api.openSnapshotStream(configuration, callbacks);
    const socket = sockets[0];
    if (socket === undefined) {
      throw new Error('TEST.SOCKET_NOT_CREATED');
    }

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
    const { api, configuration, sockets } = await createConnectedApi();
    const callbacks = createCallbacks();
    api.openSnapshotStream(configuration, callbacks);
    const socket = sockets[0];
    if (socket === undefined) {
      throw new Error('TEST.SOCKET_NOT_CREATED');
    }
    const staleMessageHandler = socket.onmessage;

    api.dispose();
    api.dispose();
    staleMessageHandler?.(
      new MessageEvent('message', {
        data: JSON.stringify(snapshotMessageExample),
      }),
    );

    expect(socket.close).toHaveBeenCalledTimes(1);
    expect(callbacks.onSnapshot).not.toHaveBeenCalled();
  });
});
