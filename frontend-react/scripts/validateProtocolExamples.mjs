// canonical: protocol_example_validation -- the only validator for checked-in protocol examples.
//
// What it does: validates every golden example under docs/protocol/schema/<version>/examples
//   against its mapped schema, offline, including explicitly historical versions.
// Inputs: none. The schema and example trees are discovered from the repository layout below.
// Side effects: none beyond stdout. Idempotent.
// Failure modes, each an explicit PROTOCOL.CONFORMANCE.* error rather than a silent skip:
//   SCHEMAS_MISSING          a version directory contains no *.schema.json
//   SCHEMA_STATUS_MISMATCH   a schema disagrees with its version's explicit lifecycle status
//   SCHEMA_ID_MISMATCH       a schema's $id does not live under its own version namespace
//   SCHEMA_UNCOMPILABLE      a schema (or a $ref it reaches) does not resolve locally
//   ROOT_SCHEMA_MISSING      an example maps to a schema file that does not exist
//   EXAMPLE_UNMAPPED         an example file exists with no schema mapping
//   EXAMPLE_MISSING          a mapping names an example file that does not exist
//   EXAMPLE_INVALID          an example does not validate against its mapped schema
//   EXAMPLE_COUNT_INVALID    the validated count disagrees with the mapping size
//
// related: docs/protocol/v1.md section "Golden examples", docs/protocol/v2.md section
// "Golden examples". Ajv resolves every $ref from the schemas added below; nothing is fetched.

import { readdir, readFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import Ajv2020 from 'ajv/dist/2020.js';
import addFormats from 'ajv-formats';

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const protocolSchemaRoot = resolve(
  scriptDirectory,
  '../../docs/protocol/schema',
);

// One entry per published protocol version. A new version adds one entry; the mapping is exhaustive
// by construction because an unmapped example file is a failure, not a skip.
const protocolVersions = [
  {
    version: 'v1',
    expectedStatus: 'Accepted',
    exampleSchemas: new Map([
      ['configuration-response.json', 'configuration-response.schema.json'],
      ['error-response.json', 'error-response.schema.json'],
      ['liveness-response.json', 'liveness-response.schema.json'],
      ['readiness-response.json', 'readiness-response.schema.json'],
      ['snapshot-message.json', 'snapshot-message.schema.json'],
    ]),
  },
  {
    version: 'v2',
    expectedStatus: 'Historical',
    exampleSchemas: new Map([
      ['command-envelope.json', 'command-envelope.schema.json'],
      ['error-response.json', 'error-response.schema.json'],
      ['lobby-directory-message.json', 'lobby-directory-message.schema.json'],
      ['race-progress-component.json', 'race-progress-component.schema.json'],
      ['race-mode-state.json', 'race-mode-state.schema.json'],
      ['snapshot-message.json', 'snapshot-message.schema.json'],
      ['welcome-message.json', 'welcome-message.schema.json'],
    ]),
  },
  {
    version: 'v3',
    expectedStatus: 'Accepted',
    exampleSchemas: new Map([
      ['hill-motion-component.json', 'hill-motion-component.schema.json'],
      [
        'set-movement-tuning-command.json',
        'set-movement-tuning-command.schema.json',
      ],
      ['tuning-result.json', 'tuning-result.schema.json'],
      ['command-envelope.json', 'command-envelope.schema.json'],
      ['error-response.json', 'error-response.schema.json'],
      ['lobby-directory-message.json', 'lobby-directory-message.schema.json'],
      ['race-progress-component.json', 'race-progress-component.schema.json'],
      ['race-mode-state.json', 'race-mode-state.schema.json'],
      ['snapshot-message.json', 'snapshot-message.schema.json'],
      ['welcome-message.json', 'welcome-message.schema.json'],
    ]),
  },
];

const ajv = new Ajv2020({
  allErrors: true,
  strict: true,
  validateFormats: true,
});
addFormats(ajv);
ajv.addKeyword({ keyword: 'x-status', schemaType: 'string', valid: true });

/**
 * Loads and registers every schema of one protocol version.
 *
 * @param {string} version Version directory name, for example `v1`.
 * @param {string} expectedStatus The explicit lifecycle status of this version.
 * @returns {Promise<Map<string, object>>} Schema documents keyed by file name.
 * @throws {Error} PROTOCOL.CONFORMANCE.SCHEMAS_MISSING, .SCHEMA_STATUS_MISMATCH, .SCHEMA_ID_MISMATCH
 */
async function loadVersionSchemas(version, expectedStatus) {
  const schemaRoot = resolve(protocolSchemaRoot, version);
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

  const expectedIdPrefix = `https://schemas.blob-royale.invalid/protocol/${version}/`;
  const schemasByFileName = new Map();
  for (const schemaFileName of schemaFileNames) {
    const schema = JSON.parse(
      await readFile(resolve(schemaRoot, schemaFileName), 'utf8'),
    );
    if (schema['x-status'] !== expectedStatus) {
      throw new Error(
        `PROTOCOL.CONFORMANCE.SCHEMA_STATUS_MISMATCH: ${version}/${schemaFileName}; expected ${expectedStatus}`,
      );
    }
    if (schema.$id !== `${expectedIdPrefix}${schemaFileName}`) {
      throw new Error(
        `PROTOCOL.CONFORMANCE.SCHEMA_ID_MISMATCH: ${version}/${schemaFileName}: ` +
          `expected=${expectedIdPrefix}${schemaFileName} actual=${String(schema.$id)}`,
      );
    }
    schemasByFileName.set(schemaFileName, schema);
    ajv.addSchema(schema);
  }

  return schemasByFileName;
}

/**
 * Compiles every schema of one version so a dangling or non-local $ref fails even when no example
 * happens to reach it.
 *
 * @param {string} version Version directory name.
 * @param {Map<string, object>} schemasByFileName Loaded schema documents.
 * @throws {Error} PROTOCOL.CONFORMANCE.SCHEMA_UNCOMPILABLE
 */
function compileVersionSchemas(version, schemasByFileName) {
  for (const [schemaFileName, schema] of schemasByFileName) {
    try {
      ajv.getSchema(schema.$id);
    } catch (cause) {
      throw new Error(
        `PROTOCOL.CONFORMANCE.SCHEMA_UNCOMPILABLE: ${version}/${schemaFileName}: ${
          cause instanceof Error ? cause.message : String(cause)
        }`,
        { cause },
      );
    }
  }
}

/**
 * Validates every example of one protocol version against its mapped schema.
 *
 * @param {string} version Version directory name.
 * @param {Map<string, string>} exampleSchemas Example file name to schema file name.
 * @param {Map<string, object>} schemasByFileName Loaded schema documents.
 * @returns {Promise<number>} Count of validated examples.
 * @throws {Error} PROTOCOL.CONFORMANCE.EXAMPLE_UNMAPPED, .EXAMPLE_MISSING, .ROOT_SCHEMA_MISSING,
 *   .VALIDATOR_MISSING, .EXAMPLE_INVALID, .EXAMPLE_COUNT_INVALID
 */
async function validateVersionExamples(
  version,
  exampleSchemas,
  schemasByFileName,
) {
  const exampleRoot = resolve(protocolSchemaRoot, version, 'examples');
  const presentExampleFileNames = (
    await readdir(exampleRoot, { withFileTypes: true })
  )
    .filter(
      (directoryEntry) =>
        directoryEntry.isFile() && directoryEntry.name.endsWith('.json'),
    )
    .map((directoryEntry) => directoryEntry.name)
    .sort();

  for (const exampleFileName of presentExampleFileNames) {
    if (!exampleSchemas.has(exampleFileName)) {
      throw new Error(
        `PROTOCOL.CONFORMANCE.EXAMPLE_UNMAPPED: ${version}/examples/${exampleFileName}`,
      );
    }
  }
  for (const exampleFileName of exampleSchemas.keys()) {
    if (!presentExampleFileNames.includes(exampleFileName)) {
      throw new Error(
        `PROTOCOL.CONFORMANCE.EXAMPLE_MISSING: ${version}/examples/${exampleFileName}`,
      );
    }
  }

  let validatedExampleCount = 0;
  for (const [exampleFileName, schemaFileName] of exampleSchemas) {
    const schema = schemasByFileName.get(schemaFileName);
    if (schema === undefined || typeof schema.$id !== 'string') {
      throw new Error(
        `PROTOCOL.CONFORMANCE.ROOT_SCHEMA_MISSING: ${version}/${schemaFileName}`,
      );
    }

    const validate = ajv.getSchema(schema.$id);
    if (validate === undefined) {
      throw new Error(
        `PROTOCOL.CONFORMANCE.VALIDATOR_MISSING: ${version}/${schemaFileName}`,
      );
    }

    const example = JSON.parse(
      await readFile(resolve(exampleRoot, exampleFileName), 'utf8'),
    );
    if (!validate(example)) {
      throw new Error(
        `PROTOCOL.CONFORMANCE.EXAMPLE_INVALID: ${version}/examples/${exampleFileName}: ${ajv.errorsText(
          validate.errors,
          { separator: '; ' },
        )}`,
      );
    }
    ++validatedExampleCount;
  }

  if (validatedExampleCount !== exampleSchemas.size) {
    throw new Error(
      `PROTOCOL.CONFORMANCE.EXAMPLE_COUNT_INVALID: version=${version} ` +
        `expected=${exampleSchemas.size} actual=${validatedExampleCount}`,
    );
  }

  return validatedExampleCount;
}

const loadedSchemas = new Map();
for (const { version, expectedStatus } of protocolVersions) {
  loadedSchemas.set(version, await loadVersionSchemas(version, expectedStatus));
}

let totalSchemaCount = 0;
let totalExampleCount = 0;
const versionSummaries = {};
for (const { version, exampleSchemas } of protocolVersions) {
  const schemasByFileName = loadedSchemas.get(version);
  compileVersionSchemas(version, schemasByFileName);
  const exampleCount = await validateVersionExamples(
    version,
    exampleSchemas,
    schemasByFileName,
  );
  versionSummaries[version] = {
    schema_count: schemasByFileName.size,
    example_count: exampleCount,
  };
  totalSchemaCount += schemasByFileName.size;
  totalExampleCount += exampleCount;
}

console.log(
  JSON.stringify({
    status: 'passed',
    verification: 'protocol-schema-examples',
    protocol_versions: protocolVersions.map(({ version }) => version),
    schema_count: totalSchemaCount,
    example_count: totalExampleCount,
    by_version: versionSummaries,
  }),
);
