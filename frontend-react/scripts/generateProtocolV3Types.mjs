#!/usr/bin/env node
// Generates the browser's immutable TypeScript boundary and bundled validation
// schemas from the canonical local protocol v3 schemas. With --check it writes
// nothing and fails on drift; without arguments it deterministically rewrites
// both generated files. The operation is idempotent.
//
// The five roots are v3's five wire documents: the two server frames a session
// receives, the one client frame it may send, the HTTP failure envelope of a
// /api/v3/ target, and the lobby directory a client chooses a room from. Every
// component, match, mode-state, command payload, and lobby-listing schema is
// reachable from those five, so the compiler emits a type for each without this
// list naming it -- which is what keeps adding a component kind a schema change
// rather than a generator change.
//
// The pipeline itself lives in ./protocolTypeGeneration.mjs, shared with v1.

import { generateProtocolVersion } from './protocolTypeGeneration.mjs';

await generateProtocolVersion(
  Object.freeze({
    version: 'v3',
    generatorScriptName: 'generateProtocolV3Types.mjs',
    typesFileName: 'protocolV3Types.generated.ts',
    schemasFileName: 'protocolV3Schemas.generated.ts',
    aggregateTypeName: 'ProtocolV3SchemaTypes',
    schemasExportName: 'protocolV3Schemas',
    // json-schema-to-typescript 15.0.4 lowers this version's `allOf` + `if/then` composition --
    // the command envelope's payload, the match section's mode-state block, and the outcome's
    // paired winners -- to an `unknown | undefined` index signature and an empty `{}` member. Both
    // are the pinned compiler's output for an accepted schema rather than a style choice made
    // here, and the exact runtime shape is enforced by Ajv against `protocolV3Schemas`, not by
    // these declarations. The directive is emitted into the generated file rather than configured
    // repository-wide so every other file, generated or not, keeps both rules.
    typesPrelude: `/* eslint-disable @typescript-eslint/no-empty-object-type, @typescript-eslint/no-redundant-type-constituents --
   Emitted by json-schema-to-typescript 15.0.4 for the accepted v3 schemas' allOf/if-then
   composition; the exact shape is enforced at runtime by Ajv against protocolV3Schemas. */

`,
    rootSchemaFileNames: Object.freeze([
      'command-envelope.schema.json',
      'error-response.schema.json',
      'lobby-directory-message.schema.json',
      'snapshot-message.schema.json',
      'welcome-message.schema.json',
    ]),
  }),
  process.argv.slice(2),
);
