import { afterEach, describe, expect, it, vi } from 'vitest';

import { SimulationApiError } from './SimulationApiError';
import {
  sessionCommandEnvelopeExample,
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
  validateSessionCommand,
  validateSessionSnapshotMessage,
  validateSessionWelcomeMessage,
} from './sessionProtocolValidation';
import type { SessionCommand } from './simulationProtocolTypes';

const welcomeSequence: SessionSequenceState = Object.freeze({
  messageSequence: 1,
  requestId: sessionWelcomeMessageExample.meta.request_id,
  tickSequence: null,
});

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
    // One minor ahead of the supported set; moved 2.1 -> 2.2 when 2.1 became current.
    document.meta.protocol_version = '2.2';
    Reflect.deleteProperty(document.data, 'match');

    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.SESSION_VERSION_UNSUPPORTED',
        context: {
          protocol_version: '2.2',
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
