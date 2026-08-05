import { describe, expect, it } from 'vitest';

import { SimulationApiError } from './SimulationApiError';
import {
  configurationResponseExample,
  errorResponseExample,
  snapshotMessageExample,
} from './fixtures/protocolV1Examples';
import {
  validateSimulationConfigurationResponse,
  validateSimulationHttpErrorResponse,
  validateSimulationSnapshotMessage,
} from './simulationProtocolValidation';

function validSnapshotDocument() {
  const snapshotDocument = structuredClone(snapshotMessageExample);
  snapshotDocument.meta.message_sequence = 1;
  return snapshotDocument;
}

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

describe('validateSimulationSnapshotMessage', () => {
  const configuration = validateSimulationConfigurationResponse(
    structuredClone(configurationResponseExample),
    configurationResponseExample.meta.request_id,
  ).data;

  it('accepts the first complete ordered snapshot and freezes it deeply', () => {
    const snapshot = validateSimulationSnapshotMessage(
      validSnapshotDocument(),
      configuration,
      null,
    );

    expect(Object.isFrozen(snapshot.data.players)).toBe(true);
    expect(Object.isFrozen(snapshot.data.players[0]?.position)).toBe(true);
  });

  it('rejects player identifiers that are not strictly ascending', () => {
    const snapshotDocument = validSnapshotDocument();
    snapshotDocument.data.players.reverse();

    expect(() =>
      validateSimulationSnapshotMessage(snapshotDocument, configuration, null),
    ).toThrow(/strictly ascending/);
  });

  it('rejects positions outside the configured radius-adjusted world', () => {
    const snapshotDocument = validSnapshotDocument();
    const firstPlayer = snapshotDocument.data.players[0];
    if (firstPlayer === undefined) {
      throw new Error('TEST.SNAPSHOT_FIXTURE_EMPTY');
    }
    firstPlayer.position.x = 5;

    expect(() =>
      validateSimulationSnapshotMessage(snapshotDocument, configuration, null),
    ).toThrow(/outside the configured world bounds/);
  });

  it('rejects negative zero that JSON Schema cannot distinguish', () => {
    const snapshotDocument = validSnapshotDocument();
    const firstPlayer = snapshotDocument.data.players[0];
    if (firstPlayer === undefined) {
      throw new Error('TEST.SNAPSHOT_FIXTURE_EMPTY');
    }
    firstPlayer.velocity.x = -0;

    expect(() =>
      validateSimulationSnapshotMessage(snapshotDocument, configuration, null),
    ).toThrow(/negative zero/);
  });

  it('rejects a non-monotonic tick on the same connection', () => {
    const snapshotDocument = validSnapshotDocument();
    snapshotDocument.meta.message_sequence = 2;

    expect(() =>
      validateSimulationSnapshotMessage(snapshotDocument, configuration, {
        messageSequence: 1,
        requestId: snapshotDocument.meta.request_id,
        tickSequence: snapshotDocument.data.tick_sequence,
      }),
    ).toThrow(/strictly increase/);
  });

  it('rejects a skipped transport message sequence', () => {
    const snapshotDocument = validSnapshotDocument();
    snapshotDocument.meta.message_sequence = 3;
    snapshotDocument.data.tick_sequence += 1;

    expect(() =>
      validateSimulationSnapshotMessage(snapshotDocument, configuration, {
        messageSequence: 1,
        requestId: snapshotDocument.meta.request_id,
        tickSequence: snapshotDocument.data.tick_sequence - 1,
      }),
    ).toThrow(/increment exactly once/);
  });

  it('rejects a request identifier change within one connection', () => {
    const snapshotDocument = validSnapshotDocument();
    snapshotDocument.meta.message_sequence = 2;
    snapshotDocument.data.tick_sequence += 1;

    expect(() =>
      validateSimulationSnapshotMessage(snapshotDocument, configuration, {
        messageSequence: 1,
        requestId: 'different-request-id',
        tickSequence: snapshotDocument.data.tick_sequence - 1,
      }),
    ).toThrow(/request_id changed/);
  });
});
