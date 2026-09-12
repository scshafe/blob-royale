import { describe, expect, it } from 'vitest';
import { npcCatalogueContains, npcCatalogueFromWelcome } from './npcCatalogue';
import { validateSessionWelcomeMessage } from './sessionProtocolValidation';
import { legacyNpcCatalogue, welcomeDocument } from './fixtures/sessionFrames';
import {
  QUICK_NPC_PROFILE,
  STEADY_NPC_PROFILE,
  tacticalNpcCatalogue,
  tacticalWelcomeDocument,
} from './fixtures/tacticalProfileFrames';

describe('npcCatalogue', () => {
  it('preserves absence and order of the legacy plain projection', () => {
    const catalogue = npcCatalogueFromWelcome(
      validateSessionWelcomeMessage(welcomeDocument(), null).data,
    );
    expect(catalogue).toEqual(legacyNpcCatalogue);
    expect(Object.hasOwn(catalogue, 'npc_profiles')).toBe(false);
  });

  it('retains the exact frozen welcome projections without copying mutable capability lists', () => {
    const welcome = validateSessionWelcomeMessage(
      tacticalWelcomeDocument(),
      null,
    );
    const catalogue = npcCatalogueFromWelcome(welcome.data);
    expect(catalogue.npc_controller_kinds).toBe(
      welcome.data.npc_controller_kinds,
    );
    expect(catalogue.npc_profiles).toBe(welcome.data.npc_profiles);
    expect(Object.isFrozen(catalogue)).toBe(true);
    expect(Object.isFrozen(catalogue.npc_profiles)).toBe(true);
    expect(Object.isFrozen(catalogue.npc_profiles?.[0])).toBe(true);
  });

  it('requires exact kind and optional profile membership in its partition', () => {
    const catalogue = tacticalNpcCatalogue();
    expect(npcCatalogueContains(catalogue, 'wanderer', undefined)).toBe(true);
    expect(
      npcCatalogueContains(
        catalogue,
        STEADY_NPC_PROFILE.npc_kind,
        STEADY_NPC_PROFILE.profile_name,
      ),
    ).toBe(true);
    expect(
      npcCatalogueContains(
        catalogue,
        QUICK_NPC_PROFILE.npc_kind,
        QUICK_NPC_PROFILE.profile_name,
      ),
    ).toBe(true);
    expect(
      npcCatalogueContains(catalogue, STEADY_NPC_PROFILE.npc_kind, undefined),
    ).toBe(false);
    expect(
      npcCatalogueContains(
        catalogue,
        'wanderer',
        STEADY_NPC_PROFILE.profile_name,
      ),
    ).toBe(false);
    expect(npcCatalogueContains(null, 'wanderer', undefined)).toBe(false);
    expect(
      npcCatalogueContains({ npc_controller_kinds: [] }, 'wanderer', undefined),
    ).toBe(false);
  });
});
