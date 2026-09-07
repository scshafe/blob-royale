import Ajv2020, { type ErrorObject } from 'ajv/dist/2020.js';
import addFormats from 'ajv-formats';

/**
 * Builds the one Ajv configuration every protocol version decodes with: strict Draft 2020-12, no
 * coercion, no property removal, and `x-status` accepted as documentation rather than a keyword.
 * Coercion or removal here would silently repair a frame the server must be held to.
 */
export function createProtocolAjv(): Ajv2020 {
  const ajv = new Ajv2020({
    allErrors: true,
    coerceTypes: false,
    removeAdditional: false,
    strict: true,
    validateFormats: true,
  });
  addFormats(ajv);
  ajv.addKeyword({ keyword: 'x-status', schemaType: 'string', valid: true });
  return ajv;
}

/** Renders Ajv errors as one bounded diagnostic string safe to attach to an error context. */
export function formatValidationErrors(
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

/** Freezes a validated document so no component can mutate wire state after the fact. */
export function deepFreeze<T>(value: T): T {
  if (value === null || typeof value !== 'object' || Object.isFrozen(value)) {
    return value;
  }

  for (const nestedValue of Object.values(value)) {
    deepFreeze(nestedValue);
  }

  return Object.freeze(value);
}
