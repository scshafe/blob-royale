import {
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
