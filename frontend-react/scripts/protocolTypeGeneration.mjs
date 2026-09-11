// canonical: protocol_type_generation -- the only generator of the browser's protocol boundary.
//
// What it does: turns one accepted protocol version's canonical local schemas into the browser's
//   immutable TypeScript types and its bundled validation schemas. With --check it writes nothing
//   and fails on drift; without arguments it deterministically rewrites both generated files. The
//   operation is idempotent.
// Inputs: one version descriptor, supplied by the version's entry-point script.
// Side effects: writes (or, with --check, only reads) the two generated files of that version, and
//   one JSON line on stdout.
// Failure modes, each an explicit WEB.PROTOCOL_* error rather than a silent skip:
//   WEB.PROTOCOL_TYPE_GENERATOR_ARGUMENT_INVALID  an argument other than a single --check
//   WEB.PROTOCOL_SCHEMAS_MISSING                  a version directory contains no *.schema.json
//   WEB.PROTOCOL_TYPE_GENERATION_EMPTY            the schema compiler emitted no declarations
//   WEB.PROTOCOL_GENERATION_DRIFT                 a generated file is missing or out of date
//
// **One implementation, one location, two versions.** Protocol v3 needed the identical pipeline
// over a different schema tree, and a second copy of it would have been the drift the --check mode
// exists to prevent, one level up. Everything a version differs in is a field of the descriptor
// below; nothing here branches on which version is running.
//
// **A version's generated header names its own entry-point script**, so the provenance comment in a
// generated file still points at the command that produced it and the v1 outputs stay byte-for-byte
// what they were before v2 existed.
// related: scripts/generateProtocolV1Types.mjs -- the v1 entry point.
// related: scripts/generateProtocolV3Types.mjs -- the v3 entry point.
// related: scripts/validateProtocolExamples.mjs -- the sibling that validates the golden examples.

import { mkdir, readFile, readdir, writeFile } from 'node:fs/promises';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

import { compile } from 'json-schema-to-typescript';
import { format as formatWithPrettier } from 'prettier';

const scriptDirectory = dirname(fileURLToPath(import.meta.url));
const webRoot = resolve(scriptDirectory, '..');
const generatedRoot = resolve(webRoot, 'src/features/simulation/generated');

/**
 * @typedef {object} ProtocolVersionDescriptor
 * @property {string} version Version directory name under docs/protocol/schema, e.g. `v1`.
 * @property {string} generatorScriptName Entry-point script name recorded in the generated header.
 * @property {string} typesFileName Generated types file name under the generated directory.
 * @property {string} schemasFileName Generated schemas file name under the generated directory.
 * @property {string} aggregateTypeName Root name the schema compiler is asked to emit.
 * @property {string} schemasExportName Name of the bundled schema constant.
 * @property {readonly string[]} rootSchemaFileNames The version's top-level message schemas.
 * @property {string} [typesPrelude] Text inserted after the header of the types file only.
 */

/**
 * Parses the entry point's arguments.
 *
 * @param {readonly string[]} argumentsToParse Arguments after the script path.
 * @returns {{ checkOnly: boolean }} Whether to verify rather than write.
 * @throws {Error} WEB.PROTOCOL_TYPE_GENERATOR_ARGUMENT_INVALID
 */
export function parseArguments(argumentsToParse) {
  if (argumentsToParse.length === 0) {
    return Object.freeze({ checkOnly: false });
  }

  if (argumentsToParse.length === 1 && argumentsToParse[0] === '--check') {
    return Object.freeze({ checkOnly: true });
  }

  throw new Error(
    `WEB.PROTOCOL_TYPE_GENERATOR_ARGUMENT_INVALID: expected no arguments or --check, received ${argumentsToParse.join(' ')}`,
  );
}

/**
 * Reads every canonical schema of one version, in sorted file-name order.
 *
 * @param {string} schemaRoot Absolute path of the version's schema directory.
 * @returns {Promise<readonly { schemaFileName: string, schema: object }[]>} Schemas in file order.
 * @throws {Error} WEB.PROTOCOL_SCHEMAS_MISSING
 */
async function readCanonicalSchemas(schemaRoot) {
  const schemaFileNames = (await readdir(schemaRoot, { withFileTypes: true }))
    .filter(
      (directoryEntry) =>
        directoryEntry.isFile() && directoryEntry.name.endsWith('.schema.json'),
    )
    .map((directoryEntry) => directoryEntry.name)
    .sort();
  if (schemaFileNames.length === 0) {
    throw new Error(
      `WEB.PROTOCOL_SCHEMAS_MISSING: no canonical schemas found in ${schemaRoot}`,
    );
  }

  return Promise.all(
    schemaFileNames.map(async (schemaFileName) => {
      const schemaText = await readFile(
        resolve(schemaRoot, schemaFileName),
        'utf8',
      );
      return Object.freeze({
        schemaFileName,
        schema: JSON.parse(schemaText),
      });
    }),
  );
}

/**
 * Compiles one version's root schemas into deeply immutable TypeScript declarations.
 *
 * @param {ProtocolVersionDescriptor} descriptor The version being generated.
 * @param {string} schemaRoot Absolute path of the version's schema directory.
 * @param {string} generatedHeader Provenance comment prepended to the file.
 * @returns {Promise<string>} The formatted file contents.
 * @throws {Error} WEB.PROTOCOL_TYPE_GENERATION_EMPTY
 */
async function generateTypes(descriptor, schemaRoot, generatedHeader) {
  const typesPrelude = descriptor.typesPrelude ?? '';
  const aggregateSchema = {
    $schema: 'https://json-schema.org/draft/2020-12/schema',
    title: descriptor.aggregateTypeName,
    type: 'object',
    additionalProperties: false,
    required: descriptor.rootSchemaFileNames.map((schemaFileName) =>
      schemaFileName.replace('.schema.json', '').replaceAll('-', '_'),
    ),
    properties: Object.fromEntries(
      descriptor.rootSchemaFileNames.map((schemaFileName) => [
        schemaFileName.replace('.schema.json', '').replaceAll('-', '_'),
        { $ref: schemaFileName },
      ]),
    ),
  };

  // json-schema-to-typescript 15.0.4 exposes no immutableTypes option. Keep
  // immutability as a deterministic post-generation transform until the exact
  // pinned generator publishes that supported option.
  const mutableTypes = await compile(
    aggregateSchema,
    descriptor.aggregateTypeName,
    {
      additionalProperties: false,
      bannerComment: '',
      cwd: schemaRoot,
      declareExternallyReferenced: true,
      enableConstEnums: false,
      ignoreMinAndMaxItems: false,
      style: {
        bracketSpacing: true,
        printWidth: 80,
        semi: true,
        singleQuote: true,
        tabWidth: 2,
        trailingComma: 'all',
        useTabs: false,
      },
      strictIndexSignatures: true,
      unreachableDefinitions: true,
      unknownAny: true,
    },
  );

  const declarationNames = Array.from(
    mutableTypes.matchAll(/^export (?:interface|type|enum) ([A-Za-z0-9_]+)/gm),
    (match) => match[1],
  );

  if (declarationNames.length === 0) {
    throw new Error(
      'WEB.PROTOCOL_TYPE_GENERATION_EMPTY: schema compiler emitted no declarations',
    );
  }

  const mutableDeclarations = mutableTypes.replace(
    /^export (interface|type|enum) /gm,
    '$1 Mutable',
  );
  const renamedReferences = declarationNames.reduce(
    (source, declarationName) =>
      source.replaceAll(
        new RegExp(`\\b${declarationName}\\b`, 'g'),
        `Mutable${declarationName}`,
      ),
    mutableDeclarations,
  );
  const readonlyAliases = declarationNames
    .map(
      (declarationName) =>
        `export type ${declarationName} = DeepReadonly<Mutable${declarationName}>;`,
    )
    .join('\n');

  return formatWithPrettier(
    `${generatedHeader}${typesPrelude}type DeepReadonly<T> = T extends (...arguments_: readonly unknown[]) => unknown
  ? T
  : T extends readonly (infer Item)[]
    ? readonly DeepReadonly<Item>[]
    : T extends object
      ? { readonly [Key in keyof T]: DeepReadonly<T[Key]> }
      : T;

${renamedReferences.trim()}

${readonlyAliases}
`,
    {
      parser: 'typescript',
      singleQuote: true,
      trailingComma: 'all',
    },
  );
}

/**
 * Bundles one version's schemas as a frozen constant the browser validator compiles offline.
 *
 * @param {ProtocolVersionDescriptor} descriptor The version being generated.
 * @param {readonly { schemaFileName: string, schema: object }[]} canonicalSchemas Loaded schemas.
 * @param {string} generatedHeader Provenance comment prepended to the file.
 * @returns {Promise<string>} The formatted file contents.
 */
async function generateSchemas(descriptor, canonicalSchemas, generatedHeader) {
  const schemaEntries = canonicalSchemas
    .map(({ schemaFileName, schema }) => {
      const exportName = schemaFileName
        .replace('.schema.json', '')
        .split('-')
        .map((segment, index) =>
          index === 0
            ? segment
            : `${segment.slice(0, 1).toUpperCase()}${segment.slice(1)}`,
        )
        .join('');
      return `  ${JSON.stringify(exportName)}: ${JSON.stringify(schema, null, 2)},`;
    })
    .join('\n');

  return formatWithPrettier(
    `${generatedHeader}export const ${descriptor.schemasExportName} = {
${schemaEntries}
} as const;
`,
    {
      parser: 'typescript',
      singleQuote: true,
      trailingComma: 'all',
    },
  );
}

/**
 * Writes one generated file, or verifies it and fails on drift.
 *
 * @param {string} filePath Absolute path of the generated file.
 * @param {string} expectedContents Contents the generator produced.
 * @param {boolean} checkOnly Whether to verify rather than write.
 * @returns {Promise<void>} Resolves when the file is written or verified.
 * @throws {Error} WEB.PROTOCOL_GENERATION_DRIFT
 */
async function assertOrWriteFile(filePath, expectedContents, checkOnly) {
  if (!checkOnly) {
    await mkdir(dirname(filePath), { recursive: true });
    await writeFile(filePath, expectedContents, 'utf8');
    return;
  }

  let actualContents;
  try {
    actualContents = await readFile(filePath, 'utf8');
  } catch (error) {
    throw new Error(
      `WEB.PROTOCOL_GENERATION_DRIFT: generated file is missing: ${filePath}`,
      { cause: error },
    );
  }

  if (actualContents !== expectedContents) {
    throw new Error(
      `WEB.PROTOCOL_GENERATION_DRIFT: regenerate ${filePath} with npm run generate:protocol`,
    );
  }
}

/**
 * Generates or verifies one protocol version's browser boundary.
 *
 * @param {ProtocolVersionDescriptor} descriptor The version to generate.
 * @param {readonly string[]} commandArguments Arguments after the script path.
 * @returns {Promise<void>} Resolves after both files are written or verified.
 */
export async function generateProtocolVersion(descriptor, commandArguments) {
  const options = parseArguments(commandArguments);
  const schemaRoot = resolve(
    webRoot,
    `../docs/protocol/schema/${descriptor.version}`,
  );
  const generatedHeader = `// @generated by scripts/${descriptor.generatorScriptName}.
// Canonical source: ../../../../../docs/protocol/schema/${descriptor.version}/*.schema.json.
// Do not edit this file directly; run npm run generate:protocol.

`;

  const canonicalSchemas = await readCanonicalSchemas(schemaRoot);
  const generatedTypes = await generateTypes(
    descriptor,
    schemaRoot,
    generatedHeader,
  );
  const generatedSchemas = await generateSchemas(
    descriptor,
    canonicalSchemas,
    generatedHeader,
  );

  await assertOrWriteFile(
    resolve(generatedRoot, descriptor.typesFileName),
    generatedTypes,
    options.checkOnly,
  );
  await assertOrWriteFile(
    resolve(generatedRoot, descriptor.schemasFileName),
    generatedSchemas,
    options.checkOnly,
  );

  process.stdout.write(
    `${JSON.stringify({
      status: 'passed',
      operation: options.checkOnly
        ? 'protocol-generation-check'
        : 'protocol-generation',
      protocol_version: descriptor.version,
      schema_count: canonicalSchemas.length,
    })}\n`,
  );
}
