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
