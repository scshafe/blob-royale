import { describe, expect, it } from 'vitest';

import { SimulationApiError } from './SimulationApiError';
import {
  configurationResponseExample,
  errorResponseExample,
} from './fixtures/protocolV1Examples';
import {
  validateSimulationConfigurationResponse,
  validateSimulationHttpErrorResponse,
} from './simulationProtocolValidation';

describe('validateSimulationConfigurationResponse', () => {
  it('returns a deeply frozen value derived from the canonical schema example', () => {
    const response = validateSimulationConfigurationResponse(
      structuredClone(configurationResponseExample),
      configurationResponseExample.meta.request_id,
    );

    expect(Object.isFrozen(response)).toBe(true);
    expect(Object.isFrozen(response.data.world)).toBe(true);
  });

  it('rejects a structurally unknown configuration property', () => {
    const response = structuredClone(configurationResponseExample);
    Object.assign(response.data.world, { implementation_detail: 4 });

    expect(() =>
      validateSimulationConfigurationResponse(
        response,
        response.meta.request_id,
      ),
    ).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.CONFIGURATION_RESPONSE_INVALID',
      }),
    );
  });

  it('rejects world dimensions that do not exceed twice the radius', () => {
    const response = structuredClone(configurationResponseExample);
    response.data.world.width_world_units = 20;

    expect(() =>
      validateSimulationConfigurationResponse(
        response,
        response.meta.request_id,
      ),
    ).toThrow(/greater than twice the player radius/);
  });
});

describe('validateSimulationHttpErrorResponse', () => {
  it('accepts and freezes a registered status, retryability, and request-ID tuple', () => {
    const errorResponse = structuredClone(errorResponseExample);
    const validatedError = validateSimulationHttpErrorResponse(
      errorResponse,
      405,
      errorResponse.meta.request_id,
    );

    expect(Object.isFrozen(validatedError)).toBe(true);
    expect(Object.isFrozen(validatedError.error.details)).toBe(true);
  });

  it('rejects a retryable flag that disagrees with the stable error registry', () => {
    const errorResponse = structuredClone(errorResponseExample);
    errorResponse.error.retryable = true;

    expect(() =>
      validateSimulationHttpErrorResponse(
        errorResponse,
        405,
        errorResponse.meta.request_id,
      ),
    ).toThrow(/do not match the protocol v1 registry/);
  });

  it('rejects a missing X-Request-ID header', () => {
    const errorResponse = structuredClone(errorResponseExample);

    expect(() =>
      validateSimulationHttpErrorResponse(errorResponse, 405, null),
    ).toThrow(/X-Request-ID must exactly match/);
  });
});
