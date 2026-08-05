import { readdir, readFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import Ajv2020 from 'ajv/dist/2020.js';
import addFormats from 'ajv-formats';

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const schemaRoot = resolve(scriptDirectory, '../../docs/protocol/schema/v1');
const exampleRoot = resolve(schemaRoot, 'examples');

const exampleSchemas = new Map([
  ['configuration-response.json', 'configuration-response.schema.json'],
  ['error-response.json', 'error-response.schema.json'],
  ['liveness-response.json', 'liveness-response.schema.json'],
  ['readiness-response.json', 'readiness-response.schema.json'],
  ['snapshot-message.json', 'snapshot-message.schema.json'],
]);

const ajv = new Ajv2020({
  allErrors: true,
  strict: true,
  validateFormats: true,
});
addFormats(ajv);
ajv.addKeyword({ keyword: 'x-status', schemaType: 'string', valid: true });

const schemaFileNames = (await readdir(schemaRoot, { withFileTypes: true }))
  .filter(
    (directoryEntry) =>
      directoryEntry.isFile() && directoryEntry.name.endsWith('.schema.json'),
  )
  .map((directoryEntry) => directoryEntry.name)
  .sort();

if (schemaFileNames.length === 0) {
  throw new Error(
    `PROTOCOL.CONFORMANCE.SCHEMAS_MISSING: no schemas found in ${schemaRoot}`,
  );
}

const schemasByFileName = new Map();
for (const schemaFileName of schemaFileNames) {
  const schema = JSON.parse(
    await readFile(resolve(schemaRoot, schemaFileName), 'utf8'),
  );
  if (schema['x-status'] !== 'Accepted') {
    throw new Error(
      `PROTOCOL.CONFORMANCE.SCHEMA_NOT_ACCEPTED: ${schemaFileName}`,
    );
  }
  schemasByFileName.set(schemaFileName, schema);
  ajv.addSchema(schema);
}

let validatedExampleCount = 0;
for (const [exampleFileName, schemaFileName] of exampleSchemas) {
  const schema = schemasByFileName.get(schemaFileName);
  if (schema === undefined || typeof schema.$id !== 'string') {
    throw new Error(
      `PROTOCOL.CONFORMANCE.ROOT_SCHEMA_MISSING: ${schemaFileName}`,
    );
  }

  const validate = ajv.getSchema(schema.$id);
  if (validate === undefined) {
    throw new Error(
      `PROTOCOL.CONFORMANCE.VALIDATOR_MISSING: ${schemaFileName}`,
    );
  }

  const example = JSON.parse(
    await readFile(resolve(exampleRoot, exampleFileName), 'utf8'),
  );
  if (!validate(example)) {
    throw new Error(
      `PROTOCOL.CONFORMANCE.EXAMPLE_INVALID: ${exampleFileName}: ${ajv.errorsText(
        validate.errors,
        { separator: '; ' },
      )}`,
    );
  }
  ++validatedExampleCount;
}

if (validatedExampleCount !== exampleSchemas.size) {
  throw new Error(
    `PROTOCOL.CONFORMANCE.EXAMPLE_COUNT_INVALID: expected=${exampleSchemas.size} actual=${validatedExampleCount}`,
  );
}

console.log(
  JSON.stringify({
    status: 'passed',
    verification: 'protocol-v1-schema-examples',
    schema_count: schemaFileNames.length,
    example_count: validatedExampleCount,
  }),
);
