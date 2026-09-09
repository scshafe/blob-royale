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
 * The one command a v2 client may send today. The generated envelope type is deliberately looser
 * than the schema — json-schema-to-typescript cannot express the if/then payload correlation — so
 * the outbound shape is stated exactly here and validated against the schema before it is sent.
 */
export interface SessionSetThrustCommand {
  readonly kind: 'set_thrust';
  readonly payload: { readonly x: number; readonly y: number };
}

/** @extension-point session_command -- a new client command kind adds one member to this union. */
export type SessionCommand = SessionSetThrustCommand;
