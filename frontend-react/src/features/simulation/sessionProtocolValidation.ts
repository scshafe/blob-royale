import type { ValidateFunction } from 'ajv/dist/2020.js';

import { SimulationApiError } from './SimulationApiError';
import { npcCatalogueContains, npcCatalogueFromWelcome } from './npcCatalogue';
import { protocolV3Schemas } from './generated/protocolV3Schemas.generated';
import {
  createProtocolAjv,
  assertNoNegativeZero,
  deepFreeze,
  formatValidationErrors,
} from './protocolValidationSupport';
import type {
  SessionCommand,
  SessionHttpErrorResponse,
  SessionLobbyDirectoryMessage,
  SessionNpcCatalogue,
  SessionSnapshotMessage,
  SessionTerrain,
  SessionSetMovementTuningCommand,
  SessionTuningResult,
  SessionWelcomeMessage,
} from './simulationProtocolTypes';
import { HTTP_ERROR_REGISTRY } from './simulationProtocolValidation';
import { assertTerrainSemantics } from './terrainValidation';

const WELCOME_MESSAGE_SCHEMA_ID =
  'https://schemas.blob-royale.invalid/protocol/v3/welcome-message.schema.json';
const SNAPSHOT_MESSAGE_SCHEMA_ID =
  'https://schemas.blob-royale.invalid/protocol/v3/snapshot-message.schema.json';
const COMMAND_ENVELOPE_SCHEMA_ID =
  'https://schemas.blob-royale.invalid/protocol/v3/command-envelope.schema.json';
const LOBBY_DIRECTORY_MESSAGE_SCHEMA_ID =
  'https://schemas.blob-royale.invalid/protocol/v3/lobby-directory-message.schema.json';
const HTTP_ERROR_RESPONSE_SCHEMA_ID =
  'https://schemas.blob-royale.invalid/protocol/v3/error-response.schema.json';

/**
 * Status and retryability for the active v3 error vocabulary. `satisfies` over the generated enum is what makes a code the schema names and this
 * table forgets a build failure rather than an envelope the client cannot check.
 */
const V3_HTTP_ERROR_REGISTRY = Object.freeze({
  ...HTTP_ERROR_REGISTRY,
  'LOBBY.FULL': { retryable: true, status: 409 },
  'LOBBY.NOT_FOUND': { retryable: false, status: 404 },
  'LOBBY.UNAVAILABLE': { retryable: true, status: 503 },
  'PROTOCOL.INVALID_FORWARDED_CLIENT': { retryable: false, status: 400 },
  'PROTOCOL.SESSION_VERSION_UPGRADE_REQUIRED': {
    retryable: false,
    status: 426,
  },
} as const satisfies Record<
  SessionHttpErrorResponse['error']['code'],
  { readonly retryable: boolean; readonly status: number }
>);

/**
 * The kind vocabularies come from the generated schema bundle, never from a second hand-written
 * list: a kind this client does not know is exactly a kind the accepted schemas do not name.
 */
const KNOWN_COMPONENT_KINDS: ReadonlySet<string> = new Set<string>(
  protocolV3Schemas.common.$defs.component_kind.enum,
);
const KNOWN_MODE_STATE_SCHEMA_IDS: ReadonlySet<string> = new Set<string>(
  protocolV3Schemas.common.$defs.mode_state_schema_id.enum,
);
const KNOWN_COMMAND_KINDS: ReadonlySet<string> = new Set<string>(
  protocolV3Schemas.common.$defs.command_kind.enum,
);

/** The exact wire version this build decodes, taken from the const in `common.schema.json`. */
export const SUPPORTED_PROTOCOL_VERSION: string =
  protocolV3Schemas.common.$defs.protocol_version.const;

export interface SessionSequenceState {
  readonly messageSequence: number;
  readonly requestId: string;
  readonly tickSequence: number | null;
  /** Immutable terrain admitted with this connection's welcome, never replaced by a frame. */
  readonly terrain: SessionTerrain;
  /** Current welcome authority, retained through every frame and retired with the socket. */
  readonly npcCatalogue: SessionNpcCatalogue;
}

const ajv = createProtocolAjv();
for (const schema of Object.values(protocolV3Schemas)) {
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
const validateLobbyDirectorySchema =
  requireValidator<SessionLobbyDirectoryMessage>(
    LOBBY_DIRECTORY_MESSAGE_SCHEMA_ID,
  );
const validateHttpErrorSchema = requireValidator<SessionHttpErrorResponse>(
  HTTP_ERROR_RESPONSE_SCHEMA_ID,
);

function isRecord(value: unknown): value is Readonly<Record<string, unknown>> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

/**
 * Logs an unknown wire kind by name exactly once per detection, before the caller closes. Protocol
 * v3 requires the name to survive in a log even though the frame itself never reaches a component.
 */
function warnUnknownKind(
  detail: Readonly<Record<string, string | number>>,
): void {
  console.warn(
    JSON.stringify({
      event: 'protocol.v3.unknown_kind',
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

    const generation = entity.components.controllable?.input_generation;
    if (generation !== undefined && generation > snapshot.data.tick_sequence) {
      throw new SimulationApiError(
        'SIMULATION.SESSION_INVARIANT_VIOLATION',
        'Controllable input_generation must be covered by its snapshot tick.',
        {
          context: {
            entity_id: entity.entity_id,
            input_generation: generation,
            tick_sequence: snapshot.data.tick_sequence,
          },
        },
      );
    }
    const stun = entity.components.stun;
    if (
      stun !== undefined &&
      (stun.activation_tick >= stun.expiry_tick ||
        stun.activation_tick > snapshot.data.tick_sequence)
    ) {
      throw new SimulationApiError(
        'SIMULATION.SESSION_INVARIANT_VIOLATION',
        'Stun requires an ordered window activated by its snapshot tick.',
        {
          context: {
            entity_id: entity.entity_id,
            ...stun,
            tick_sequence: snapshot.data.tick_sequence,
          },
        },
      );
    }

    // Shield publishes four endpoints of one interval family, and JSON Schema can bound each number
    // on its own but cannot state that they nest. The comparisons are `<=` on the protection
    // endpoints rather than `<`: a shield the status system cancels at its own activation tick
    // legitimately publishes zero-length protection, and that empty window is precisely how a reader
    // distinguishes a cancelled shield from a cooldown that has not elapsed -- rejecting it would
    // close a connection over a frame the server is required to send. `parry_stun_duration_ticks` is
    // the duration the defender captured when it activated, so a zero would describe a parry that
    // stuns nobody; it is checked here as well as bounded by the schema because it is part of the
    // same published contract and the reader that explains a parry reads it from this frame.
    const shield = entity.components.shield;
    if (
      shield !== undefined &&
      (shield.activation_tick > shield.perfect_expiry_tick ||
        shield.perfect_expiry_tick > shield.shield_expiry_tick ||
        shield.activation_tick > shield.cooldown_expiry_tick ||
        shield.activation_tick > snapshot.data.tick_sequence ||
        shield.parry_stun_duration_ticks < 1)
    ) {
      throw new SimulationApiError(
        'SIMULATION.SESSION_INVARIANT_VIOLATION',
        'Shield requires nested windows activated by its snapshot tick.',
        {
          context: {
            entity_id: entity.entity_id,
            ...shield,
            tick_sequence: snapshot.data.tick_sequence,
          },
        },
      );
    }

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

    const hillMotion = entity.components.hill_motion;
    if (hillMotion !== undefined) {
      // JSON Schema treats -0 as zero; match the canonical Vector2 publication invariant here.
      assertNoNegativeZero(
        hillMotion.velocity.x,
        'hill_motion.velocity.x',
        entity.entity_id,
      );
      assertNoNegativeZero(
        hillMotion.velocity.y,
        'hill_motion.velocity.y',
        entity.entity_id,
      );
    }
  }
}

/** The schema proves the name grammar; only this session context can prove its binding. */
function assertRaceTerrainBinding(
  snapshot: SessionSnapshotMessage,
  terrain: SessionTerrain,
): void {
  if (
    snapshot.data.match.mode_state.schema_id !==
    'blob-royale://protocol/v3/mode-state/race'
  ) {
    return;
  }
  const state: Readonly<Record<string, unknown>> =
    snapshot.data.match.mode_state.value;
  const road = state.road;
  const corridor = terrain.corridors.find(
    (candidate) => candidate.name === road,
  );
  if (corridor === undefined) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_INVARIANT_VIOLATION',
      'Race road does not name a corridor in this session terrain.',
      { context: { road: typeof road === 'string' ? road : null } },
    );
  }
  if (
    typeof state.checkpoint_radius !== 'number' ||
    state.checkpoint_radius > corridor.half_width
  ) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_INVARIANT_VIOLATION',
      'Race checkpoint radius exceeds its selected terrain corridor half-width.',
      { context: { road: corridor.name, half_width: corridor.half_width } },
    );
  }
}

/** Schema and terrain semantics precede freezing; the API additionally checks fetched bounds. */
export function validateSessionWelcomeMessage(
  document: unknown,
  previousSequence: SessionSequenceState | null,
): SessionWelcomeMessage {
  assertSupportedProtocolVersion(document);
  assertKnownWelcomeKinds(document);

  if (!validateWelcomeSchema(document)) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_FRAME_INVALID',
      'Session welcome does not match protocol v3.',
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

  assertTerrainSemantics(document.data.terrain);
  npcCatalogueFromWelcome(document.data);
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
      'Session snapshot does not match protocol v3.',
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
  document.data.match.seats.forEach((seat, seatIndex) => {
    if (
      seat.kind === 'npc' &&
      (seat.npc_kind === null ||
        !npcCatalogueContains(
          previousSequence.npcCatalogue,
          seat.npc_kind,
          seat.profile_name,
        ))
    ) {
      throw new SimulationApiError(
        'SIMULATION.SESSION_INVARIANT_VIOLATION',
        'Published NPC seat must match a declaration in the current welcome catalogue.',
        {
          context: {
            seat_index: seatIndex,
            npc_kind: seat.npc_kind,
            profile_name: seat.profile_name ?? null,
          },
        },
      );
    }
  });
  assertRaceTerrainBinding(document, previousSequence.terrain);
  assertMovementSnapshotInvariants(document);
  return deepFreeze(document);
}

function assertMovementSnapshotInvariants(
  snapshot: SessionSnapshotMessage,
): void {
  const movement = snapshot.data.match.movement;
  const result = snapshot.data.tuning_result;
  if (
    movement.effective_tick > snapshot.data.tick_sequence ||
    (result !== null &&
      result.decision_tick !== null &&
      result.decision_tick > snapshot.data.tick_sequence) ||
    (result !== null &&
      result.revision !== null &&
      result.revision > movement.revision)
  ) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_INVARIANT_VIOLATION',
      'Movement state and tuning results must be covered by their snapshot tick and revision.',
    );
  }
}

/** Admission uses the API's pending request; a standalone schema cannot prove correlation. */
export function validateSessionTuningResult(
  snapshot: SessionSnapshotMessage,
  pending: SessionSetMovementTuningCommand['payload'] | null,
): SessionTuningResult | null {
  const result = snapshot.data.tuning_result;
  if (result === null) return null;
  if (
    pending === null ||
    result.tuning_request_id !== pending.tuning_request_id
  ) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_INVARIANT_VIOLATION',
      "A tuning result must match this connection's pending request exactly.",
      {
        context: {
          tuning_request_id: result.tuning_request_id,
          pending_tuning_request_id: pending?.tuning_request_id ?? null,
        },
      },
    );
  }
  if (result.status === 'applied') {
    if (
      pending.expected_revision >= Number.MAX_SAFE_INTEGER ||
      result.revision !== pending.expected_revision + 1
    ) {
      throw new SimulationApiError(
        'SIMULATION.SESSION_INVARIANT_VIOLATION',
        'An applied tuning result must advance the requested revision exactly once.',
      );
    }
    const movement = snapshot.data.match.movement;
    if (
      movement.revision === result.revision &&
      (movement.effective_tick !== result.decision_tick ||
        movement.current.acceleration_world_units_per_second_squared !==
          pending.acceleration_world_units_per_second_squared ||
        movement.current.normal_top_speed_world_units_per_second !==
          pending.normal_top_speed_world_units_per_second)
    ) {
      throw new SimulationApiError(
        'SIMULATION.SESSION_INVARIANT_VIOLATION',
        'The latest applied tuning revision must publish the requested complete pair and effective tick.',
      );
    }
  }
  return result;
}

/**
 * Validates an outbound command against the same closed schema the server enforces. A command the
 * server would refuse closes the connection, so this client refuses to put one on the wire.
 * NPC declarations additionally require exact membership in the current welcome catalogue;
 * the default absent catalogue admits no NPC selection and never skips membership checks.
 */
export function validateSessionCommand(
  command: SessionCommand,
  npcCatalogue: SessionNpcCatalogue | null = null,
): void {
  if (!KNOWN_COMMAND_KINDS.has(command.kind)) {
    throw unknownKindError(
      'Client command names a kind protocol v3 does not register.',
      { command_kind: command.kind },
    );
  }

  // Validated through an `unknown` alias so the failure branch stays reachable: a type-guard call
  // on `command` itself would narrow the refusal path to `never`.
  const commandDocument: unknown = command;
  if (!validateCommandEnvelopeSchema(commandDocument)) {
    throw new SimulationApiError(
      'SIMULATION.COMMAND_REJECTED',
      'Client command does not match protocol v3.',
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
  if (
    command.kind === 'seat_npc' &&
    !npcCatalogueContains(
      npcCatalogue,
      command.payload.npc_kind,
      command.payload.profile_name,
    )
  ) {
    throw new SimulationApiError(
      'SIMULATION.COMMAND_REJECTED',
      'NPC declaration is not selectable in the current welcome catalogue.',
      {
        context: {
          npc_kind: command.payload.npc_kind,
          profile_name: command.payload.profile_name ?? null,
        },
      },
    );
  }
}

/**
 * Validates and freezes one lobby directory document, `GET /api/v3/lobbies`. Version first, then
 * the closed schema, then the two facts the schema cannot state: the response's `X-Request-ID`
 * echoes the envelope's, and the rooms are numbered `1..N` in order -- the directory that Step 13's
 * router serves is exactly that, and a client that trusted a disordered one would join the wrong
 * room by index.
 */
export function validateLobbyDirectoryMessage(
  document: unknown,
  responseRequestId: string | null,
): SessionLobbyDirectoryMessage {
  assertSupportedProtocolVersion(document);

  if (!validateLobbyDirectorySchema(document)) {
    throw new SimulationApiError(
      'SIMULATION.LOBBY_DIRECTORY_RESPONSE_INVALID',
      'Lobby directory does not match protocol v3.',
      {
        context: {
          validation_errors: formatValidationErrors(
            validateLobbyDirectorySchema.errors,
          ),
        },
      },
    );
  }

  if (responseRequestId !== document.meta.request_id) {
    throw new SimulationApiError(
      'SIMULATION.LOBBY_DIRECTORY_RESPONSE_INVALID',
      'X-Request-ID must exactly match lobby directory meta.request_id.',
      {
        context: {
          envelope_request_id: document.meta.request_id,
          response_request_id: responseRequestId,
        },
      },
    );
  }

  document.data.lobbies.forEach((listing, index) => {
    if (listing.lobby_id !== index + 1) {
      throw new SimulationApiError(
        'SIMULATION.LOBBY_DIRECTORY_RESPONSE_INVALID',
        'Lobby directory rooms must be numbered 1..N in order.',
        {
          context: { actual_lobby_id: listing.lobby_id, position: index + 1 },
        },
      );
    }
  });

  return deepFreeze(document);
}

/**
 * Validates and freezes the v3 failure envelope of a `/api/v3/` target, holding it to the same
 * registry discipline as v1's: the status, the code, and the retryable flag must agree with what
 * the protocol registers for that code, and the request id must be echoed.
 */
export function validateSessionHttpErrorResponse(
  document: unknown,
  httpStatus: number,
  responseRequestId: string | null,
): SessionHttpErrorResponse {
  if (!validateHttpErrorSchema(document)) {
    throw new SimulationApiError(
      'SIMULATION.HTTP_ERROR_RESPONSE_INVALID',
      'HTTP error response does not match protocol v3.',
      {
        context: {
          validation_errors: formatValidationErrors(
            validateHttpErrorSchema.errors,
          ),
        },
      },
    );
  }

  const registeredError = V3_HTTP_ERROR_REGISTRY[document.error.code];
  if (
    httpStatus !== registeredError.status ||
    document.error.retryable !== registeredError.retryable
  ) {
    throw new SimulationApiError(
      'SIMULATION.HTTP_ERROR_RESPONSE_INVALID',
      'HTTP status, protocol error code, and retryable flag do not match the protocol v3 registry.',
      {
        context: {
          actual_http_status: httpStatus,
          actual_retryable: document.error.retryable,
          expected_http_status: registeredError.status,
          expected_retryable: registeredError.retryable,
          protocol_error_code: document.error.code,
        },
      },
    );
  }

  if (responseRequestId !== document.meta.request_id) {
    throw new SimulationApiError(
      'SIMULATION.HTTP_ERROR_RESPONSE_INVALID',
      'X-Request-ID must exactly match error-envelope meta.request_id.',
      {
        context: {
          envelope_request_id: document.meta.request_id,
          response_request_id: responseRequestId,
        },
      },
    );
  }

  return deepFreeze(document);
}
