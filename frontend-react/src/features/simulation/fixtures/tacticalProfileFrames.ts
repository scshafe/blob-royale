import { npcCatalogueFromWelcome } from '../npcCatalogue';
import {
  validateSessionSnapshotMessage,
  validateSessionWelcomeMessage,
} from '../sessionProtocolValidation';
import type {
  SessionNpcCatalogue,
  SessionNpcProfile,
  SessionSeatNpcCommand,
} from '../simulationProtocolTypes';
import {
  npcProfileExample,
  seatNpcProfileCommandExample,
  sessionWelcomeProfileMessageExample,
} from './protocolV3Examples';
import { snapshotDocument } from './sessionFrames';

export const STEADY_NPC_PROFILE: SessionNpcProfile = Object.freeze({
  ...npcProfileExample,
});
export const QUICK_NPC_PROFILE: SessionNpcProfile = Object.freeze({
  ...npcProfileExample,
  profile_name: 'quick',
});
export const TACTICAL_NPC_PROFILES: readonly SessionNpcProfile[] =
  Object.freeze([STEADY_NPC_PROFILE, QUICK_NPC_PROFILE]);
export const PROFILED_NPC_SEAT_INDEX = seatNpcProfileCommandExample.seat_index;
export const PROFILED_NPC_CONTROLLER_ID = 41;
export const PROFILED_NPC_DISPLAY_NAME = 'Bot 41';

/** New welcome fixtures carry the entire catalogue; legacy goldens retain their absent field. */
export function tacticalWelcomeDocument(
  profiles: readonly SessionNpcProfile[] = TACTICAL_NPC_PROFILES,
  lobbyId = 1,
) {
  const document = structuredClone(sessionWelcomeProfileMessageExample);
  document.data.npc_profiles = profiles.map((profile) => ({ ...profile }));
  document.data.lobby_id = lobbyId;
  return document;
}

export function tacticalNpcCatalogue(
  profiles: readonly SessionNpcProfile[] = TACTICAL_NPC_PROFILES,
): SessionNpcCatalogue {
  return npcCatalogueFromWelcome(
    validateSessionWelcomeMessage(tacticalWelcomeDocument(profiles), null).data,
  );
}

/** The existing joining seat acquires its complete profile identity, preserving every other row. */
export function tacticalSnapshotDocument(
  profile: SessionNpcProfile = STEADY_NPC_PROFILE,
  controllerId: number | null = null,
  messageSequence = 2,
) {
  const document = snapshotDocument(messageSequence);
  const seat = document.data.match.seats[PROFILED_NPC_SEAT_INDEX];
  if (seat === undefined) throw new Error('TEST.TACTICAL_PROFILE_SEAT_MISSING');
  seat.kind = 'npc';
  seat.npc_kind = profile.npc_kind;
  seat.controller_id = controllerId;
  Reflect.set(seat, 'profile_name', profile.profile_name);
  return document;
}

export function tacticalSnapshot(
  profile: SessionNpcProfile = STEADY_NPC_PROFILE,
  controllerId: number | null = null,
) {
  const welcome = validateSessionWelcomeMessage(
    tacticalWelcomeDocument(),
    null,
  );
  return validateSessionSnapshotMessage(
    tacticalSnapshotDocument(profile, controllerId),
    {
      messageSequence: welcome.meta.message_sequence,
      requestId: welcome.meta.request_id,
      tickSequence: null,
      terrain: welcome.data.terrain,
      npcCatalogue: npcCatalogueFromWelcome(welcome.data),
    },
  );
}

export function tacticalSeatCommand(
  profile: SessionNpcProfile = STEADY_NPC_PROFILE,
  seatIndex = PROFILED_NPC_SEAT_INDEX,
): SessionSeatNpcCommand {
  return { kind: 'seat_npc', payload: { ...profile, seat_index: seatIndex } };
}

export const INVALID_NPC_CATALOGUES: readonly {
  readonly name: string;
  readonly fields: Readonly<Record<string, unknown>>;
}[] = Object.freeze([
  { name: 'null catalogue', fields: { npc_profiles: null } },
  { name: 'present empty catalogue', fields: { npc_profiles: [] } },
  {
    name: 'missing profile name',
    fields: { npc_profiles: [{ npc_kind: 'tactical' }] },
  },
  {
    name: 'missing kind',
    fields: { npc_profiles: [{ profile_name: 'steady' }] },
  },
  {
    name: 'null profile name',
    fields: { npc_profiles: [{ npc_kind: 'tactical', profile_name: null }] },
  },
  {
    name: 'invalid profile grammar',
    fields: {
      npc_profiles: [{ npc_kind: 'tactical', profile_name: 'Steady' }],
    },
  },
  {
    name: 'empty profile name',
    fields: { npc_profiles: [{ npc_kind: 'tactical', profile_name: '' }] },
  },
  {
    name: 'oversized profile name',
    fields: {
      npc_profiles: [{ npc_kind: 'tactical', profile_name: 'a'.repeat(65) }],
    },
  },
  {
    name: 'invalid kind grammar',
    fields: {
      npc_profiles: [{ npc_kind: 'Tactical', profile_name: 'steady' }],
    },
  },
  {
    name: 'repeated pair',
    fields: { npc_profiles: [STEADY_NPC_PROFILE, STEADY_NPC_PROFILE] },
  },
  {
    name: 'kind in both partitions',
    fields: {
      npc_controller_kinds: ['tactical'],
      npc_profiles: [STEADY_NPC_PROFILE],
    },
  },
  {
    name: 'numeric settings in catalogue',
    fields: { npc_profiles: [{ ...STEADY_NPC_PROFILE, aim_error: 0 }] },
  },
  {
    name: 'profile count overflow',
    fields: {
      npc_profiles: Array.from({ length: 17 }, (_, index) => ({
        npc_kind: 'tactical',
        profile_name: `profile_${index}`,
      })),
    },
  },
]);

export const MAXIMUM_NPC_CATALOGUE = Object.freeze({
  npc_controller_kinds: Array.from(
    { length: 64 },
    (_, index) => `diagnostic_${index}`,
  ),
  npc_profiles: Array.from({ length: 16 }, (_, index) => ({
    npc_kind: 'tactical',
    profile_name: `profile_${index}`,
  })),
});

export const INVALID_NPC_DECLARATIONS: readonly {
  readonly name: string;
  readonly declaration: Readonly<Record<string, unknown>>;
}[] = Object.freeze([
  { name: 'missing tactical profile', declaration: { npc_kind: 'tactical' } },
  {
    name: 'unknown profile',
    declaration: { npc_kind: 'tactical', profile_name: 'unknown' },
  },
  {
    name: 'profile on plain kind',
    declaration: { npc_kind: 'wanderer', profile_name: 'steady' },
  },
  { name: 'unknown plain kind', declaration: { npc_kind: 'unregistered' } },
  {
    name: 'profile on wrong kind',
    declaration: { npc_kind: 'unregistered', profile_name: 'steady' },
  },
  {
    name: 'null profile',
    declaration: { npc_kind: 'tactical', profile_name: null },
  },
  {
    name: 'empty profile',
    declaration: { npc_kind: 'tactical', profile_name: '' },
  },
]);
