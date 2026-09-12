// Protocol v1 remains the only source of public configuration: v3 deliberately adds no config
// route, so a client that wants world dimensions still calls `/api/v1/config`.
export type {
  BlobRoyaleProtocolV1ConfigurationResponse as SimulationConfigurationResponse,
  BlobRoyaleProtocolV1HTTPErrorResponse as SimulationHttpErrorResponse,
  BlobRoyaleProtocolV1PublicConfigurationData as SimulationConfiguration,
} from './generated/protocolV1Types.generated';

// Protocol v3 owns every session frame, every command, and since 2.4 the lobby directory and the
// HTTP failure envelope of every `/api/v3/` target.
export type {
  BlobRoyaleProtocolV3ControllableComponent as SessionControllableComponent,
  BlobRoyaleProtocolV3EntitySnapshot as SessionEntitySnapshot,
  BlobRoyaleProtocolV3HTTPErrorResponse as SessionHttpErrorResponse,
  BlobRoyaleProtocolV3LobbyDirectoryResponse as SessionLobbyDirectoryMessage,
  LobbyListing as SessionLobbyListing,
  Seat as SessionSeat,
  BlobRoyaleProtocolV3LifetimeComponent as SessionLifetimeComponent,
  BlobRoyaleProtocolV3MatchSection as SessionMatchSection,
  BlobRoyaleProtocolV3PhysicsBodyComponent as SessionPhysicsBodyComponent,
  BlobRoyaleProtocolV3ScoreComponent as SessionScoreComponent,
  BlobRoyaleProtocolV3StunComponent as SessionStunComponent,
  BlobRoyaleProtocolV3TeamComponent as SessionTeamComponent,
  BlobRoyaleProtocolV3WebSocketSnapshotMessage as SessionSnapshotMessage,
  BlobRoyaleProtocolV3WebSocketWelcomeMessage as SessionWelcomeMessage,
  BlobRoyaleProtocolV3WelcomeData as SessionWelcomeData,
  BlobRoyaleProtocolV3WorldSnapshotData as SessionWorldSnapshot,
  BlobRoyaleProtocolV3ZoneComponent as SessionZoneComponent,
  BlobRoyaleProtocolV3ZoneExposureComponent as SessionZoneExposureComponent,
  Outcome as SessionOutcome,
  Placement as SessionPlacement,
  Vector2 as SessionVector2,
} from './generated/protocolV3Types.generated';

import type {
  BlobRoyaleProtocolV3EntitySnapshot,
  BlobRoyaleProtocolV3MatchSection,
  BlobRoyaleProtocolV3WorldSnapshotData,
  BlobRoyaleProtocolV3WelcomeData,
} from './generated/protocolV3Types.generated';

/** The closed component vocabulary of `entity-snapshot.schema.json#/properties/components`. */
export type SessionComponentKind =
  keyof BlobRoyaleProtocolV3EntitySnapshot['components'];

/** The component value carried under one kind, with the schema's optionality removed. */
export type SessionComponentOfKind<Kind extends SessionComponentKind> =
  NonNullable<BlobRoyaleProtocolV3EntitySnapshot['components'][Kind]>;

export type SessionMatchPhase = BlobRoyaleProtocolV3MatchSection['phase'];

/** The client-sendable command vocabulary; `spawn` and `despawn` are server-issued and absent. */
export type SessionCommandKind =
  BlobRoyaleProtocolV3WelcomeData['accepted_command_kinds'][number];

/** Immutable authored geometry, owned by the validated welcome rather than per-tick frames. */
export type SessionTerrain = BlobRoyaleProtocolV3WelcomeData['terrain'];
export type SessionMovementTuning =
  BlobRoyaleProtocolV3MatchSection['movement']['current'];
export type SessionMovementState = BlobRoyaleProtocolV3MatchSection['movement'];
export type SessionTuningResult = NonNullable<
  BlobRoyaleProtocolV3WorldSnapshotData['tuning_result']
>;

export interface SessionSetMovementTuningCommand {
  readonly kind: 'set_movement_tuning';
  readonly payload: SessionMovementTuning & {
    readonly tuning_request_id: number;
    readonly expected_revision: number;
  };
}

/** Client-only delivery knowledge, separate from shared room state and server result statuses. */
export type MovementTuningExchangeState =
  | { readonly status: 'idle' }
  | {
      readonly status: 'pending';
      readonly request: SessionSetMovementTuningCommand['payload'];
    }
  | {
      readonly status: 'resolved';
      readonly request: SessionSetMovementTuningCommand['payload'];
      readonly result: SessionTuningResult;
    }
  | {
      readonly status: 'unknown';
      readonly request: SessionSetMovementTuningCommand['payload'];
    };

/**
 * The commands a v3 client may send. The generated envelope type is deliberately looser than the
 * schema — json-schema-to-typescript cannot express the if/then payload correlation — so each
 * outbound shape is stated exactly here and validated against the schema before it is sent.
 */
export interface SessionSetThrustCommand {
  readonly kind: 'set_thrust';
  readonly payload: {
    readonly x: number;
    readonly y: number;
    readonly input_generation?: number;
  };
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
  | SessionSetMovementTuningCommand
  | SessionSetSeatCountCommand
  | SessionSeatNpcCommand
  | SessionClearSeatCommand
  | SessionStartMatchCommand;
