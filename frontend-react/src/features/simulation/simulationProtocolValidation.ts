import Ajv2020, {
  type ErrorObject,
  type ValidateFunction,
} from 'ajv/dist/2020.js';
import addFormats from 'ajv-formats';

import { SimulationApiError } from './SimulationApiError';
import { protocolV1Schemas } from './generated/protocolV1Schemas.generated';
import type {
  SimulationConfiguration,
  SimulationConfigurationResponse,
  SimulationHttpErrorResponse,
  SimulationSnapshotMessage,
  SimulationVector2,
} from './simulationProtocolTypes';

const CONFIGURATION_SCHEMA_ID =
  'https://schemas.blob-royale.invalid/protocol/v1/configuration-response.schema.json';
const ERROR_RESPONSE_SCHEMA_ID =
  'https://schemas.blob-royale.invalid/protocol/v1/error-response.schema.json';
const SNAPSHOT_MESSAGE_SCHEMA_ID =
  'https://schemas.blob-royale.invalid/protocol/v1/snapshot-message.schema.json';

const HTTP_ERROR_REGISTRY = Object.freeze({
  'PROTOCOL.CONNECTION_LIMIT_REACHED': { retryable: true, status: 429 },
  'PROTOCOL.HEADER_TOO_LARGE': { retryable: false, status: 431 },
  'PROTOCOL.INVALID_REQUEST': { retryable: false, status: 400 },
  'PROTOCOL.INVALID_REQUEST_ID': { retryable: false, status: 400 },
  'PROTOCOL.METHOD_NOT_ALLOWED': { retryable: false, status: 405 },
  'PROTOCOL.ORIGIN_REJECTED': { retryable: false, status: 403 },
  'PROTOCOL.PAYLOAD_TOO_LARGE': { retryable: false, status: 413 },
  'PROTOCOL.RATE_LIMITED': { retryable: true, status: 429 },
  'PROTOCOL.ROUTE_NOT_FOUND': { retryable: false, status: 404 },
  'PROTOCOL.SUBPROTOCOL_REQUIRED': { retryable: false, status: 400 },
  'PROTOCOL.UPGRADE_REQUIRED': { retryable: false, status: 426 },
  'PROTOCOL.WEBSOCKET_VERSION_UNSUPPORTED': {
    retryable: false,
    status: 400,
  },
  'SERVICE.INTERNAL_FAILURE': { retryable: false, status: 500 },
  'SERVICE.NOT_READY': { retryable: true, status: 503 },
} as const satisfies Record<
  SimulationHttpErrorResponse['error']['code'],
  { readonly retryable: boolean; readonly status: number }
>);

export interface SnapshotSequenceState {
  readonly messageSequence: number;
  readonly requestId: string;
  readonly tickSequence: number;
}

const ajv = new Ajv2020({
  allErrors: true,
  coerceTypes: false,
  removeAdditional: false,
  strict: true,
  validateFormats: true,
});
addFormats(ajv);
ajv.addKeyword({ keyword: 'x-status', schemaType: 'string', valid: true });

for (const schema of Object.values(protocolV1Schemas)) {
  ajv.addSchema(schema);
}

function requireValidator<T>(schemaId: string): ValidateFunction<T> {
  const validator = ajv.getSchema<T>(schemaId);
  if (validator === undefined) {
    throw new SimulationApiError(
      'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
      `Required protocol validator was not registered: ${schemaId}`,
      { context: { schema_id: schemaId } },
    );
  }
  return validator;
}

const validateConfigurationSchema =
  requireValidator<SimulationConfigurationResponse>(CONFIGURATION_SCHEMA_ID);
const validateHttpErrorSchema = requireValidator<SimulationHttpErrorResponse>(
  ERROR_RESPONSE_SCHEMA_ID,
);
const validateSnapshotSchema = requireValidator<SimulationSnapshotMessage>(
  SNAPSHOT_MESSAGE_SCHEMA_ID,
);

function formatValidationErrors(
  validationErrors: ErrorObject[] | null | undefined,
): string {
  if (validationErrors === null || validationErrors === undefined) {
    return 'schema validation failed without error details';
  }

  return validationErrors
    .map(
      (validationError) =>
        `${validationError.instancePath || '/'} ${validationError.message ?? 'is invalid'}`,
    )
    .join('; ')
    .slice(0, 1_024);
}

function deepFreeze<T>(value: T): T {
  if (value === null || typeof value !== 'object' || Object.isFrozen(value)) {
    return value;
  }

  for (const nestedValue of Object.values(value)) {
    deepFreeze(nestedValue);
  }

  return Object.freeze(value);
}

function validateConfigurationSemantics(
  configuration: SimulationConfiguration,
): void {
  const { height_world_units, player_radius_world_units, width_world_units } =
    configuration.world;

  if (
    width_world_units <= 2 * player_radius_world_units ||
    height_world_units <= 2 * player_radius_world_units
  ) {
    throw new SimulationApiError(
      'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
      'World dimensions must be greater than twice the player radius.',
      {
        context: {
          height_world_units,
          player_radius_world_units,
          width_world_units,
        },
      },
    );
  }
}

function validateVectorSignedZero(
  vector: SimulationVector2,
  fieldName: string,
  entityId: number,
): void {
  if (Object.is(vector.x, -0) || Object.is(vector.y, -0)) {
    throw new SimulationApiError(
      'SIMULATION.SNAPSHOT_INVARIANT_VIOLATION',
      `Snapshot ${fieldName} contains forbidden negative zero.`,
      { context: { entity_id: entityId, field_name: fieldName } },
    );
  }
}

/** Validates and freezes an untrusted configuration response document. */
export function validateSimulationConfigurationResponse(
  document: unknown,
  responseRequestId: string | null,
): SimulationConfigurationResponse {
  if (!validateConfigurationSchema(document)) {
    throw new SimulationApiError(
      'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
      'Configuration response does not match protocol v1.',
      {
        context: {
          validation_errors: formatValidationErrors(
            validateConfigurationSchema.errors,
          ),
        },
      },
    );
  }

  validateConfigurationSemantics(document.data);
  if (responseRequestId !== document.meta.request_id) {
    throw new SimulationApiError(
      'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
      'X-Request-ID must exactly match configuration-envelope meta.request_id.',
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

/** Validates and freezes an untrusted HTTP error response document. */
export function validateSimulationHttpErrorResponse(
  document: unknown,
  httpStatus: number,
  responseRequestId: string | null,
): SimulationHttpErrorResponse {
  if (!validateHttpErrorSchema(document)) {
    throw new SimulationApiError(
      'SIMULATION.HTTP_ERROR_RESPONSE_INVALID',
      'HTTP error response does not match protocol v1.',
      {
        context: {
          validation_errors: formatValidationErrors(
            validateHttpErrorSchema.errors,
          ),
        },
      },
    );
  }

  const registeredError = HTTP_ERROR_REGISTRY[document.error.code];
  if (
    httpStatus !== registeredError.status ||
    document.error.retryable !== registeredError.retryable
  ) {
    throw new SimulationApiError(
      'SIMULATION.HTTP_ERROR_RESPONSE_INVALID',
      'HTTP status, protocol error code, and retryable flag do not match the protocol v1 registry.',
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

/** Validates schema and cross-message/world invariants before rendering. */
export function validateSimulationSnapshotMessage(
  document: unknown,
  configuration: SimulationConfiguration,
  previousSequence: SnapshotSequenceState | null,
): SimulationSnapshotMessage {
  if (!validateSnapshotSchema(document)) {
    throw new SimulationApiError(
      'SIMULATION.SNAPSHOT_FRAME_INVALID',
      'Snapshot frame does not match protocol v1.',
      {
        context: {
          validation_errors: formatValidationErrors(
            validateSnapshotSchema.errors,
          ),
        },
      },
    );
  }

  const expectedMessageSequence =
    previousSequence === null ? 1 : previousSequence.messageSequence + 1;
  if (document.meta.message_sequence !== expectedMessageSequence) {
    throw new SimulationApiError(
      'SIMULATION.SNAPSHOT_INVARIANT_VIOLATION',
      'Snapshot message_sequence must start at one and increment exactly once.',
      {
        context: {
          actual_message_sequence: document.meta.message_sequence,
          expected_message_sequence: expectedMessageSequence,
        },
      },
    );
  }

  if (
    previousSequence !== null &&
    document.meta.request_id !== previousSequence.requestId
  ) {
    throw new SimulationApiError(
      'SIMULATION.SNAPSHOT_INVARIANT_VIOLATION',
      'Snapshot request_id changed within one WebSocket connection.',
      {
        context: {
          actual_request_id: document.meta.request_id,
          expected_request_id: previousSequence.requestId,
        },
      },
    );
  }

  if (
    previousSequence !== null &&
    document.data.tick_sequence <= previousSequence.tickSequence
  ) {
    throw new SimulationApiError(
      'SIMULATION.SNAPSHOT_INVARIANT_VIOLATION',
      'Snapshot tick_sequence must strictly increase.',
      {
        context: {
          actual_tick_sequence: document.data.tick_sequence,
          previous_tick_sequence: previousSequence.tickSequence,
        },
      },
    );
  }

  const { height_world_units, player_radius_world_units, width_world_units } =
    configuration.world;
  let previousEntityId = 0;
  for (const player of document.data.players) {
    if (player.entity_id <= previousEntityId) {
      throw new SimulationApiError(
        'SIMULATION.SNAPSHOT_INVARIANT_VIOLATION',
        'Snapshot player entity_id values must be unique and strictly ascending.',
        {
          context: {
            entity_id: player.entity_id,
            previous_entity_id: previousEntityId,
          },
        },
      );
    }
    previousEntityId = player.entity_id;

    validateVectorSignedZero(player.position, 'position', player.entity_id);
    validateVectorSignedZero(player.velocity, 'velocity', player.entity_id);
    validateVectorSignedZero(
      player.acceleration,
      'acceleration',
      player.entity_id,
    );

    if (
      player.position.x < player_radius_world_units ||
      player.position.x > width_world_units - player_radius_world_units ||
      player.position.y < player_radius_world_units ||
      player.position.y > height_world_units - player_radius_world_units
    ) {
      throw new SimulationApiError(
        'SIMULATION.SNAPSHOT_INVARIANT_VIOLATION',
        'Snapshot player position lies outside the configured world bounds.',
        {
          context: {
            entity_id: player.entity_id,
            position_x: player.position.x,
            position_y: player.position.y,
          },
        },
      );
    }
  }

  return deepFreeze(document);
}
