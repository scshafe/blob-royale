import { afterEach, describe, expect, it, vi } from 'vitest';

import { SimulationApiError } from './SimulationApiError';
import {
  lobbyDirectoryMessageExample,
  raceModeStateExample,
  sessionCommandEnvelopeExample,
  sessionErrorResponseExample,
  sessionWelcomeMessageExample,
} from './fixtures/protocolV2Examples';
import {
  firstEntity,
  playerEntity,
  snapshotDocument,
  welcomeDocument,
} from './fixtures/sessionFrames';
import {
  SUPPORTED_PROTOCOL_VERSION,
  type SessionSequenceState,
  validateLobbyDirectoryMessage,
  validateSessionCommand,
  validateSessionHttpErrorResponse,
  validateSessionSnapshotMessage,
  validateSessionWelcomeMessage,
} from './sessionProtocolValidation';
import type { SessionCommand } from './simulationProtocolTypes';

const welcomeSequence: SessionSequenceState = Object.freeze({
  messageSequence: 1,
  requestId: sessionWelcomeMessageExample.meta.request_id,
  tickSequence: null,
});

function raceSnapshotDocument(state = structuredClone(raceModeStateExample)) {
  const document = snapshotDocument();
  return {
    ...document,
    data: {
      ...document.data,
      match: {
        ...document.data.match,
        mode: 'race',
        placements: [],
        mode_state: {
          schema_id: 'blob-royale://protocol/v2/mode-state/race',
          value: state,
        },
      },
    },
  };
}

function silenceProtocolWarnings() {
  return vi.spyOn(console, 'warn').mockImplementation(() => undefined);
}

afterEach(() => {
  vi.restoreAllMocks();
});

describe('validateSessionWelcomeMessage', () => {
  it('accepts the golden welcome and freezes it deeply', () => {
    const welcome = validateSessionWelcomeMessage(welcomeDocument(), null);

    expect(Object.isFrozen(welcome)).toBe(true);
    expect(Object.isFrozen(welcome.data.accepted_command_kinds)).toBe(true);
    expect(welcome.data.controller_id).toBe(3);
    expect(welcome.meta.message_sequence).toBe(1);
  });

  it('rejects a second welcome on one connection', () => {
    expect(() =>
      validateSessionWelcomeMessage(welcomeDocument(), welcomeSequence),
    ).toThrow(/exactly one welcome/);
  });

  it('fails closed on an unknown advertised command kind', () => {
    const warn = silenceProtocolWarnings();
    const document = welcomeDocument();
    Reflect.set(document.data, 'accepted_command_kinds', ['set_afterburner']);

    expect(() => validateSessionWelcomeMessage(document, null)).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.SESSION_KIND_UNSUPPORTED',
      }),
    );
    expect(warn).toHaveBeenCalledWith(
      expect.stringContaining('set_afterburner'),
    );
  });
});

describe('validateSessionSnapshotMessage', () => {
  it('accepts the race course with shared standings or an empty standings array', () => {
    for (const standings of [raceModeStateExample.standings, []]) {
      const state = structuredClone(raceModeStateExample);
      state.standings = standings;
      const snapshot = validateSessionSnapshotMessage(
        raceSnapshotDocument(state),
        welcomeSequence,
      );
      expect(snapshot.data.match.mode_state.value).toEqual(state);
      expect(Object.isFrozen(snapshot.data.match.mode_state.value)).toBe(true);
      expect(snapshot.data.match.placements).toEqual([]);
    }
  });

  it.each([
    'track_half_width',
    'checkpoint_radius',
    'track',
    'checkpoints',
    'time_limit_ticks',
    'finish_window_ticks',
    'standings',
  ])('rejects a race block missing required member %s', (member) => {
    const document = raceSnapshotDocument();
    Reflect.deleteProperty(document.data.match.mode_state.value, member);
    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(SimulationApiError);
  });

  it.each([
    ['track_half_width', 0],
    ['checkpoint_radius', -1],
    ['track', [{ x: 1, y: 2 }]],
    ['checkpoints', []],
    [
      'track',
      [
        { x: 1, y: 2 },
        { x: 3, y: 4, z: 5 },
      ],
    ],
    ['checkpoints', [{ x: 1, y: Number.POSITIVE_INFINITY }]],
    ['time_limit_ticks', Number.MAX_SAFE_INTEGER + 1],
    ['finish_window_ticks', -1],
    ['unexpected', 1],
  ])('rejects malformed race member %s', (member, value) => {
    const document = raceSnapshotDocument();
    Reflect.set(document.data.match.mode_state.value, String(member), value);
    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(SimulationApiError);
  });

  it.each([
    ['entity_id', 0],
    ['controller_id', Number.MAX_SAFE_INTEGER + 1],
    ['placement', 0],
    ['placement', 1025],
    ['finished_tick', 0],
    ['finished_tick', 1.5],
    ['eliminated_tick', 1],
  ])('rejects malformed nested race standing %s', (member, value) => {
    const document = raceSnapshotDocument();
    const standing = document.data.match.mode_state.value.standings[0];
    if (standing === undefined) throw new Error('TEST.RACE_STANDING_MISSING');
    Reflect.set(standing, String(member), value);
    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(SimulationApiError);
  });

  it('accepts an ordered race gate count including zero', () => {
    for (const nextCheckpoint of [0, 2]) {
      const document = snapshotDocument();
      Reflect.set(playerEntity(document).components, 'race_progress', {
        next_checkpoint: nextCheckpoint,
      });
      const snapshot = validateSessionSnapshotMessage(
        document,
        welcomeSequence,
      );
      expect(
        snapshot.data.entities.find((entity) => entity.entity_id === 7)
          ?.components.race_progress?.next_checkpoint,
      ).toBe(nextCheckpoint);
    }
  });

  it.each([-1, 0.5, Number.MAX_SAFE_INTEGER + 1])(
    'rejects race progress outside the non-negative safe-integer range: %s',
    (nextCheckpoint) => {
      const document = snapshotDocument();
      Reflect.set(playerEntity(document).components, 'race_progress', {
        next_checkpoint: nextCheckpoint,
      });
      expect(() =>
        validateSessionSnapshotMessage(document, welcomeSequence),
      ).toThrow(SimulationApiError);
    },
  );

  it.each([{}, { next_checkpoint: 1, lap: 2 }])(
    'rejects race progress with missing or extra members: %j',
    (progress) => {
      const document = snapshotDocument();
      Reflect.set(playerEntity(document).components, 'race_progress', progress);
      expect(() =>
        validateSessionSnapshotMessage(document, welcomeSequence),
      ).toThrow(SimulationApiError);
    },
  );

  it('accepts the first snapshot after the welcome and freezes it deeply', () => {
    const snapshot = validateSessionSnapshotMessage(
      snapshotDocument(),
      welcomeSequence,
    );

    expect(Object.isFrozen(snapshot.data.entities)).toBe(true);
    expect(Object.isFrozen(snapshot.data.match.placements)).toBe(true);
    expect(snapshot.meta.message_sequence).toBe(2);
  });

  it('rejects entity identifiers that are not strictly ascending', () => {
    const document = snapshotDocument();
    document.data.entities.reverse();

    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(/strictly ascending/);
  });

  it('rejects a snapshot that arrives before the welcome', () => {
    expect(() =>
      validateSessionSnapshotMessage(snapshotDocument(), null),
    ).toThrow(/before the welcome/);
  });

  it('rejects negative zero that JSON Schema cannot distinguish', () => {
    const document = snapshotDocument();
    const body = playerEntity(document).components.physics_body;
    if (body === undefined) {
      throw new Error('TEST.SNAPSHOT_FIXTURE_BODY_MISSING');
    }
    body.velocity.x = -0;

    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(/negative zero/);
  });

  it('rejects a non-monotonic tick on the same connection', () => {
    const document = snapshotDocument(3);

    expect(() =>
      validateSessionSnapshotMessage(document, {
        messageSequence: 2,
        requestId: document.meta.request_id,
        tickSequence: document.data.tick_sequence,
      }),
    ).toThrow(/strictly increase/);
  });

  it('rejects a skipped transport message sequence', () => {
    const document = snapshotDocument(4);

    expect(() =>
      validateSessionSnapshotMessage(document, {
        messageSequence: 2,
        requestId: document.meta.request_id,
        tickSequence: document.data.tick_sequence - 1,
      }),
    ).toThrow(/increment exactly once/);
  });

  it('rejects a request identifier change within one connection', () => {
    const document = snapshotDocument();

    expect(() =>
      validateSessionSnapshotMessage(document, {
        messageSequence: 1,
        requestId: 'different-request-id',
        tickSequence: null,
      }),
    ).toThrow(/request_id changed/);
  });

  it('fails closed on a component kind the schema set does not name', () => {
    const warn = silenceProtocolWarnings();
    const document = snapshotDocument();
    Reflect.set(firstEntity(document).components, 'gravity_well', {
      strength: 1,
    });

    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.SESSION_KIND_UNSUPPORTED',
      }),
    );
    expect(warn).toHaveBeenCalledWith(expect.stringContaining('gravity_well'));
  });

  it('fails closed on an unregistered mode-state schema id', () => {
    silenceProtocolWarnings();
    const document = snapshotDocument();
    document.data.match.mode_state.schema_id =
      'blob-royale://protocol/v2/mode-state/capture';

    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.SESSION_KIND_UNSUPPORTED',
      }),
    );
  });

  it('fails closed on a protocol minor it cannot decode, before shape validation', () => {
    const document = snapshotDocument();
    // One minor ahead of the supported set; moved 2.1 -> 2.2 when 2.1 became current, 2.2 -> 2.3
    // when 2.2 did, 2.3 -> 2.4 when 2.3 did, 2.4 -> 2.5 when 2.4 did, and 2.5 -> 2.6 when 2.5 did.
    document.meta.protocol_version = '2.6';
    Reflect.deleteProperty(document.data, 'match');

    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.SESSION_VERSION_UNSUPPORTED',
        context: {
          protocol_version: '2.6',
          supported_protocol_version: SUPPORTED_PROTOCOL_VERSION,
        },
      }),
    );
  });

  it('rejects a frame that carries no protocol version at all', () => {
    const document = snapshotDocument();
    Reflect.deleteProperty(document.meta, 'protocol_version');

    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.SESSION_FRAME_INVALID',
      }),
    );
  });
});

describe('validateSessionCommand', () => {
  it('accepts the golden command envelope', () => {
    expect(() =>
      validateSessionCommand({
        kind: 'set_thrust',
        payload: sessionCommandEnvelopeExample.payload,
      }),
    ).not.toThrow();
  });

  it('fails closed on a command kind protocol v2 does not register', () => {
    silenceProtocolWarnings();
    const command: SessionCommand = {
      kind: 'set_thrust',
      payload: { x: 0, y: 0 },
    };
    const unregistered: SessionCommand = { ...command, kind: 'brake' as never };

    expect(() => validateSessionCommand(unregistered)).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.SESSION_KIND_UNSUPPORTED',
      }),
    );
  });

  it('rejects a thrust component outside the closed unit interval', () => {
    expect(() =>
      validateSessionCommand({ kind: 'set_thrust', payload: { x: 1.5, y: 0 } }),
    ).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.COMMAND_REJECTED',
      }),
    );
  });
});

describe('validateLobbyDirectoryMessage', () => {
  const requestId = lobbyDirectoryMessageExample.meta.request_id;

  it('accepts the golden directory and freezes it deeply', () => {
    const directory = validateLobbyDirectoryMessage(
      structuredClone(lobbyDirectoryMessageExample),
      requestId,
    );

    expect(directory.data.lobbies.map((listing) => listing.lobby_id)).toEqual([
      1, 2,
    ]);
    expect(Object.isFrozen(directory)).toBe(true);
    expect(Object.isFrozen(directory.data.lobbies)).toBe(true);
    expect(Object.isFrozen(directory.data.lobbies[0])).toBe(true);
  });

  it('rejects a response whose X-Request-ID does not echo the envelope', () => {
    expect(() =>
      validateLobbyDirectoryMessage(
        structuredClone(lobbyDirectoryMessageExample),
        'another-request',
      ),
    ).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.LOBBY_DIRECTORY_RESPONSE_INVALID',
      }) as SimulationApiError,
    );
  });

  it('rejects rooms that are not numbered 1..N in order', () => {
    const document = structuredClone(lobbyDirectoryMessageExample);
    const second = document.data.lobbies[1];
    if (second === undefined) {
      throw new Error('TEST.DIRECTORY_FIXTURE_TOO_SHORT');
    }
    second.lobby_id = 3;

    expect(() => validateLobbyDirectoryMessage(document, requestId)).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.LOBBY_DIRECTORY_RESPONSE_INVALID',
        context: { actual_lobby_id: 3, position: 2 },
      }) as SimulationApiError,
    );
  });

  it('fails closed on a newer protocol minor before shape validation', () => {
    const document = structuredClone(lobbyDirectoryMessageExample);
    // One minor ahead of the supported set, moved with every minor as the session case above is.
    document.meta.protocol_version = '2.6';

    expect(() => validateLobbyDirectoryMessage(document, requestId)).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.SESSION_VERSION_UNSUPPORTED',
      }) as SimulationApiError,
    );
  });

  it('rejects a listing carrying a member the closed schema does not name', () => {
    const document = structuredClone(lobbyDirectoryMessageExample);
    const first: Record<string, unknown> = document.data.lobbies[0] ?? {};
    first.spectator_count = 1;

    expect(() => validateLobbyDirectoryMessage(document, requestId)).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.LOBBY_DIRECTORY_RESPONSE_INVALID',
      }) as SimulationApiError,
    );
  });
});

describe('validateSessionHttpErrorResponse', () => {
  const requestId = sessionErrorResponseExample.meta.request_id;

  function lobbyFullEnvelope() {
    return {
      ...structuredClone(sessionErrorResponseExample),
      error: {
        code: 'LOBBY.FULL',
        details: { lobby_id: 2 },
        message:
          'Every seat in this lobby is taken; read the directory and choose another.',
        retryable: true,
      },
    };
  }

  it('accepts the golden v2 failure envelope and a lobby refusal at their registered statuses', () => {
    const forwarded = validateSessionHttpErrorResponse(
      structuredClone(sessionErrorResponseExample),
      400,
      requestId,
    );
    expect(forwarded.error.code).toBe('PROTOCOL.INVALID_FORWARDED_CLIENT');
    expect(Object.isFrozen(forwarded.error)).toBe(true);

    const full = validateSessionHttpErrorResponse(
      lobbyFullEnvelope(),
      409,
      requestId,
    );
    expect(full.error.details.lobby_id).toBe(2);
    expect(full.error.retryable).toBe(true);
  });

  it('rejects a status or a retryable flag that disagrees with the code', () => {
    expect(() =>
      validateSessionHttpErrorResponse(lobbyFullEnvelope(), 404, requestId),
    ).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.HTTP_ERROR_RESPONSE_INVALID',
      }) as SimulationApiError,
    );

    const envelope = lobbyFullEnvelope();
    envelope.error.retryable = false;
    expect(() =>
      validateSessionHttpErrorResponse(envelope, 409, requestId),
    ).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.HTTP_ERROR_RESPONSE_INVALID',
      }) as SimulationApiError,
    );
  });

  it('rejects an envelope whose X-Request-ID does not echo', () => {
    expect(() =>
      validateSessionHttpErrorResponse(
        lobbyFullEnvelope(),
        409,
        'another-request',
      ),
    ).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.HTTP_ERROR_RESPONSE_INVALID',
      }) as SimulationApiError,
    );
  });
});

describe('validateSessionCommand for the lobby kinds', () => {
  it('accepts every lobby command at its closed shape', () => {
    const commands: readonly SessionCommand[] = [
      { kind: 'set_seat_count', payload: { seat_count: 4 } },
      { kind: 'seat_npc', payload: { npc_kind: 'wanderer', seat_index: 3 } },
      { kind: 'clear_seat', payload: { seat_index: 1 } },
      { kind: 'start_match', payload: {} },
    ];
    for (const command of commands) {
      expect(() => {
        validateSessionCommand(command);
      }).not.toThrow();
    }
  });

  it('refuses a lobby command outside its schema bound before it reaches the wire', () => {
    // Each of these is a value the published contract does not describe, which the server would
    // answer with `command_payload_invalid` and a closed connection.
    const commands: readonly unknown[] = [
      { kind: 'set_seat_count', payload: { seat_count: 0 } },
      { kind: 'set_seat_count', payload: { seat_count: 65 } },
      { kind: 'set_seat_count', payload: { seat_count: 2.5 } },
      { kind: 'seat_npc', payload: { npc_kind: 'Wanderer', seat_index: 0 } },
      { kind: 'seat_npc', payload: { npc_kind: 'wanderer', seat_index: 64 } },
      { kind: 'clear_seat', payload: { seat_index: -1 } },
      { kind: 'start_match', payload: { now: true } },
    ];
    for (const command of commands) {
      expect(() => {
        validateSessionCommand(command as SessionCommand);
      }).toThrow(
        expect.objectContaining({
          code: 'SIMULATION.COMMAND_REJECTED',
        }) as SimulationApiError,
      );
    }
  });
});
