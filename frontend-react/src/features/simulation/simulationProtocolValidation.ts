import type { ValidateFunction } from 'ajv/dist/2020.js';

import { SimulationApiError } from './SimulationApiError';
import { protocolV1Schemas } from './generated/protocolV1Schemas.generated';
import {
  createProtocolAjv,
  deepFreeze,
  formatValidationErrors,
} from './protocolValidationSupport';
import type {
  SimulationConfiguration,
  SimulationConfigurationResponse,
  SimulationHttpErrorResponse,
} from './simulationProtocolTypes';

const CONFIGURATION_SCHEMA_ID =
  'https://schemas.blob-royale.invalid/protocol/v1/configuration-response.schema.json';
const ERROR_RESPONSE_SCHEMA_ID =
  'https://schemas.blob-royale.invalid/protocol/v1/error-response.schema.json';

export const HTTP_ERROR_REGISTRY = Object.freeze({
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

const ajv = createProtocolAjv();
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
