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
  BlobRoyaleProtocolV3ChargeComponent as SessionChargeComponent,
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
  BlobRoyaleProtocolV3ShieldComponent as SessionShieldComponent,
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
/** The two immutable wire projections of the room's one NPC selection authority. */
export type SessionNpcCatalogue = Pick<
  BlobRoyaleProtocolV3WelcomeData,
  'npc_controller_kinds' | 'npc_profiles'
>;
export type SessionNpcProfile = NonNullable<
  SessionNpcCatalogue['npc_profiles']
>[number];
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

/**
 * A pulse, not a state: the payload names no direction, no duration and no actor identity, because
 * the ability configuration owns the durations and the session's own stamping owns the actor. Its
 * `input_generation` is required and nullable where `set_thrust`'s is optional -- a shield request
 * is one discrete intent rather than a held axis, so the client states the exact generation it
 * believes it holds and writes `null` for the never-invalidated entity instead of omitting the
 * member. Queue acceptance and a successful send are not activation: only the published shield
 * windows prove the server admitted the pulse.
 */
export interface SessionShieldCommand {
  readonly kind: 'shield';
  readonly payload: { readonly input_generation: number | null };
}

/**
 * A direction and nothing else. The payload names no gain because ADR 0008 fixes the burst at a
 * fraction of the room's *current* normal ceiling, which lives in match state no client authors;
 * naming one here would be a client selecting its own strength. `x` and `y` are `set_thrust`'s exact
 * per-component unit interval and carry no magnitude meaning: the server normalizes them to a unit
 * direction, so `{x: 0.5, y: 0}` charges exactly as hard as `{x: 1, y: 0}` and pointer distance
 * cannot become strength. Sending a non-unit or a zero vector is therefore legal on the wire --
 * normalization and refusal are both the server's, not this client's.
 *
 * Its `input_generation` is optional exactly as `set_thrust`'s is, and deliberately *not* shield's
 * required-and-nullable member. The recorded discriminator is payload shape: shield has no other
 * member, so an optional generation would let `{}` be the whole message, while charge always carries
 * `x` and `y` and can never be truncated into a defaulted pulse. A present zero is still refused,
 * because zero is not the absence of a generation.
 *
 * A successful send is not an activation. Only a published `charge` component proves the tick
 * admitted it; a refusal -- an unnormalizable direction, a live cooldown, active shield protection,
 * an eligible shield pulse on the same tick, or a burst the safety envelope would not admit -- is a
 * silent no-op that consumes no cooldown and returns no receipt at all.
 */
export interface SessionChargeCommand {
  readonly kind: 'charge';
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
  readonly payload: {
    readonly npc_kind: string;
    readonly seat_index: number;
    readonly profile_name?: string;
  };
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
  | SessionShieldCommand
  | SessionChargeCommand
  | SessionSetMovementTuningCommand
  | SessionSetSeatCountCommand
  | SessionSeatNpcCommand
  | SessionClearSeatCommand
  | SessionStartMatchCommand;
