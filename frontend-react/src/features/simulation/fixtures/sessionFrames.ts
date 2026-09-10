import {
  raceModeStateExample,
  sessionSnapshotMessageExample,
  sessionWelcomeMessageExample,
} from './protocolV2Examples';

export type MutableWelcomeDocument = typeof sessionWelcomeMessageExample;
export type MutableSnapshotDocument = typeof sessionSnapshotMessageExample;

/** A deep-mutable copy of the golden welcome, ready to be perturbed by one test. */
export function welcomeDocument(): MutableWelcomeDocument {
  return structuredClone(sessionWelcomeMessageExample);
}

/**
 * A deep-mutable copy of the golden snapshot renumbered to the first snapshot of a session: the
 * welcome is always message one, so the first snapshot a client accepts is always message two.
 */
export function snapshotDocument(messageSequence = 2): MutableSnapshotDocument {
  const document = structuredClone(sessionSnapshotMessageExample);
  document.meta.message_sequence = messageSequence;
  return document;
}

/** The entity of the golden snapshot that carries a body, a controller, and an exposure. */
export function playerEntity(document: MutableSnapshotDocument) {
  const entity = document.data.entities.find(
    (candidate) => candidate.entity_id === 7,
  );
  if (entity === undefined) {
    throw new Error('TEST.SNAPSHOT_FIXTURE_PLAYER_MISSING');
  }
  return entity;
}

export function firstEntity(document: MutableSnapshotDocument) {
  const entity = document.data.entities[0];
  if (entity === undefined) {
    throw new Error('TEST.SNAPSHOT_FIXTURE_EMPTY');
  }
  return entity;
}

/**
 * A complete hill frame, built rather than recorded: the golden snapshot is a royale frame, and a
 * hill frame differs in its mode-state block and in the kinds its entities carry. Entity 3 is the
 * hill, entity 7 the session's own blob holding it (120 of the 400 ticks toward a point), entity 8 a
 * bot leading the board, and entity 10 a bot knocked out and waiting 300 ticks for a seat. The
 * running phase started 2,000 ticks ago against a 96,000-tick limit. It is validated by every test
 * that reads it, so the 2.5 schemas accept it or the tests do not start.
 */
export function hillSnapshotDocument(messageSequence = 2) {
  const golden = snapshotDocument(messageSequence);
  return {
    data: {
      tick_sequence: 12904,
      entities: [
        {
          entity_id: 3,
          components: {
            hill: { center: { x: 480, y: 320 }, radius: 90 },
          },
        },
        {
          entity_id: 7,
          components: {
            controllable: {
              controller_id: 3,
              controller_kind: 'session',
              display_name: 'Cole Shaffer',
            },
            hill_presence: { inside_ticks: 120 },
            physics_body: {
              position: { x: 500, y: 330 },
              velocity: { x: 0, y: 0 },
              acceleration: { x: 0, y: 0 },
              radius: 10,
              mass: 1,
              collision_layer: 1,
              collision_mask: 3,
              is_static: false,
            },
            score: { points: 4 },
          },
        },
        {
          entity_id: 8,
          components: {
            controllable: {
              controller_id: 4,
              controller_kind: 'wanderer',
              display_name: 'wanderer-1',
            },
            physics_body: {
              position: { x: 760.5, y: 512.25 },
              velocity: { x: -6.25, y: 31.5 },
              acceleration: { x: 0, y: -400 },
              radius: 10,
              mass: 1,
              collision_layer: 1,
              collision_mask: 3,
              is_static: false,
            },
            score: { points: 6 },
          },
        },
        {
          entity_id: 10,
          components: {
            controllable: {
              controller_id: 5,
              controller_kind: 'chaser',
              display_name: 'chaser-2',
            },
            respawn_timer: { ticks_remaining: 300 },
            score: { points: 2 },
          },
        },
      ],
      match: {
        mode: 'king_of_the_hill',
        phase: 'running',
        phase_started_tick: 10904,
        seats: [
          { kind: 'controller', controller_id: 3, npc_kind: null },
          { kind: 'npc', controller_id: 4, npc_kind: 'wanderer' },
          { kind: 'npc', controller_id: 5, npc_kind: 'chaser' },
        ],
        start_requested: true,
        outcome: {
          kind: 'none',
          winner_entity_id: null,
          winner_team_id: null,
        },
        placements: [],
        mode_state: {
          schema_id: 'blob-royale://protocol/v2/mode-state/king-of-the-hill',
          value: {
            points_to_win: 30,
            point_interval_ticks: 400,
            time_limit_ticks: 96000,
          },
        },
      },
    },
    error: null,
    meta: golden.meta,
  };
}

/**
 * A running race on the published example course: the local racer has taken one of three gates,
 * racer-1 has taken two, and racer-2 has lost its body and is waiting 300 ticks to return to gate
 * one. Every consumer validates this complete document against the snapshot schema before use.
 */
export function raceSnapshotDocument(messageSequence = 2) {
  const golden = snapshotDocument(messageSequence);
  const hill = hillSnapshotDocument(messageSequence);
  return {
    data: {
      tick_sequence: 12904,
      entities: hill.data.entities
        .filter((entity) => entity.components.controllable !== undefined)
        .map((entity) => ({
          entity_id: entity.entity_id,
          components: {
            controllable: entity.components.controllable,
            ...(entity.components.physics_body === undefined
              ? {}
              : { physics_body: entity.components.physics_body }),
            ...(entity.components.respawn_timer === undefined
              ? {}
              : { respawn_timer: entity.components.respawn_timer }),
            race_progress: { next_checkpoint: entity.entity_id === 8 ? 2 : 1 },
          },
        })),
      match: {
        ...hill.data.match,
        mode: 'race',
        outcome: {
          kind: 'none',
          winner_entity_id: null as number | null,
          winner_team_id: null as number | null,
        },
        mode_state: {
          schema_id: 'blob-royale://protocol/v2/mode-state/race',
          value: {
            ...structuredClone(raceModeStateExample),
            standings: [] as typeof raceModeStateExample.standings,
          },
        },
      },
    },
    error: null,
    meta: golden.meta,
  };
}

/** Exact binary64-safe publication ceilings shared with simulation_limits.hpp, not balance defaults. */
export const MAXIMUM_PUBLISHED_WORLD_SCALAR = 1_000_000_000_000;

/** A hill frame at the inclusive radius and winning-score publication ceilings. */
export function maximumHillSnapshotDocument() {
  const document = hillSnapshotDocument();
  const hill = document.data.entities.find(
    (entity) => entity.components.hill !== undefined,
  )?.components.hill;
  if (hill === undefined) {
    throw new Error('TEST.HILL_FIXTURE_HILL_MISSING');
  }
  hill.radius = MAXIMUM_PUBLISHED_WORLD_SCALAR;
  document.data.match.mode_state.value.points_to_win = Number.MAX_SAFE_INTEGER;
  return document;
}

/** A race frame at both inclusive dimension publication ceilings; the gate fits the corridor. */
export function maximumRaceSnapshotDocument() {
  const document = raceSnapshotDocument();
  document.data.match.mode_state.value.track_half_width =
    MAXIMUM_PUBLISHED_WORLD_SCALAR;
  document.data.match.mode_state.value.checkpoint_radius =
    MAXIMUM_PUBLISHED_WORLD_SCALAR;
  return document;
}

export type RaceSnapshotScenario =
  | 'running'
  | 'finish_window'
  | 'finish_window_expired'
  | 'finished_respawning'
  | 'own_finish_after_wipe'
  | 'tied_finish'
  | 'awaiting_checkpoint'
  | 'awaiting_grid'
  | 'clock_win'
  | 'clock_draw'
  | 'empty_draw'
  | 'empty_draw_after_finish'
  | 'countdown';

/** Named lifecycle cases built on one race fixture; outcomes and standings remain wire-shaped. */
export function raceScenarioDocument(scenario: RaceSnapshotScenario) {
  const document = raceSnapshotDocument();
  const { match } = document.data;
  const ownEntity = document.data.entities.find(
    (entity) => entity.entity_id === 7,
  );
  const returningEntity = document.data.entities.find(
    (entity) => entity.entity_id === 10,
  );
  if (ownEntity === undefined || returningEntity === undefined) {
    throw new Error('TEST.RACE_FIXTURE_PARTICIPANTS_MISSING');
  }
  if (scenario === 'awaiting_checkpoint' || scenario === 'awaiting_grid') {
    delete returningEntity.components.respawn_timer;
    returningEntity.components.race_progress.next_checkpoint =
      scenario === 'awaiting_grid' ? 0 : 1;
  }
  if (
    scenario === 'finish_window' ||
    scenario === 'finish_window_expired' ||
    scenario === 'finished_respawning' ||
    scenario === 'empty_draw_after_finish' ||
    scenario === 'own_finish_after_wipe' ||
    scenario === 'tied_finish'
  ) {
    ownEntity.components.race_progress.next_checkpoint = 3;
    match.mode_state.value.standings.push({
      entity_id: 7,
      controller_id: 3,
      placement: 1,
      finished_tick: 12504,
    });
  }
  if (scenario === 'finish_window_expired') {
    document.data.tick_sequence = 14505;
  }
  if (scenario === 'finished_respawning') {
    delete ownEntity.components.physics_body;
    ownEntity.components.respawn_timer = { ticks_remaining: 300 };
  }
  if (scenario === 'own_finish_after_wipe') {
    document.data.entities = document.data.entities.filter(
      (entity) => entity.entity_id !== 7,
    );
    match.phase = 'ended';
    match.phase_started_tick = document.data.tick_sequence;
    match.outcome = {
      kind: 'won_by_entity',
      winner_entity_id: 7,
      winner_team_id: null,
    };
  }
  if (scenario === 'tied_finish') {
    match.mode_state.value.standings.push({
      entity_id: 8,
      controller_id: 4,
      placement: 1,
      finished_tick: 12504,
    });
    match.phase = 'ended';
    match.phase_started_tick = document.data.tick_sequence;
    match.outcome.kind = 'drawn';
  }
  if (
    scenario === 'clock_win' ||
    scenario === 'clock_draw' ||
    scenario === 'empty_draw'
  ) {
    document.data.tick_sequence = 106904;
    match.phase = 'ended';
    match.phase_started_tick = document.data.tick_sequence;
    if (scenario === 'clock_win') {
      match.outcome = {
        kind: 'won_by_entity',
        winner_entity_id: 8,
        winner_team_id: null,
      };
    } else {
      match.outcome.kind = 'drawn';
      ownEntity.components.race_progress.next_checkpoint = 2;
    }
  }
  if (scenario === 'empty_draw' || scenario === 'empty_draw_after_finish') {
    document.data.entities = [];
    match.phase = 'ended';
    match.phase_started_tick = document.data.tick_sequence;
    match.outcome.kind = 'drawn';
  }
  if (scenario === 'countdown') {
    match.phase = 'countdown';
  }
  return document;
}
