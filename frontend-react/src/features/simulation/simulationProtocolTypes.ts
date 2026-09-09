// Protocol v1 remains the only source of public configuration: v2 deliberately adds no config
// route, so a client that wants world dimensions still calls `/api/v1/config`.
export type {
  BlobRoyaleProtocolV1ConfigurationResponse as SimulationConfigurationResponse,
  BlobRoyaleProtocolV1HTTPErrorResponse as SimulationHttpErrorResponse,
  BlobRoyaleProtocolV1PublicConfigurationData as SimulationConfiguration,
} from './generated/protocolV1Types.generated';

// Protocol v2 owns every session frame, every command, and since 2.4 the lobby directory and the
// HTTP failure envelope of every `/api/v2/` target.
export type {
  BlobRoyaleProtocolV2ControllableComponent as SessionControllableComponent,
  BlobRoyaleProtocolV2EntitySnapshot as SessionEntitySnapshot,
  BlobRoyaleProtocolV2HTTPErrorResponse as SessionHttpErrorResponse,
  BlobRoyaleProtocolV2LobbyDirectoryResponse as SessionLobbyDirectoryMessage,
  LobbyListing as SessionLobbyListing,
  Seat as SessionSeat,
  BlobRoyaleProtocolV2LifetimeComponent as SessionLifetimeComponent,
  BlobRoyaleProtocolV2MatchSection as SessionMatchSection,
  BlobRoyaleProtocolV2PhysicsBodyComponent as SessionPhysicsBodyComponent,
  BlobRoyaleProtocolV2ScoreComponent as SessionScoreComponent,
  BlobRoyaleProtocolV2TeamComponent as SessionTeamComponent,
  BlobRoyaleProtocolV2WebSocketSnapshotMessage as SessionSnapshotMessage,
  BlobRoyaleProtocolV2WebSocketWelcomeMessage as SessionWelcomeMessage,
  BlobRoyaleProtocolV2WelcomeData as SessionWelcomeData,
  BlobRoyaleProtocolV2WorldSnapshotData as SessionWorldSnapshot,
  BlobRoyaleProtocolV2ZoneComponent as SessionZoneComponent,
  BlobRoyaleProtocolV2ZoneExposureComponent as SessionZoneExposureComponent,
  Outcome as SessionOutcome,
  Placement as SessionPlacement,
  Vector2 as SessionVector2,
} from './generated/protocolV2Types.generated';

import type {
  BlobRoyaleProtocolV2EntitySnapshot,
  BlobRoyaleProtocolV2MatchSection,
  BlobRoyaleProtocolV2WelcomeData,
} from './generated/protocolV2Types.generated';

/** The closed component vocabulary of `entity-snapshot.schema.json#/properties/components`. */
export type SessionComponentKind =
  keyof BlobRoyaleProtocolV2EntitySnapshot['components'];

/** The component value carried under one kind, with the schema's optionality removed. */
export type SessionComponentOfKind<Kind extends SessionComponentKind> =
  NonNullable<BlobRoyaleProtocolV2EntitySnapshot['components'][Kind]>;

export type SessionMatchPhase = BlobRoyaleProtocolV2MatchSection['phase'];

/** The client-sendable command vocabulary; `spawn` and `despawn` are server-issued and absent. */
export type SessionCommandKind =
  BlobRoyaleProtocolV2WelcomeData['accepted_command_kinds'][number];

/**
 * The commands a v2 client may send. The generated envelope type is deliberately looser than the
 * schema — json-schema-to-typescript cannot express the if/then payload correlation — so each
 * outbound shape is stated exactly here and validated against the schema before it is sent.
 */
export interface SessionSetThrustCommand {
  readonly kind: 'set_thrust';
  readonly payload: { readonly x: number; readonly y: number };
}

/** A count, not a delta: two clients who both choose four agree rather than compounding. */
export interface SessionSetSeatCountCommand {
  readonly kind: 'set_seat_count';
  readonly payload: { readonly seat_count: number };
}

/** Fills an empty seat with a bot of a kind the welcome published; never replaces an occupant. */
export interface SessionSeatNpcCommand {
  readonly kind: 'seat_npc';
  readonly payload: { readonly npc_kind: string; readonly seat_index: number };
}

/** Empties an NPC seat; a person's seat belongs to a live session and is left alone. */
export interface SessionClearSeatCommand {
  readonly kind: 'clear_seat';
  readonly payload: { readonly seat_index: number };
}

/** Records a request; the server commits the start only once the field is complete. */
export interface SessionStartMatchCommand {
  readonly kind: 'start_match';
  readonly payload: Readonly<Record<string, never>>;
}

/** @extension-point session_command -- a new client command kind adds one member to this union. */
export type SessionCommand =
  | SessionSetThrustCommand
  | SessionSetSeatCountCommand
  | SessionSeatNpcCommand
  | SessionClearSeatCommand
  | SessionStartMatchCommand;
