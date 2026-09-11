import { describe, expect, it } from 'vitest';

import { protocolV3Schemas } from '../generated/protocolV3Schemas.generated';
import { modeStateRendererRegistry } from './modeStateRendererRegistry';

describe('modeStateRendererRegistry', () => {
  it('registers exactly the schema ids protocol validation accepts', () => {
    expect(Object.keys(modeStateRendererRegistry).sort()).toEqual(
      [...protocolV3Schemas.common.$defs.mode_state_schema_id.enum].sort(),
    );
  });

  it('draws the race block and explains why every other block has no geometry', () => {
    const visualSchemaIds: string[] = [];
    for (const [schemaId, registration] of Object.entries(
      modeStateRendererRegistry,
    )) {
      if (registration.renders) {
        visualSchemaIds.push(schemaId);
      } else {
        expect(registration.reason.length).toBeGreaterThan(20);
      }
    }
    expect(visualSchemaIds).toEqual([
      'blob-royale://protocol/v3/mode-state/race',
    ]);
  });
});
