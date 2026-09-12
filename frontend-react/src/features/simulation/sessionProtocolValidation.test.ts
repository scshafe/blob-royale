import {
  maximumRaceTerrain,
  namedRaceTerrain,
  raceTerrain,
  solidTerrain,
} from './fixtures/terrainFrames';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { SimulationApiError } from './SimulationApiError';
import {
  ANY_TOUCH_ADMISSION,
  INVALID_CONTACT_EFFECT_ADMISSIONS,
  contactEffectAdmissionSnapshotDocument,
} from './fixtures/contactEffectAdmissionFrames';
import {
  INVALID_NPC_CATALOGUES,
  INVALID_NPC_DECLARATIONS,
  MAXIMUM_NPC_CATALOGUE,
  PROFILED_NPC_SEAT_INDEX,
  STEADY_NPC_PROFILE,
  tacticalNpcCatalogue,
  tacticalSeatCommand,
  tacticalSnapshotDocument,
  tacticalWelcomeDocument,
} from './fixtures/tacticalProfileFrames';
import {
  acceptedHillMotionComponents,
  hillMotionSnapshotDocument,
  malformedHillMotionComponents,
} from './fixtures/hillMotionFrames';
import {
  lobbyDirectoryMessageExample,
  raceModeStateExample,
  sessionCommandEnvelopeExample,
  sessionErrorResponseExample,
  sessionWelcomeMessageExample,
} from './fixtures/protocolV3Examples';
import {
  legacyNpcCatalogue,
  firstEntity,
  MAXIMUM_PUBLISHED_WORLD_SCALAR,
  MAXIMUM_TERRAIN_WORLD_SCALAR,
  maximumHillSnapshotDocument,
  maximumRaceSnapshotDocument,
  playerEntity,
  snapshotDocument,
  welcomeDocument,
} from './fixtures/sessionFrames';
import {
  SUPPORTED_PROTOCOL_VERSION,
  type SessionSequenceState,
  validateLobbyDirectoryMessage,
  validateSessionCommand,
  validateSessionHttpErrorResponse,
  validateSessionSnapshotMessage,
  validateSessionWelcomeMessage,
  validateSessionTuningResult,
} from './sessionProtocolValidation';
import type {
  SessionChargeCommand,
  SessionCommand,
  SessionSnapshotMessage,
} from './simulationProtocolTypes';
import {
  tuningCommand,
  tuningSnapshotDocument,
  TUNING_RESULT_STATUSES,
} from './fixtures/tuningFrames';
import {
  ACCEPTED_SHIELD_COMPONENTS,
  CANCELLED_SHIELD_WINDOWS,
  INVALID_SHIELD_COMPONENTS,
  SHIELD_SNAPSHOT_TICK,
  shieldSnapshotDocument,
} from './fixtures/shieldFrames';
import {
  ACCEPTED_CHARGE_COMPONENTS,
  CHARGE_ACTIVATION_TICK,
  CHARGE_COOLDOWN,
  INVALID_CHARGE_COMPONENTS,
  chargeSnapshotDocument,
} from './fixtures/chargeFrames';
import {
  INVALID_INPUT_GENERATIONS,
  INVALID_STUN_WINDOWS,
  STUN_INPUT_GENERATION,
  STUN_INPUT_SNAPSHOT_TICK,
  STUN_INPUT_WINDOW,
  THRUST_GENERATION_COMMAND_CASES,
  stunInputSnapshotDocument,
} from './fixtures/stunInputFrames';

const welcomeSequence: SessionSequenceState = Object.freeze({
  messageSequence: 1,
  requestId: sessionWelcomeMessageExample.meta.request_id,
  tickSequence: null,
  npcCatalogue: legacyNpcCatalogue,
  terrain: solidTerrain,
});

const raceSequence: SessionSequenceState = Object.freeze({
  ...welcomeSequence,
  terrain: raceTerrain,
});

function raceSnapshotDocument(state = structuredClone(raceModeStateExample)) {
  const document = snapshotDocument();
  return {
    ...document,
    data: {
      ...document.data,
      match: {
        ...document.data.match,
        mode: 'race',
        placements: [],
        mode_state: {
          schema_id: 'blob-royale://protocol/v3/mode-state/race',
          value: state,
        },
      },
    },
  };
}

function silenceProtocolWarnings() {
  return vi.spyOn(console, 'warn').mockImplementation(() => undefined);
}

afterEach(() => {
  vi.restoreAllMocks();
});

describe('NPC catalogue and profile protocol', () => {
  it.each(INVALID_NPC_CATALOGUES)('rejects $name in welcome', ({ fields }) => {
    const document = tacticalWelcomeDocument();
    Object.assign(document.data, fields);
    expect(() => validateSessionWelcomeMessage(document, null)).toThrow(
      SimulationApiError,
    );
  });

  it('admits the inclusive 64 plain and 16 profiled choices budget', () => {
    const document = tacticalWelcomeDocument();
    Object.assign(document.data, MAXIMUM_NPC_CATALOGUE);
    expect(() => validateSessionWelcomeMessage(document, null)).not.toThrow();
  });

  it('allows the same profile name under distinct profiled kinds', () => {
    const document = tacticalWelcomeDocument([
      STEADY_NPC_PROFILE,
      { ...STEADY_NPC_PROFILE, npc_kind: 'another_algorithm' },
    ]);
    expect(() => validateSessionWelcomeMessage(document, null)).not.toThrow();
  });

  it('admits a complete declared profile and freezes it on a published NPC seat', () => {
    const snapshot = validateSessionSnapshotMessage(
      tacticalSnapshotDocument(),
      {
        ...welcomeSequence,
        npcCatalogue: tacticalNpcCatalogue(),
      },
    );
    expect(snapshot.data.match.seats[PROFILED_NPC_SEAT_INDEX]).toMatchObject(
      STEADY_NPC_PROFILE,
    );
    expect(
      Object.isFrozen(snapshot.data.match.seats[PROFILED_NPC_SEAT_INDEX]),
    ).toBe(true);
    expect(() =>
      validateSessionCommand(tacticalSeatCommand(), tacticalNpcCatalogue()),
    ).not.toThrow();
  });

  it('admits no NPC command by default when no catalogue is supplied', () => {
    expect(() => validateSessionCommand(tacticalSeatCommand())).toThrow(
      SimulationApiError,
    );
    expect(() =>
      validateSessionCommand({
        kind: 'seat_npc',
        payload: { npc_kind: 'wanderer', seat_index: PROFILED_NPC_SEAT_INDEX },
      }),
    ).toThrow(SimulationApiError);
  });

  it.each(INVALID_NPC_DECLARATIONS)(
    'rejects $name on outgoing and published NPC declarations',
    ({ declaration }) => {
      const command = {
        kind: 'seat_npc',
        payload: { seat_index: PROFILED_NPC_SEAT_INDEX, ...declaration },
      };
      expect(() =>
        validateSessionCommand(
          command as SessionCommand,
          tacticalNpcCatalogue(),
        ),
      ).toThrow(SimulationApiError);
      const document = tacticalSnapshotDocument();
      document.data.match.seats[PROFILED_NPC_SEAT_INDEX] = {
        kind: 'npc',
        controller_id: null,
        ...declaration,
      } as (typeof document.data.match.seats)[number];
      expect(() =>
        validateSessionSnapshotMessage(document, {
          ...welcomeSequence,
          npcCatalogue: tacticalNpcCatalogue(),
        }),
      ).toThrow(SimulationApiError);
    },
  );

  it('rejects a profile from a previous welcome catalogue', () => {
    expect(() =>
      validateSessionSnapshotMessage(
        tacticalSnapshotDocument(),
        welcomeSequence,
      ),
    ).toThrow(SimulationApiError);
  });

  it.each(['empty', 'controller'] as const)(
    'rejects profile metadata on a %s seat',
    (kind) => {
      const document = tacticalSnapshotDocument();
      const seat = document.data.match.seats[PROFILED_NPC_SEAT_INDEX];
      if (seat === undefined)
        throw new Error('TEST.TACTICAL_PROFILE_SEAT_MISSING');
      Object.assign(seat, {
        kind,
        npc_kind: null,
        controller_id: kind === 'controller' ? 3 : null,
      });
      expect(() =>
        validateSessionSnapshotMessage(document, {
          ...welcomeSequence,
          npcCatalogue: tacticalNpcCatalogue(),
        }),
      ).toThrow(SimulationApiError);
    },
  );
});

describe('per-object contact effect admission protocol', () => {
  it('accepts and freezes any-touch without manufacturing the default component', () => {
    const ordinary = validateSessionSnapshotMessage(
      contactEffectAdmissionSnapshotDocument(),
      welcomeSequence,
    );
    expect(
      ordinary.data.entities.every(
        (entity) => entity.components.contact_effect_admission === undefined,
      ),
    ).toBe(true);
    const mutableAdmission = { policy: 'any_touch' };
    expect(Object.isFrozen(mutableAdmission)).toBe(false);
    const snapshot = validateSessionSnapshotMessage(
      contactEffectAdmissionSnapshotDocument(mutableAdmission),
      welcomeSequence,
    );
    const admissions = snapshot.data.entities.flatMap((entity) =>
      entity.components.contact_effect_admission === undefined
        ? []
        : [entity.components.contact_effect_admission],
    );
    expect(admissions).toEqual([ANY_TOUCH_ADMISSION]);
    expect(Object.isFrozen(admissions[0])).toBe(true);
  });

  it.each(INVALID_CONTACT_EFFECT_ADMISSIONS)('rejects $name', ({ value }) => {
    expect(() =>
      validateSessionSnapshotMessage(
        contactEffectAdmissionSnapshotDocument(value),
        welcomeSequence,
      ),
    ).toThrow(SimulationApiError);
  });

  it('rejects admission on an entity with no physical body', () => {
    const document =
      contactEffectAdmissionSnapshotDocument(ANY_TOUCH_ADMISSION);
    Reflect.deleteProperty(playerEntity(document).components, 'physics_body');
    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(SimulationApiError);
  });
});

describe('stun and input generation protocol', () => {
  it.each([
    undefined,
    1,
    STUN_INPUT_GENERATION,
    STUN_INPUT_SNAPSHOT_TICK,
    Number.MAX_SAFE_INTEGER,
  ])(
    'accepts the exact optional generation %s without inventing absent values',
    (generation) => {
      const tick =
        generation === Number.MAX_SAFE_INTEGER
          ? generation
          : STUN_INPUT_SNAPSHOT_TICK;
      const snapshot = validateSessionSnapshotMessage(
        stunInputSnapshotDocument(generation, undefined, tick),
        welcomeSequence,
      );
      const own = snapshot.data.entities.find(
        (entity) => entity.entity_id === 7,
      );
      expect(own?.components.controllable?.input_generation).toBe(generation);
      expect(
        Object.hasOwn(own?.components.controllable ?? {}, 'input_generation'),
      ).toBe(generation !== undefined);
    },
  );

  it.each(INVALID_INPUT_GENERATIONS)(
    'rejects $name generation on published controllable',
    ({ value }) => {
      expect(() =>
        validateSessionSnapshotMessage(
          stunInputSnapshotDocument(value),
          welcomeSequence,
        ),
      ).toThrow(SimulationApiError);
    },
  );

  it('rejects a generation after its covering snapshot tick', () => {
    expect(() =>
      validateSessionSnapshotMessage(
        stunInputSnapshotDocument(STUN_INPUT_SNAPSHOT_TICK + 1),
        welcomeSequence,
      ),
    ).toThrow(SimulationApiError);
  });

  it.each([STUN_INPUT_SNAPSHOT_TICK, STUN_INPUT_WINDOW.expiry_tick])(
    'accepts and freezes published stun endpoints at snapshot tick %s',
    (tick) => {
      const snapshot = validateSessionSnapshotMessage(
        stunInputSnapshotDocument(
          STUN_INPUT_GENERATION,
          STUN_INPUT_WINDOW,
          tick,
        ),
        welcomeSequence,
      );
      const stun = snapshot.data.entities.find(
        (entity) => entity.entity_id === 7,
      )?.components.stun;
      expect(stun).toEqual(STUN_INPUT_WINDOW);
      expect(Object.isFrozen(stun)).toBe(true);
    },
  );

  it('accepts expiry at the maximum exact integer', () => {
    const window = { activation_tick: 1, expiry_tick: Number.MAX_SAFE_INTEGER };
    expect(() =>
      validateSessionSnapshotMessage(
        stunInputSnapshotDocument(STUN_INPUT_GENERATION, window),
        welcomeSequence,
      ),
    ).not.toThrow();
  });

  it.each(INVALID_STUN_WINDOWS)('rejects $name stun', ({ value }) => {
    expect(() =>
      validateSessionSnapshotMessage(
        stunInputSnapshotDocument(STUN_INPUT_GENERATION, value),
        welcomeSequence,
      ),
    ).toThrow(SimulationApiError);
  });

  it.each(THRUST_GENERATION_COMMAND_CASES)('accepts $name', ({ command }) => {
    expect(() => validateSessionCommand(command)).not.toThrow();
  });

  it.each(INVALID_INPUT_GENERATIONS)(
    'rejects $name generation in a zero release',
    ({ value }) => {
      const command = structuredClone(sessionCommandEnvelopeExample);
      Reflect.set(command.payload, 'x', 0);
      Reflect.set(command.payload, 'y', 0);
      Reflect.set(command.payload, 'input_generation', value);
      expect(() => validateSessionCommand(command as SessionCommand)).toThrow(
        SimulationApiError,
      );
    },
  );
});

describe('shield protocol', () => {
  function publishedShield(snapshot: SessionSnapshotMessage) {
    return snapshot.data.entities.find((entity) => entity.entity_id === 7)
      ?.components.shield;
  }

  it.each(ACCEPTED_SHIELD_COMPONENTS)(
    'accepts $name and publishes its five members unchanged',
    ({ value }) => {
      const snapshot = validateSessionSnapshotMessage(
        shieldSnapshotDocument(value),
        welcomeSequence,
      );
      const shield = publishedShield(snapshot);
      expect(shield).toEqual(value);
      // The published order is the encoder's, and a client that dropped or reordered a member would
      // read one window's endpoint as another's; every member is public by contract, so there is
      // nothing here for the client to hide either.
      expect(Object.keys(shield ?? {})).toEqual([
        'activation_tick',
        'shield_expiry_tick',
        'perfect_expiry_tick',
        'cooldown_expiry_tick',
        'parry_stun_duration_ticks',
      ]);
      expect(Object.isFrozen(shield)).toBe(true);
    },
  );

  it.each(INVALID_SHIELD_COMPONENTS)(
    'rejects $name rather than reading a window it had to guess',
    ({ value }) => {
      expect(() =>
        validateSessionSnapshotMessage(
          shieldSnapshotDocument(value),
          welcomeSequence,
        ),
      ).toThrow(SimulationApiError);
    },
  );

  it('accepts cancelled zero-length protection while its cooldown still refuses a pulse', () => {
    // This is the single case a `<` on the protection endpoints would have rejected. The status
    // system cancels a shield on the tick it activated, so protection covers no tick at all while
    // the cooldown it already started keeps running -- and the live cooldown is the only reason the
    // component is still published. Closing the connection over it would drop a frame the server is
    // required to send whenever a player is stunned on their own activation tick.
    const snapshot = validateSessionSnapshotMessage(
      shieldSnapshotDocument(CANCELLED_SHIELD_WINDOWS),
      welcomeSequence,
    );
    const shield = publishedShield(snapshot);
    expect(shield?.shield_expiry_tick).toBe(
      CANCELLED_SHIELD_WINDOWS.activation_tick,
    );
    expect(shield?.perfect_expiry_tick).toBe(
      CANCELLED_SHIELD_WINDOWS.activation_tick,
    );
    expect(shield?.cooldown_expiry_tick).toBeGreaterThan(SHIELD_SNAPSHOT_TICK);
    expect(shield?.parry_stun_duration_ticks).toBeGreaterThan(0);
  });

  it.each([null, 1, STUN_INPUT_GENERATION, Number.MAX_SAFE_INTEGER])(
    'accepts a shield pulse naming generation %s',
    (generation) => {
      expect(() =>
        validateSessionCommand({
          kind: 'shield',
          payload: { input_generation: generation },
        }),
      ).not.toThrow();
    },
  );

  // `input_generation` is required and nullable on a pulse where `set_thrust` leaves it optional:
  // one discrete request states the exact generation it believes it holds, and a never-invalidated
  // entity says so with `null` rather than leaving a reader to infer it from an absent member. The
  // payload also names no actor and no duration -- session stamping owns the first and the mode's
  // ability configuration owns the second, so either one on the wire is a client overreaching.
  const invalidShieldPulses: readonly {
    readonly name: string;
    readonly payload: Readonly<Record<string, unknown>>;
  }[] = [
    { name: 'an omitted generation', payload: {} },
    { name: 'a present zero generation', payload: { input_generation: 0 } },
    { name: 'a negative generation', payload: { input_generation: -1 } },
    { name: 'a fractional generation', payload: { input_generation: 1.5 } },
    {
      name: 'an unsafe generation',
      payload: { input_generation: Number.MAX_SAFE_INTEGER + 1 },
    },
    { name: 'a string generation', payload: { input_generation: '12900' } },
    {
      name: 'a smuggled actor identity',
      payload: { input_generation: null, entity_id: 7 },
    },
    {
      name: 'an authored duration the configuration owns',
      payload: { input_generation: null, shield_duration_ticks: 160 },
    },
  ];

  it.each(invalidShieldPulses)(
    'rejects a shield pulse carrying $name',
    ({ payload }) => {
      expect(() =>
        validateSessionCommand({ kind: 'shield', payload } as SessionCommand),
      ).toThrow(SimulationApiError);
    },
  );
});

describe('charge protocol', () => {
  function publishedCharge(snapshot: SessionSnapshotMessage) {
    return snapshot.data.entities.find((entity) => entity.entity_id === 7)
      ?.components.charge;
  }

  it.each(ACCEPTED_CHARGE_COMPONENTS)(
    'accepts $name and publishes its two members unchanged',
    ({ value }) => {
      const snapshot = validateSessionSnapshotMessage(
        chargeSnapshotDocument(value),
        welcomeSequence,
      );
      const charge = publishedCharge(snapshot);
      expect(charge).toEqual(value);
      // The published order is the encoder's, and the pair is closed at two: the activation is the
      // denominator a cooldown arc needs and the expiry is its end, so a client that dropped or
      // reordered one would draw an arc against the wrong endpoint. Both are public by contract.
      expect(Object.keys(charge ?? {})).toEqual([
        'activation_tick',
        'cooldown_expiry_tick',
      ]);
      expect(Object.isFrozen(charge)).toBe(true);
    },
  );

  it.each(INVALID_CHARGE_COMPONENTS)(
    'rejects $name rather than reading an interval it had to guess',
    ({ value }) => {
      expect(() =>
        validateSessionSnapshotMessage(
          chargeSnapshotDocument(value),
          welcomeSequence,
        ),
      ).toThrow(SimulationApiError);
    },
  );

  it('refuses the zero-length cooldown the shield corpus deliberately accepts', () => {
    // The one case where the two abilities part company, asserted directly rather than only through
    // the corpus so the asymmetry is stated where a reader comparing the two blocks will find it.
    // Shield admits an expiry equal to its activation because a stun cancels protection to zero
    // length while the cooldown keeps running; charge has no protection window and no cancellation,
    // so the same shape would describe a one-shot with no cooldown at all -- four hundred bursts a
    // second, which is exactly why the authored value is validated strictly positive at load.
    expect(() =>
      validateSessionSnapshotMessage(
        chargeSnapshotDocument({
          ...CHARGE_COOLDOWN,
          cooldown_expiry_tick: CHARGE_ACTIVATION_TICK,
        }),
        welcomeSequence,
      ),
    ).toThrow(SimulationApiError);
    expect(() =>
      validateSessionSnapshotMessage(
        chargeSnapshotDocument({
          ...CHARGE_COOLDOWN,
          cooldown_expiry_tick: CHARGE_ACTIVATION_TICK + 1,
        }),
        welcomeSequence,
      ),
    ).not.toThrow();
    expect(() =>
      validateSessionSnapshotMessage(
        shieldSnapshotDocument(CANCELLED_SHIELD_WINDOWS),
        welcomeSequence,
      ),
    ).not.toThrow();
  });

  // The wire admits directions the ability system will refuse, and that is the boundary itself: the
  // server owns normalization and admission, so a client that pre-filtered here would be inventing
  // a second rule and could disagree with the tick. A refusal is a silent no-op, never a frame
  // error, so none of these may be turned into a send-time throw.
  const acceptedChargeDirections: readonly {
    readonly name: string;
    readonly payload: SessionChargeCommand['payload'];
  }[] = [
    {
      // The deliberate difference from shield, whose corpus above refuses exactly this omission.
      // A never-invalidated entity says so by leaving the member out, as `set_thrust` does.
      name: 'an omitted generation the shield pulse refuses',
      payload: { x: 1, y: 0 },
    },
    {
      name: 'a direction naming the generation it believes it holds',
      payload: { x: 0, y: -1, input_generation: STUN_INPUT_GENERATION },
    },
    {
      // Magnitude sqrt(2), exactly as `set_thrust` admits: the per-component bound is the wire rule
      // and magnitude is not. Normalizing here would make this client's burst disagree with the
      // server's, which is the one thing a second normalization rule guarantees.
      name: 'a diagonal past unit magnitude the server will normalize',
      payload: { x: 1, y: 1 },
    },
    {
      // The inverse case, and the reason charge may not reuse `normalized_thrust_intent`: routed
      // through the tree's magnitude *clamp* this would buy half a burst, making pointer distance
      // into strength. Normalized, it is the same charge as `{x: 1, y: 0}`.
      name: 'a subunit direction that buys no weaker charge',
      payload: { x: 0.5, y: 0 },
    },
    {
      // Passes the decoder and the input batch, then underflows to a zero squared magnitude at
      // admission and is refused there, silently. A wire rejection would turn a refusal into a
      // closed connection.
      name: 'a subnormal direction refused at admission rather than on the wire',
      payload: { x: 1e-200, y: 0 },
    },
    {
      name: 'a zero direction the ability system refuses without an error',
      payload: { x: 0, y: 0 },
    },
  ];

  it.each(acceptedChargeDirections)(
    'accepts a charge carrying $name',
    ({ payload }) => {
      expect(() =>
        validateSessionCommand({ kind: 'charge', payload }),
      ).not.toThrow();
    },
  );

  // `input_generation` is optional here where shield's is required and nullable, and the difference
  // is the tree's own recorded discriminator rather than a second vocabulary: shield has no other
  // member, so an optional one would make `{}` the whole message and a truncated send would read as
  // an authored pulse. Charge always carries `x` and `y`, so it cannot be truncated into anything,
  // and it follows `set_thrust`. A present zero is still refused -- zero is not the absence of a
  // generation. The payload also names no actor and no gain: session stamping owns the first, and
  // ADR 0008 fixes the second against the room's current ceiling, so either on the wire is a client
  // authoring its own strength.
  const invalidChargeCommands: readonly {
    readonly name: string;
    readonly payload: Readonly<Record<string, unknown>>;
  }[] = [
    {
      name: 'a present zero generation',
      payload: { x: 1, y: 0, input_generation: 0 },
    },
    {
      name: 'a null generation the optional shape has no room for',
      payload: { x: 1, y: 0, input_generation: null },
    },
    {
      name: 'a negative generation',
      payload: { x: 1, y: 0, input_generation: -1 },
    },
    {
      name: 'a fractional generation',
      payload: { x: 1, y: 0, input_generation: 1.5 },
    },
    {
      name: 'an unsafe generation',
      payload: { x: 1, y: 0, input_generation: Number.MAX_SAFE_INTEGER + 1 },
    },
    {
      name: 'a string generation',
      payload: { x: 1, y: 0, input_generation: '12900' },
    },
    {
      // The shape shield's required-and-nullable generation exists to prevent, and the reason
      // charge does not need that shape: `{}` is a truncated send here, never an authored pulse.
      name: 'an empty payload no truncation may pass off as a direction',
      payload: {},
    },
    { name: 'one component of a direction', payload: { x: 1 } },
    {
      name: 'a component outside the closed unit interval',
      payload: { x: 1.000001, y: 0 },
    },
    { name: 'a non-finite component', payload: { x: Number.NaN, y: 0 } },
    {
      name: 'a smuggled actor identity',
      payload: { x: 1, y: 0, entity_id: 7 },
    },
    {
      name: 'an authored gain the room tuning owns',
      payload: { x: 1, y: 0, speed_fraction: 0.75 },
    },
    {
      name: 'an authored burst speed the safety envelope bounds',
      payload: { x: 1, y: 0, burst_speed_world_units_per_second: 450 },
    },
  ];

  it.each(invalidChargeCommands)(
    'rejects a charge carrying $name',
    ({ payload }) => {
      expect(() =>
        validateSessionCommand({ kind: 'charge', payload } as SessionCommand),
      ).toThrow(SimulationApiError);
    },
  );
});

describe('hill motion protocol', () => {
  it.each(acceptedHillMotionComponents)(
    'accepts $name without exposing schedule',
    ({ value }) => {
      const snapshot = validateSessionSnapshotMessage(
        hillMotionSnapshotDocument(value),
        welcomeSequence,
      );
      const motion = snapshot.data.entities.find(
        (entity) => entity.components.hill_motion !== undefined,
      )?.components.hill_motion;
      expect(motion).toEqual(value);
      expect(Object.keys(motion ?? {})).toEqual(['velocity']);
      expect(Object.isFrozen(motion)).toBe(true);
      expect(Object.isFrozen(motion?.velocity)).toBe(true);
      expect(snapshot.data.match.mode_state).toEqual(
        hillMotionSnapshotDocument(value).data.match.mode_state,
      );
    },
  );

  it.each(malformedHillMotionComponents)(
    'rejects $name rather than clipping or ignoring it',
    ({ value }) => {
      silenceProtocolWarnings();
      expect(() =>
        validateSessionSnapshotMessage(
          hillMotionSnapshotDocument(value),
          welcomeSequence,
        ),
      ).toThrow(SimulationApiError);
    },
  );
});

describe('movement tuning protocol', () => {
  it.each(TUNING_RESULT_STATUSES)(
    'accepts the closed %s result shape',
    (status) => {
      const snapshot = validateSessionSnapshotMessage(
        tuningSnapshotDocument(status),
        welcomeSequence,
      );
      expect(snapshot.data.tuning_result?.status).toBe(status);
      expect(Object.isFrozen(snapshot.data.match.movement.current)).toBe(true);
    },
  );

  it.each([
    [0, 1],
    [10000, 10000],
  ])(
    'admits the exact acceleration/speed bounds %s/%s',
    (acceleration, speed) => {
      const command = tuningCommand();
      expect(() =>
        validateSessionCommand({
          ...command,
          payload: {
            ...command.payload,
            acceleration_world_units_per_second_squared: acceleration,
            normal_top_speed_world_units_per_second: speed,
          },
        }),
      ).not.toThrow();
    },
  );

  it.each([-1, 10001, Number.NaN, Number.POSITIVE_INFINITY])(
    'rejects invalid acceleration %s',
    (acceleration) => {
      const command = tuningCommand();
      expect(() =>
        validateSessionCommand({
          ...command,
          payload: {
            ...command.payload,
            acceleration_world_units_per_second_squared: acceleration,
          },
        }),
      ).toThrow();
    },
  );

  it.each([0, -1, 10001, Number.NaN, Number.POSITIVE_INFINITY])(
    'rejects invalid normal speed %s',
    (speed) => {
      const command = tuningCommand();
      expect(() =>
        validateSessionCommand({
          ...command,
          payload: {
            ...command.payload,
            normal_top_speed_world_units_per_second: speed,
          },
        }),
      ).toThrow();
    },
  );

  it.each([0, -1, 1.5, Number.MAX_SAFE_INTEGER + 1, '1', null])(
    'rejects malformed request identity %s',
    (requestId) => {
      const command = structuredClone(tuningCommand());
      Reflect.set(command.payload, 'tuning_request_id', requestId);
      expect(() => validateSessionCommand(command)).toThrow();
    },
  );

  it.each(['movement', 'tuning_result'])(
    'requires the new %s member',
    (member) => {
      const document = snapshotDocument();
      Reflect.deleteProperty(
        member === 'movement' ? document.data.match : document.data,
        member,
      );
      expect(() =>
        validateSessionSnapshotMessage(document, welcomeSequence),
      ).toThrow();
    },
  );

  it('requires the exact welcome interval and published intrinsic limits', () => {
    const welcome = welcomeDocument();
    Reflect.deleteProperty(
      welcome.data,
      'movement_tuning_minimum_interval_milliseconds',
    );
    expect(() => validateSessionWelcomeMessage(welcome, null)).toThrow();
    const document = snapshotDocument();
    document.data.match.movement.limits.normal_top_speed_world_units_per_second.maximum = 9999;
    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow();
  });

  it.each(['decision_tick', 'revision', 'retry_after_milliseconds'])(
    'rejects impossible applied %s nullability',
    (field) => {
      const document = tuningSnapshotDocument();
      Reflect.set(
        document.data.tuning_result,
        field,
        field === 'retry_after_milliseconds' ? 1 : null,
      );
      expect(() =>
        validateSessionSnapshotMessage(document, welcomeSequence),
      ).toThrow();
    },
  );

  it.each([null, 0, 501, Number.MAX_SAFE_INTEGER])(
    'rejects an invalid rate retry interval %s',
    (retry) => {
      const document = tuningSnapshotDocument('rate_limited');
      Reflect.set(
        document.data.tuning_result,
        'retry_after_milliseconds',
        retry,
      );
      expect(() =>
        validateSessionSnapshotMessage(document, welcomeSequence),
      ).toThrow();
    },
  );

  it.each(['status', 'decision_tick', 'revision', 'extra'])(
    'rejects unknown or uncovered result %s',
    (field) => {
      const document = tuningSnapshotDocument();
      Reflect.set(
        document.data.tuning_result,
        field,
        field === 'status'
          ? 'unknown'
          : field === 'revision'
            ? 2
            : document.data.tick_sequence + 1,
      );
      expect(() =>
        validateSessionSnapshotMessage(document, welcomeSequence),
      ).toThrow();
    },
  );

  it('requires exact pending correlation and applied revision arithmetic', () => {
    const snapshot = validateSessionSnapshotMessage(
      tuningSnapshotDocument(),
      welcomeSequence,
    );
    expect(() => validateSessionTuningResult(snapshot, null)).toThrow();
    expect(() =>
      validateSessionTuningResult(snapshot, tuningCommand(2).payload),
    ).toThrow();
    expect(() =>
      validateSessionTuningResult(snapshot, tuningCommand(1, 1).payload),
    ).toThrow();
    expect(() =>
      validateSessionTuningResult(
        snapshot,
        tuningCommand(1, Number.MAX_SAFE_INTEGER).payload,
      ),
    ).toThrow();
    expect(
      validateSessionTuningResult(snapshot, tuningCommand().payload)?.status,
    ).toBe('applied');
  });
});

describe('validateSessionWelcomeMessage', () => {
  it('accepts the golden welcome and freezes it deeply', () => {
    const welcome = validateSessionWelcomeMessage(welcomeDocument(), null);

    expect(Object.isFrozen(welcome)).toBe(true);
    expect(Object.isFrozen(welcome.data.accepted_command_kinds)).toBe(true);
    expect(welcome.data.controller_id).toBe(3);
    expect(welcome.meta.message_sequence).toBe(1);
  });

  it('rejects a second welcome on one connection', () => {
    expect(() =>
      validateSessionWelcomeMessage(welcomeDocument(), welcomeSequence),
    ).toThrow(/exactly one welcome/);
  });

  it('refuses the retired session major before welcome publication', () => {
    const document = welcomeDocument();
    document.meta.protocol_version = '2.5';
    expect(() => validateSessionWelcomeMessage(document, null)).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.SESSION_VERSION_UNSUPPORTED',
      }),
    );
  });

  it('fails closed on an unknown advertised command kind', () => {
    const warn = silenceProtocolWarnings();
    const document = welcomeDocument();
    Reflect.set(document.data, 'accepted_command_kinds', ['set_afterburner']);

    expect(() => validateSessionWelcomeMessage(document, null)).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.SESSION_KIND_UNSUPPORTED',
      }),
    );
    expect(warn).toHaveBeenCalledWith(
      expect.stringContaining('set_afterburner'),
    );
  });
});

describe('validateSessionSnapshotMessage', () => {
  it.each([false, true])(
    'resolves an alternate road by exact name regardless of declaration order: selected first %s',
    (selectedFirst) => {
      const document = raceSnapshotDocument();
      document.data.match.mode_state.value.road = 'alternate_road';
      const snapshot = validateSessionSnapshotMessage(document, {
        ...raceSequence,
        terrain: namedRaceTerrain('alternate_road', selectedFirst),
      });
      expect(snapshot.data.match.mode_state.value).toMatchObject({
        road: 'alternate_road',
        checkpoint_radius: 20,
      });
      expect(snapshot.data.match.mode_state.value).not.toHaveProperty('track');
      expect(snapshot.data.match.mode_state.value).not.toHaveProperty(
        'track_half_width',
      );
    },
  );

  it.each([solidTerrain, namedRaceTerrain('alternate_road')])(
    'refuses a road absent from the retained welcome terrain',
    (terrain) => {
      expect(() =>
        validateSessionSnapshotMessage(raceSnapshotDocument(), {
          ...raceSequence,
          terrain,
        }),
      ).toThrow(/does not name a corridor/);
    },
  );

  it('retains the exact selected corridor gate-width boundary', () => {
    const document = raceSnapshotDocument();
    document.data.match.mode_state.value.road = 'alternate_road';
    document.data.match.mode_state.value.checkpoint_radius = 60;
    const sequence = {
      ...raceSequence,
      terrain: namedRaceTerrain('alternate_road'),
    };
    expect(() =>
      validateSessionSnapshotMessage(structuredClone(document), sequence),
    ).not.toThrow();
    document.data.match.mode_state.value.checkpoint_radius = 61;
    expect(() => validateSessionSnapshotMessage(document, sequence)).toThrow(
      /selected terrain corridor half-width/,
    );
    expect(sequence.messageSequence).toBe(1);
    expect(sequence.tickSequence).toBeNull();
  });

  it('accepts a hill frame at the inclusive radius and safe winning-score ceilings', () => {
    const document = maximumHillSnapshotDocument();
    const snapshot = validateSessionSnapshotMessage(document, welcomeSequence);
    expect(
      snapshot.data.entities.find(
        (entity) => entity.components.hill !== undefined,
      )?.components.hill?.radius,
    ).toBe(MAXIMUM_PUBLISHED_WORLD_SCALAR);
    expect(snapshot.data.match.mode_state.value).toMatchObject({
      points_to_win: Number.MAX_SAFE_INTEGER,
    });
  });

  it('accepts a race gate at the inclusive canonical terrain-width ceiling', () => {
    const snapshot = validateSessionSnapshotMessage(
      maximumRaceSnapshotDocument(),
      { ...raceSequence, terrain: maximumRaceTerrain },
    );
    expect(snapshot.data.match.mode_state.value).toMatchObject({
      road: 'road',
      checkpoint_radius: MAXIMUM_TERRAIN_WORLD_SCALAR,
    });
  });

  it('accepts the race course with shared standings or an empty standings array', () => {
    for (const standings of [raceModeStateExample.standings, []]) {
      const state = structuredClone(raceModeStateExample);
      state.standings = standings;
      const snapshot = validateSessionSnapshotMessage(
        raceSnapshotDocument(state),
        raceSequence,
      );
      expect(snapshot.data.match.mode_state.value).toEqual(state);
      expect(Object.isFrozen(snapshot.data.match.mode_state.value)).toBe(true);
      expect(snapshot.data.match.placements).toEqual([]);
    }
  });

  it.each([
    'road',
    'checkpoint_radius',
    'checkpoints',
    'time_limit_ticks',
    'finish_window_ticks',
    'standings',
  ])('rejects a race block missing required member %s', (member) => {
    const document = raceSnapshotDocument();
    Reflect.deleteProperty(document.data.match.mode_state.value, member);
    expect(() =>
      validateSessionSnapshotMessage(document, raceSequence),
    ).toThrow(SimulationApiError);
  });

  it.each([
    ['road', ''],
    ['road', 'UpperCase'],
    ['road', 'a'.repeat(65)],
    ['road', 'contains\u0000nul'],
    ['road', 'café'],
    ['road', 1],
    ['track_half_width', 60],
    ['checkpoint_radius', -1],
    ['track', [{ x: 1, y: 2 }]],
    ['checkpoints', []],
    [
      'track',
      [
        { x: 1, y: 2 },
        { x: 3, y: 4, z: 5 },
      ],
    ],
    ['checkpoints', [{ x: 1, y: Number.POSITIVE_INFINITY }]],
    ['time_limit_ticks', Number.MAX_SAFE_INTEGER + 1],
    ['finish_window_ticks', -1],
    ['unexpected', 1],
  ])('rejects malformed race member %s', (member, value) => {
    const document = raceSnapshotDocument();
    Reflect.set(document.data.match.mode_state.value, String(member), value);
    expect(() =>
      validateSessionSnapshotMessage(document, raceSequence),
    ).toThrow(SimulationApiError);
  });

  it.each([
    ['entity_id', 0],
    ['controller_id', Number.MAX_SAFE_INTEGER + 1],
    ['placement', 0],
    ['placement', 1025],
    ['finished_tick', 0],
    ['finished_tick', 1.5],
    ['finished_tick_offset', -Number.MIN_VALUE],
    ['finished_tick_offset', 1.0000000000000002],
    ['finished_tick_offset', Number.NaN],
    ['finished_tick_offset', '0.25'],
    ['finished_tick_offset', null],
    ['eliminated_tick', 1],
  ])('rejects malformed nested race standing %s', (member, value) => {
    const document = raceSnapshotDocument();
    const standing = document.data.match.mode_state.value.standings[0];
    if (standing === undefined) throw new Error('TEST.RACE_STANDING_MISSING');
    Reflect.set(standing, String(member), value);
    expect(() =>
      validateSessionSnapshotMessage(document, raceSequence),
    ).toThrow(SimulationApiError);
  });

  it.each([0, 0.25, 1])('accepts certified finish offset %s', (offset) => {
    const document = raceSnapshotDocument();
    const standing = document.data.match.mode_state.value.standings[0];
    if (standing === undefined) throw new Error('TEST.RACE_STANDING_MISSING');
    Reflect.set(standing, 'finished_tick_offset', offset);
    expect(() =>
      validateSessionSnapshotMessage(document, raceSequence),
    ).not.toThrow();
  });

  it.each(['ground_bound', 'floating'])(
    'accepts ground attachment %s',
    (attachment) => {
      const document = snapshotDocument();
      const body = playerEntity(document).components.physics_body;
      if (body === undefined) throw new Error('TEST.PLAYER_BODY_MISSING');
      Reflect.set(body, 'ground_attachment', attachment);
      expect(() =>
        validateSessionSnapshotMessage(document, welcomeSequence),
      ).not.toThrow();
    },
  );

  it.each(['bound', '', null, 0, undefined])(
    'rejects invalid ground attachment %s',
    (attachment) => {
      const document = snapshotDocument();
      const body = playerEntity(document).components.physics_body;
      if (body === undefined) throw new Error('TEST.PLAYER_BODY_MISSING');
      Reflect.set(body, 'ground_attachment', attachment);
      expect(() =>
        validateSessionSnapshotMessage(document, welcomeSequence),
      ).toThrow(SimulationApiError);
    },
  );

  it('accepts an ordered race gate count including zero', () => {
    for (const nextCheckpoint of [0, 2]) {
      const document = snapshotDocument();
      Reflect.set(playerEntity(document).components, 'race_progress', {
        next_checkpoint: nextCheckpoint,
      });
      const snapshot = validateSessionSnapshotMessage(
        document,
        welcomeSequence,
      );
      expect(
        snapshot.data.entities.find((entity) => entity.entity_id === 7)
          ?.components.race_progress?.next_checkpoint,
      ).toBe(nextCheckpoint);
    }
  });

  it.each([-1, 0.5, Number.MAX_SAFE_INTEGER + 1])(
    'rejects race progress outside the non-negative safe-integer range: %s',
    (nextCheckpoint) => {
      const document = snapshotDocument();
      Reflect.set(playerEntity(document).components, 'race_progress', {
        next_checkpoint: nextCheckpoint,
      });
      expect(() =>
        validateSessionSnapshotMessage(document, welcomeSequence),
      ).toThrow(SimulationApiError);
    },
  );

  it.each([{}, { next_checkpoint: 1, lap: 2 }])(
    'rejects race progress with missing or extra members: %j',
    (progress) => {
      const document = snapshotDocument();
      Reflect.set(playerEntity(document).components, 'race_progress', progress);
      expect(() =>
        validateSessionSnapshotMessage(document, welcomeSequence),
      ).toThrow(SimulationApiError);
    },
  );

  it('accepts the first snapshot after the welcome and freezes it deeply', () => {
    const snapshot = validateSessionSnapshotMessage(
      snapshotDocument(),
      welcomeSequence,
    );

    expect(Object.isFrozen(snapshot.data.entities)).toBe(true);
    expect(Object.isFrozen(snapshot.data.match.placements)).toBe(true);
    expect(snapshot.meta.message_sequence).toBe(2);
  });

  it('rejects entity identifiers that are not strictly ascending', () => {
    const document = snapshotDocument();
    document.data.entities.reverse();

    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(/strictly ascending/);
  });

  it('rejects a snapshot that arrives before the welcome', () => {
    expect(() =>
      validateSessionSnapshotMessage(snapshotDocument(), null),
    ).toThrow(/before the welcome/);
  });

  it('rejects negative zero that JSON Schema cannot distinguish', () => {
    const document = snapshotDocument();
    const body = playerEntity(document).components.physics_body;
    if (body === undefined) {
      throw new Error('TEST.SNAPSHOT_FIXTURE_BODY_MISSING');
    }
    body.velocity.x = -0;

    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(/negative zero/);
  });

  it('rejects a non-monotonic tick on the same connection', () => {
    const document = snapshotDocument(3);

    expect(() =>
      validateSessionSnapshotMessage(document, {
        messageSequence: 2,
        requestId: document.meta.request_id,
        tickSequence: document.data.tick_sequence,
        npcCatalogue: legacyNpcCatalogue,
        terrain: solidTerrain,
      }),
    ).toThrow(/strictly increase/);
  });

  it('rejects a skipped transport message sequence', () => {
    const document = snapshotDocument(4);

    expect(() =>
      validateSessionSnapshotMessage(document, {
        messageSequence: 2,
        requestId: document.meta.request_id,
        tickSequence: document.data.tick_sequence - 1,
        npcCatalogue: legacyNpcCatalogue,
        terrain: solidTerrain,
      }),
    ).toThrow(/increment exactly once/);
  });

  it('rejects a request identifier change within one connection', () => {
    const document = snapshotDocument();

    expect(() =>
      validateSessionSnapshotMessage(document, {
        messageSequence: 1,
        requestId: 'different-request-id',
        tickSequence: null,
        npcCatalogue: legacyNpcCatalogue,
        terrain: solidTerrain,
      }),
    ).toThrow(/request_id changed/);
  });

  it('fails closed on a component kind the schema set does not name', () => {
    const warn = silenceProtocolWarnings();
    const document = snapshotDocument();
    Reflect.set(firstEntity(document).components, 'gravity_well', {
      strength: 1,
    });

    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.SESSION_KIND_UNSUPPORTED',
      }),
    );
    expect(warn).toHaveBeenCalledWith(expect.stringContaining('gravity_well'));
  });

  it('fails closed on an unregistered mode-state schema id', () => {
    silenceProtocolWarnings();
    const document = snapshotDocument();
    document.data.match.mode_state.schema_id =
      'blob-royale://protocol/v3/mode-state/capture';

    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.SESSION_KIND_UNSUPPORTED',
      }),
    );
  });

  it('fails closed on a protocol minor it cannot decode, before shape validation', () => {
    const document = snapshotDocument();
    // One minor ahead of the active 3.0 contract.
    document.meta.protocol_version = '3.1';
    Reflect.deleteProperty(document.data, 'match');

    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.SESSION_VERSION_UNSUPPORTED',
        context: {
          protocol_version: '3.1',
          supported_protocol_version: SUPPORTED_PROTOCOL_VERSION,
        },
      }),
    );
  });

  it('rejects a frame that carries no protocol version at all', () => {
    const document = snapshotDocument();
    Reflect.deleteProperty(document.meta, 'protocol_version');

    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.SESSION_FRAME_INVALID',
      }),
    );
  });
});

describe('snapshot random draw counts', () => {
  it.each([
    { hazards: 0, hill: 0 },
    { hazards: Number.MAX_SAFE_INTEGER, hill: 0 },
    { hazards: 0, hill: Number.MAX_SAFE_INTEGER },
    { hazards: Number.MAX_SAFE_INTEGER, hill: Number.MAX_SAFE_INTEGER },
  ])('accepts exact safe counts %o and freezes the closed value', (counts) => {
    const document = snapshotDocument();
    document.data.random_draw_counts = counts;
    const snapshot = validateSessionSnapshotMessage(document, welcomeSequence);
    expect(snapshot.data.random_draw_counts).toEqual(counts);
    expect(Object.isFrozen(snapshot.data.random_draw_counts)).toBe(true);
  });

  it('requires the named count object in every snapshot', () => {
    const document = snapshotDocument();
    Reflect.deleteProperty(document.data, 'random_draw_counts');
    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(
      expect.objectContaining({ code: 'SIMULATION.SESSION_FRAME_INVALID' }),
    );
  });

  it('rejects a scalar count even alongside the required named counts', () => {
    const document = snapshotDocument();
    Reflect.set(document.data, 'random_draw_count', 0);
    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(
      expect.objectContaining({ code: 'SIMULATION.SESSION_FRAME_INVALID' }),
    );
  });

  it.each([
    null,
    [],
    0,
    '0',
    { hazards: 0 },
    { hill: 0 },
    { hazards: 0, hill: 0, future_stream: 0 },
  ])('rejects an absent, malformed, or open count set %o', (counts) => {
    const document = snapshotDocument();
    Reflect.set(document.data, 'random_draw_counts', counts);
    expect(() =>
      validateSessionSnapshotMessage(document, welcomeSequence),
    ).toThrow(
      expect.objectContaining({ code: 'SIMULATION.SESSION_FRAME_INVALID' }),
    );
  });

  it.each(['hazards', 'hill'] as const)(
    'rejects invalid %s counts without coercion',
    (stream) => {
      for (const value of [
        -1,
        0.5,
        Number.MAX_SAFE_INTEGER + 1,
        Number.POSITIVE_INFINITY,
        Number.NaN,
        '0',
        true,
        null,
      ]) {
        const document = snapshotDocument();
        Reflect.set(document.data.random_draw_counts, stream, value);
        expect(() =>
          validateSessionSnapshotMessage(document, welcomeSequence),
        ).toThrow(
          expect.objectContaining({ code: 'SIMULATION.SESSION_FRAME_INVALID' }),
        );
      }
    },
  );
});

describe('validateSessionCommand', () => {
  it('accepts the golden command envelope', () => {
    expect(() =>
      validateSessionCommand({
        kind: 'set_thrust',
        payload: sessionCommandEnvelopeExample.payload,
      }),
    ).not.toThrow();
  });

  it('fails closed on a command kind protocol v3 does not register', () => {
    silenceProtocolWarnings();
    const command: SessionCommand = {
      kind: 'set_thrust',
      payload: { x: 0, y: 0 },
    };
    const unregistered: SessionCommand = { ...command, kind: 'brake' as never };

    expect(() => validateSessionCommand(unregistered)).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.SESSION_KIND_UNSUPPORTED',
      }),
    );
  });

  it('rejects a thrust component outside the closed unit interval', () => {
    expect(() =>
      validateSessionCommand({ kind: 'set_thrust', payload: { x: 1.5, y: 0 } }),
    ).toThrow(
      expect.objectContaining<Partial<SimulationApiError>>({
        code: 'SIMULATION.COMMAND_REJECTED',
      }),
    );
  });
});

describe('validateLobbyDirectoryMessage', () => {
  const requestId = lobbyDirectoryMessageExample.meta.request_id;

  it('accepts the golden directory and freezes it deeply', () => {
    const directory = validateLobbyDirectoryMessage(
      structuredClone(lobbyDirectoryMessageExample),
      requestId,
    );

    expect(directory.data.lobbies.map((listing) => listing.lobby_id)).toEqual([
      1, 2,
    ]);
    expect(Object.isFrozen(directory)).toBe(true);
    expect(Object.isFrozen(directory.data.lobbies)).toBe(true);
    expect(Object.isFrozen(directory.data.lobbies[0])).toBe(true);
  });

  it('rejects a response whose X-Request-ID does not echo the envelope', () => {
    expect(() =>
      validateLobbyDirectoryMessage(
        structuredClone(lobbyDirectoryMessageExample),
        'another-request',
      ),
    ).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.LOBBY_DIRECTORY_RESPONSE_INVALID',
      }) as SimulationApiError,
    );
  });

  it('rejects rooms that are not numbered 1..N in order', () => {
    const document = structuredClone(lobbyDirectoryMessageExample);
    const second = document.data.lobbies[1];
    if (second === undefined) {
      throw new Error('TEST.DIRECTORY_FIXTURE_TOO_SHORT');
    }
    second.lobby_id = 3;

    expect(() => validateLobbyDirectoryMessage(document, requestId)).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.LOBBY_DIRECTORY_RESPONSE_INVALID',
        context: { actual_lobby_id: 3, position: 2 },
      }) as SimulationApiError,
    );
  });

  it('fails closed on a newer protocol minor before shape validation', () => {
    const document = structuredClone(lobbyDirectoryMessageExample);
    // One minor ahead of the supported set, moved with every minor as the session case above is.
    document.meta.protocol_version = '3.1';

    expect(() => validateLobbyDirectoryMessage(document, requestId)).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.SESSION_VERSION_UNSUPPORTED',
      }) as SimulationApiError,
    );
  });

  it('rejects a listing carrying a member the closed schema does not name', () => {
    const document = structuredClone(lobbyDirectoryMessageExample);
    const first: Record<string, unknown> = document.data.lobbies[0] ?? {};
    first.spectator_count = 1;

    expect(() => validateLobbyDirectoryMessage(document, requestId)).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.LOBBY_DIRECTORY_RESPONSE_INVALID',
      }) as SimulationApiError,
    );
  });
});

describe('validateSessionHttpErrorResponse', () => {
  const requestId = sessionErrorResponseExample.meta.request_id;

  function lobbyFullEnvelope() {
    return {
      ...structuredClone(sessionErrorResponseExample),
      error: {
        code: 'LOBBY.FULL',
        details: { lobby_id: 2 },
        message:
          'Every seat in this lobby is taken; read the directory and choose another.',
        retryable: true,
      },
    };
  }

  it('accepts the golden v3 failure envelope and a lobby refusal at their registered statuses', () => {
    const forwarded = validateSessionHttpErrorResponse(
      structuredClone(sessionErrorResponseExample),
      400,
      requestId,
    );
    expect(forwarded.error.code).toBe('PROTOCOL.INVALID_FORWARDED_CLIENT');
    expect(Object.isFrozen(forwarded.error)).toBe(true);

    const full = validateSessionHttpErrorResponse(
      lobbyFullEnvelope(),
      409,
      requestId,
    );
    expect(full.error.details.lobby_id).toBe(2);
    expect(full.error.retryable).toBe(true);
  });

  it('rejects a status or a retryable flag that disagrees with the code', () => {
    expect(() =>
      validateSessionHttpErrorResponse(lobbyFullEnvelope(), 404, requestId),
    ).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.HTTP_ERROR_RESPONSE_INVALID',
      }) as SimulationApiError,
    );

    const envelope = lobbyFullEnvelope();
    envelope.error.retryable = false;
    expect(() =>
      validateSessionHttpErrorResponse(envelope, 409, requestId),
    ).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.HTTP_ERROR_RESPONSE_INVALID',
      }) as SimulationApiError,
    );
  });

  it('accepts only the fixed nonretryable v3 upgrade-required contract at 426', () => {
    const document = {
      ...structuredClone(sessionErrorResponseExample),
      error: {
        code: 'PROTOCOL.SESSION_VERSION_UPGRADE_REQUIRED',
        details: { required_protocol_version: '3.0' },
        message: 'This session requires protocol 3.0.',
        retryable: false,
      },
    };
    expect(
      validateSessionHttpErrorResponse(
        structuredClone(document),
        426,
        requestId,
      ).error.retryable,
    ).toBe(false);
    expect(() =>
      validateSessionHttpErrorResponse(document, 400, requestId),
    ).toThrow();
    document.error.retryable = true;
    expect(() =>
      validateSessionHttpErrorResponse(document, 426, requestId),
    ).toThrow();
    document.error.retryable = false;
    document.error.details.required_protocol_version = '3.1';
    expect(() =>
      validateSessionHttpErrorResponse(document, 426, requestId),
    ).toThrow();
  });

  it('rejects an envelope whose X-Request-ID does not echo', () => {
    expect(() =>
      validateSessionHttpErrorResponse(
        lobbyFullEnvelope(),
        409,
        'another-request',
      ),
    ).toThrow(
      expect.objectContaining({
        code: 'SIMULATION.HTTP_ERROR_RESPONSE_INVALID',
      }) as SimulationApiError,
    );
  });
});

describe('validateSessionCommand for the lobby kinds', () => {
  it('accepts every lobby command at its closed shape', () => {
    const commands: readonly SessionCommand[] = [
      { kind: 'set_seat_count', payload: { seat_count: 4 } },
      { kind: 'seat_npc', payload: { npc_kind: 'wanderer', seat_index: 3 } },
      { kind: 'clear_seat', payload: { seat_index: 1 } },
      { kind: 'start_match', payload: {} },
    ];
    for (const command of commands) {
      expect(() => {
        validateSessionCommand(command, legacyNpcCatalogue);
      }).not.toThrow();
    }
  });

  it('refuses a lobby command outside its schema bound before it reaches the wire', () => {
    // Each of these is a value the published contract does not describe, which the server would
    // answer with `command_payload_invalid` and a closed connection.
    const commands: readonly unknown[] = [
      { kind: 'set_seat_count', payload: { seat_count: 0 } },
      { kind: 'set_seat_count', payload: { seat_count: 65 } },
      { kind: 'set_seat_count', payload: { seat_count: 2.5 } },
      { kind: 'seat_npc', payload: { npc_kind: 'Wanderer', seat_index: 0 } },
      { kind: 'seat_npc', payload: { npc_kind: 'wanderer', seat_index: 64 } },
      { kind: 'clear_seat', payload: { seat_index: -1 } },
      { kind: 'start_match', payload: { now: true } },
    ];
    for (const command of commands) {
      expect(() => {
        validateSessionCommand(command as SessionCommand);
      }).toThrow(
        expect.objectContaining({
          code: 'SIMULATION.COMMAND_REJECTED',
        }) as SimulationApiError,
      );
    }
  });
});
