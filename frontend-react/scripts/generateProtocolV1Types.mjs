#!/usr/bin/env node
// Generates the browser's immutable TypeScript boundary and bundled validation
// schemas from the canonical local protocol v1 schemas. With --check it writes
// nothing and fails on drift; without arguments it deterministically rewrites
// both generated files. The operation is idempotent.
//
// The pipeline itself lives in ./protocolTypeGeneration.mjs, which protocol v2
// runs with a different descriptor; this file is only v1's descriptor, so the
// two versions cannot drift apart in how they are generated.

import { generateProtocolVersion } from './protocolTypeGeneration.mjs';

await generateProtocolVersion(
  Object.freeze({
    version: 'v1',
    generatorScriptName: 'generateProtocolV1Types.mjs',
    typesFileName: 'protocolV1Types.generated.ts',
    schemasFileName: 'protocolV1Schemas.generated.ts',
    aggregateTypeName: 'ProtocolV1SchemaTypes',
    schemasExportName: 'protocolV1Schemas',
    rootSchemaFileNames: Object.freeze([
      'configuration-response.schema.json',
      'error-response.schema.json',
      'liveness-response.schema.json',
      'readiness-response.schema.json',
      'snapshot-message.schema.json',
    ]),
  }),
  process.argv.slice(2),
);
