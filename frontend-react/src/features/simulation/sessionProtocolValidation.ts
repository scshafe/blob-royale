import type { ValidateFunction } from 'ajv/dist/2020.js';

import { SimulationApiError } from './SimulationApiError';
import { protocolV2Schemas } from './generated/protocolV2Schemas.generated';
import {
  createProtocolAjv,
  deepFreeze,
  formatValidationErrors,
} from './protocolValidationSupport';
import type {
  SessionCommand,
  SessionSnapshotMessage,
  SessionWelcomeMessage,
} from './simulationProtocolTypes';

const WELCOME_MESSAGE_SCHEMA_ID =
  'https://schemas.blob-royale.invalid/protocol/v2/welcome-message.schema.json';
const SNAPSHOT_MESSAGE_SCHEMA_ID =
  'https://schemas.blob-royale.invalid/protocol/v2/snapshot-message.schema.json';
const COMMAND_ENVELOPE_SCHEMA_ID =
  'https://schemas.blob-royale.invalid/protocol/v2/command-envelope.schema.json';

/**
 * The kind vocabularies come from the generated schema bundle, never from a second hand-written
 * list: a kind this client does not know is exactly a kind the accepted schemas do not name.
 */
const KNOWN_COMPONENT_KINDS: ReadonlySet<string> = new Set<string>(
  protocolV2Schemas.common.$defs.component_kind.enum,
);
const KNOWN_MODE_STATE_SCHEMA_IDS: ReadonlySet<string> = new Set<string>(
  protocolV2Schemas.common.$defs.mode_state_schema_id.enum,
);
const KNOWN_COMMAND_KINDS: ReadonlySet<string> = new Set<string>(
  protocolV2Schemas.common.$defs.command_kind.enum,
);

/** The exact wire version this build decodes, taken from the const in `common.schema.json`. */
export const SUPPORTED_PROTOCOL_VERSION: string =
  protocolV2Schemas.common.$defs.protocol_version.const;

export interface SessionSequenceState {
  readonly messageSequence: number;
  readonly requestId: string;
  readonly tickSequence: number | null;
}

const ajv = createProtocolAjv();
for (const schema of Object.values(protocolV2Schemas)) {
  ajv.addSchema(schema);
}

function requireValidator<T>(schemaId: string): ValidateFunction<T> {
  const validator = ajv.getSchema<T>(schemaId);
  if (validator === undefined) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_FRAME_INVALID',
      `Required protocol validator was not registered: ${schemaId}`,
      { context: { schema_id: schemaId } },
    );
  }
  return validator;
}

const validateWelcomeSchema = requireValidator<SessionWelcomeMessage>(
  WELCOME_MESSAGE_SCHEMA_ID,
);
const validateSnapshotSchema = requireValidator<SessionSnapshotMessage>(
  SNAPSHOT_MESSAGE_SCHEMA_ID,
);
// Typed `unknown` deliberately: the outbound envelope is already a `SessionCommand`, and a
// narrowing guard would make the failure branch unreachable rather than checked.
const validateCommandEnvelopeSchema = requireValidator<unknown>(
  COMMAND_ENVELOPE_SCHEMA_ID,
);

function isRecord(value: unknown): value is Readonly<Record<string, unknown>> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/**
 * Logs an unknown wire kind by name exactly once per detection, before the caller closes. Protocol
 * v2 requires the name to survive in a log even though the frame itself never reaches a component.
 */
function warnUnknownKind(
  detail: Readonly<Record<string, string | number>>,
): void {
  console.warn(
    JSON.stringify({
      event: 'protocol.v2.unknown_kind',
      ...detail,
    }),
  );
}

function unknownKindError(
  message: string,
  context: Readonly<Record<string, string | number>>,
): SimulationApiError {
  warnUnknownKind(context);
  return new SimulationApiError(
    'SIMULATION.SESSION_KIND_UNSUPPORTED',
    message,
    {
      context,
    },
  );
}

function parseVersionPart(part: string | undefined): number | null {
  if (part === undefined || !/^(?:0|[1-9][0-9]*)$/.test(part)) {
    return null;
  }
  return Number(part);
}

/**
 * Version first. A client reads `meta.protocol_version` before validating or interpreting anything
 * else, because that is the only ordering in which it can answer a document it cannot validate.
 */
export function assertSupportedProtocolVersion(document: unknown): void {
  const envelope = isRecord(document) ? document : null;
  const meta =
    envelope !== null && isRecord(envelope.meta) ? envelope.meta : null;
  const declaredVersion =
    meta !== null && typeof meta.protocol_version === 'string'
      ? meta.protocol_version
      : null;
  if (declaredVersion === null) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_FRAME_INVALID',
      'Session frame carries no protocol version.',
    );
  }

  const declaredParts = declaredVersion.split('.');
  const supportedParts = SUPPORTED_PROTOCOL_VERSION.split('.');
  const declaredMajor = parseVersionPart(declaredParts[0]);
  const declaredMinor = parseVersionPart(declaredParts[1]);
  const supportedMajor = parseVersionPart(supportedParts[0]);
  const supportedMinor = parseVersionPart(supportedParts[1]);
  if (
    declaredParts.length !== 2 ||
    declaredMajor === null ||
    declaredMinor === null ||
    supportedMajor === null ||
    supportedMinor === null
  ) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_FRAME_INVALID',
      'Session frame protocol version is not a major.minor pair.',
      { context: { protocol_version: declaredVersion } },
    );
  }

  if (declaredMajor !== supportedMajor || declaredMinor > supportedMinor) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_VERSION_UNSUPPORTED',
      'Session frame names a protocol version this client cannot decode.',
      {
        context: {
          protocol_version: declaredVersion,
          supported_protocol_version: SUPPORTED_PROTOCOL_VERSION,
        },
      },
    );
  }
}

/**
 * Fails closed on a kind the accepted schema set does not name, before Ajv reports it as a generic
 * shape failure, so the close code says which kind and the log names it. An unrendered entity is an
 * invisible entity, and an invisible entity is an incorrect world, not a degraded one.
 */
function assertKnownSnapshotKinds(document: unknown): void {
  const envelope = isRecord(document) ? document : null;
  const data =
    envelope !== null && isRecord(envelope.data) ? envelope.data : null;
  if (data === null) {
    return;
  }

  if (Array.isArray(data.entities)) {
    for (const entity of data.entities) {
      if (!isRecord(entity) || !isRecord(entity.components)) {
        continue;
      }
      for (const componentKind of Object.keys(entity.components)) {
        if (!KNOWN_COMPONENT_KINDS.has(componentKind)) {
          throw unknownKindError(
            'Session snapshot carries a component kind this client does not know.',
            {
              component_kind: componentKind,
              entity_id:
                typeof entity.entity_id === 'number' ? entity.entity_id : -1,
            },
          );
        }
      }
    }
  }

  const match = isRecord(data.match) ? data.match : null;
  const modeState =
    match !== null && isRecord(match.mode_state) ? match.mode_state : null;
  if (
    modeState !== null &&
    typeof modeState.schema_id === 'string' &&
    !KNOWN_MODE_STATE_SCHEMA_IDS.has(modeState.schema_id)
  ) {
    throw unknownKindError(
      'Session snapshot carries a mode-state schema id this client does not know.',
      { mode_state_schema_id: modeState.schema_id },
    );
  }
}

function assertKnownWelcomeKinds(document: unknown): void {
  const envelope = isRecord(document) ? document : null;
  const data =
    envelope !== null && isRecord(envelope.data) ? envelope.data : null;
  if (data === null || !Array.isArray(data.accepted_command_kinds)) {
    return;
  }

  for (const commandKind of data.accepted_command_kinds) {
    if (
      typeof commandKind === 'string' &&
      !KNOWN_COMMAND_KINDS.has(commandKind)
    ) {
      throw unknownKindError(
        'Session welcome advertises a command kind this client does not know.',
        { command_kind: commandKind },
      );
    }
  }
}

function assertNoNegativeZero(
  value: number,
  fieldName: string,
  entityId: number,
): void {
  if (Object.is(value, -0)) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_INVARIANT_VIOLATION',
      `Session snapshot ${fieldName} contains forbidden negative zero.`,
      { context: { entity_id: entityId, field_name: fieldName } },
    );
  }
}

function assertSnapshotEntityInvariants(
  snapshot: SessionSnapshotMessage,
): void {
  let previousEntityId = 0;
  for (const entity of snapshot.data.entities) {
    if (entity.entity_id <= previousEntityId) {
      throw new SimulationApiError(
        'SIMULATION.SESSION_INVARIANT_VIOLATION',
        'Session snapshot entity_id values must be unique and strictly ascending.',
        {
          context: {
            entity_id: entity.entity_id,
            previous_entity_id: previousEntityId,
          },
        },
      );
    }
    previousEntityId = entity.entity_id;

    const body = entity.components.physics_body;
    if (body !== undefined) {
      assertNoNegativeZero(body.position.x, 'position.x', entity.entity_id);
      assertNoNegativeZero(body.position.y, 'position.y', entity.entity_id);
      assertNoNegativeZero(body.velocity.x, 'velocity.x', entity.entity_id);
      assertNoNegativeZero(body.velocity.y, 'velocity.y', entity.entity_id);
      assertNoNegativeZero(
        body.acceleration.x,
        'acceleration.x',
        entity.entity_id,
      );
      assertNoNegativeZero(
        body.acceleration.y,
        'acceleration.y',
        entity.entity_id,
      );
      assertNoNegativeZero(body.radius, 'radius', entity.entity_id);
      assertNoNegativeZero(body.mass, 'mass', entity.entity_id);
    }

    const zone = entity.components.zone;
    if (zone !== undefined) {
      assertNoNegativeZero(zone.center.x, 'zone.center.x', entity.entity_id);
      assertNoNegativeZero(zone.center.y, 'zone.center.y', entity.entity_id);
      assertNoNegativeZero(zone.radius, 'zone.radius', entity.entity_id);
    }
  }
}

/** Validates the one welcome frame of a session and freezes it before any component sees it. */
export function validateSessionWelcomeMessage(
  document: unknown,
  previousSequence: SessionSequenceState | null,
): SessionWelcomeMessage {
  assertSupportedProtocolVersion(document);
  assertKnownWelcomeKinds(document);

  if (!validateWelcomeSchema(document)) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_FRAME_INVALID',
      'Session welcome does not match protocol v2.',
      {
        context: {
          validation_errors: formatValidationErrors(
            validateWelcomeSchema.errors,
          ),
        },
      },
    );
  }

  if (previousSequence !== null) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_INVARIANT_VIOLATION',
      'A session receives exactly one welcome, before every snapshot.',
      {
        context: {
          previous_message_sequence: previousSequence.messageSequence,
        },
      },
    );
  }

  return deepFreeze(document);
}

/**
 * Validates one snapshot frame against the welcome that preceded it. A snapshot with no preceding
 * welcome is a lost frame, not a frame to render: the client would not know which blob is its own.
 */
export function validateSessionSnapshotMessage(
  document: unknown,
  previousSequence: SessionSequenceState | null,
): SessionSnapshotMessage {
  assertSupportedProtocolVersion(document);
  assertKnownSnapshotKinds(document);

  if (!validateSnapshotSchema(document)) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_FRAME_INVALID',
      'Session snapshot does not match protocol v2.',
      {
        context: {
          validation_errors: formatValidationErrors(
            validateSnapshotSchema.errors,
          ),
        },
      },
    );
  }

  if (previousSequence === null) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_INVARIANT_VIOLATION',
      'Session snapshot arrived before the welcome frame.',
      {
        context: { message_sequence: document.meta.message_sequence },
      },
    );
  }

  const expectedMessageSequence = previousSequence.messageSequence + 1;
  if (document.meta.message_sequence !== expectedMessageSequence) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_INVARIANT_VIOLATION',
      'Session message_sequence must increment exactly once per delivered frame.',
      {
        context: {
          actual_message_sequence: document.meta.message_sequence,
          expected_message_sequence: expectedMessageSequence,
        },
      },
    );
  }

  if (document.meta.request_id !== previousSequence.requestId) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_INVARIANT_VIOLATION',
      'Session request_id changed within one WebSocket connection.',
      {
        context: {
          actual_request_id: document.meta.request_id,
          expected_request_id: previousSequence.requestId,
        },
      },
    );
  }

  if (
    previousSequence.tickSequence !== null &&
    document.data.tick_sequence <= previousSequence.tickSequence
  ) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_INVARIANT_VIOLATION',
      'Session tick_sequence must strictly increase.',
      {
        context: {
          actual_tick_sequence: document.data.tick_sequence,
          previous_tick_sequence: previousSequence.tickSequence,
        },
      },
    );
  }

  assertSnapshotEntityInvariants(document);
  return deepFreeze(document);
}

/**
 * Validates an outbound command against the same closed schema the server enforces. A command the
 * server would refuse closes the connection, so this client refuses to put one on the wire.
 */
export function validateSessionCommand(command: SessionCommand): void {
  if (!KNOWN_COMMAND_KINDS.has(command.kind)) {
    throw unknownKindError(
      'Client command names a kind protocol v2 does not register.',
      { command_kind: command.kind },
    );
  }

  // Validated through an `unknown` alias so the failure branch stays reachable: a type-guard call
  // on `command` itself would narrow the refusal path to `never`.
  const commandDocument: unknown = command;
  if (!validateCommandEnvelopeSchema(commandDocument)) {
    throw new SimulationApiError(
      'SIMULATION.COMMAND_REJECTED',
      'Client command does not match protocol v2.',
      {
        context: {
          command_kind: command.kind,
          validation_errors: formatValidationErrors(
            validateCommandEnvelopeSchema.errors,
          ),
        },
      },
    );
  }
}
