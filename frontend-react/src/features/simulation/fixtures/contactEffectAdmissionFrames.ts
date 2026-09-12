import { playerEntity, snapshotDocument } from './sessionFrames';

export const ANY_TOUCH_ADMISSION = Object.freeze({ policy: 'any_touch' });

/** Unknown boundary values deliberately bypass static types to exercise closed admission. */
export function contactEffectAdmissionSnapshotDocument(admission?: unknown) {
  const document = snapshotDocument();
  if (admission !== undefined) {
    Reflect.set(
      playerEntity(document).components,
      'contact_effect_admission',
      admission,
    );
  }
  return document;
}

export const INVALID_CONTACT_EFFECT_ADMISSIONS = Object.freeze([
  { name: 'null component', value: null },
  { name: 'missing policy', value: {} },
  { name: 'null policy', value: { policy: null } },
  { name: 'redundant default', value: { policy: 'closing_impact' } },
  { name: 'unknown policy', value: { policy: 'center_touch' } },
  { name: 'wrong case', value: { policy: 'ANY_TOUCH' } },
  { name: 'number policy', value: { policy: 1 } },
  { name: 'extra member', value: { policy: 'any_touch', radius: 1 } },
  { name: 'array', value: ['any_touch'] },
]);
