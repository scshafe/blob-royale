import { SimulationApiError } from './SimulationApiError';
import type {
  SessionNpcCatalogue,
  SessionWelcomeData,
} from './simulationProtocolTypes';

/**
 * @canonical npc_catalogue -- retain the validated welcome's exact two projections and enforce
 * their shared identity constraints. Schema validation owns names, sizes, and closed shapes.
 * A cross-partition kind or repeated profile pair raises SIMULATION.SESSION_INVARIANT_VIOLATION.
 */
export function npcCatalogueFromWelcome(
  welcome: SessionWelcomeData,
): SessionNpcCatalogue {
  const plainKinds = new Set(welcome.npc_controller_kinds);
  const profilesByKind = new Map<string, Set<string>>();
  for (const profile of welcome.npc_profiles ?? []) {
    const previousNames =
      profilesByKind.get(profile.npc_kind) ?? new Set<string>();
    if (
      plainKinds.has(profile.npc_kind) ||
      previousNames.has(profile.profile_name)
    ) {
      throw new SimulationApiError(
        'SIMULATION.SESSION_INVARIANT_VIOLATION',
        'NPC catalogue partitions must have disjoint kinds and unique profile declarations.',
        {
          context: {
            npc_kind: profile.npc_kind,
            profile_name: profile.profile_name,
          },
        },
      );
    }
    previousNames.add(profile.profile_name);
    profilesByKind.set(profile.npc_kind, previousNames);
  }
  return Object.freeze({
    npc_controller_kinds: welcome.npc_controller_kinds,
    ...(welcome.npc_profiles === undefined
      ? {}
      : { npc_profiles: welcome.npc_profiles }),
  });
}

/** Exact optional profile membership; no catalogue admits no declarations, never unchecked ones. */
export function npcCatalogueContains(
  catalogue: SessionNpcCatalogue | null,
  npcKind: string,
  profileName: string | undefined,
): boolean {
  if (catalogue === null) return false;
  return profileName === undefined
    ? catalogue.npc_controller_kinds.includes(npcKind)
    : (catalogue.npc_profiles?.some(
        (profile) =>
          profile.npc_kind === npcKind && profile.profile_name === profileName,
      ) ?? false);
}
