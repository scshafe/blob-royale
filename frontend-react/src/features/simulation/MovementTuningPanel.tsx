import { useId } from 'react';

import type { MovementTuningControls } from './useMovementTuning';

export interface MovementTuningPanelProps {
  readonly controls: MovementTuningControls;
}

/** Accessible room-wide tuning; all drafts and submissions have one owner in useMovementTuning. */
export function MovementTuningPanel({ controls }: MovementTuningPanelProps) {
  const headingId = useId();
  const accelerationId = useId();
  const speedId = useId();
  const validationId = useId();
  const authorityId = useId();
  if (!controls.visible) return null;
  const movement = controls.authoritative;
  return (
    <section
      className="MovementTuningPanel"
      aria-labelledby={headingId}
      data-gameplay-input="blocked"
    >
      <h3 id={headingId}>Movement tuning</h3>
      <p className="MovementTuningHint">
        Shared by everyone in this room. Normal top speed limits propulsion, not
        collision momentum.
      </p>
      {movement === null ? (
        <p id={authorityId}>Waiting for current room values.</p>
      ) : (
        <>
          <dl
            className="MovementTuningValues"
            id={authorityId}
            aria-label="Authoritative movement values"
          >
            <div>
              <dt>Current acceleration</dt>
              <dd>
                {movement.current.acceleration_world_units_per_second_squared}{' '}
                wu/s²
              </dd>
            </div>
            <div>
              <dt>Current normal top speed</dt>
              <dd>
                {movement.current.normal_top_speed_world_units_per_second} wu/s
              </dd>
            </div>
            <div>
              <dt>Revision</dt>
              <dd>{movement.revision}</dd>
            </div>
            <div>
              <dt>Effective tick</dt>
              <dd>{movement.effective_tick}</dd>
            </div>
            <div>
              <dt>Authored defaults</dt>
              <dd>
                {movement.defaults.acceleration_world_units_per_second_squared}{' '}
                wu/s² ·{' '}
                {movement.defaults.normal_top_speed_world_units_per_second} wu/s
              </dd>
            </div>
          </dl>
          <fieldset
            disabled={!controls.canEdit}
            className="MovementTuningFields"
            aria-describedby={`${authorityId} ${validationId}`}
          >
            <legend>Local draft</legend>
            <label htmlFor={accelerationId}>Acceleration (wu/s²)</label>
            <input
              id={accelerationId}
              type="number"
              step="any"
              min={
                movement.limits.acceleration_world_units_per_second_squared
                  .minimum
              }
              max={
                movement.limits.acceleration_world_units_per_second_squared
                  .maximum
              }
              value={controls.acceleration}
              aria-invalid={controls.validationMessage !== null}
              onChange={(event) =>
                controls.editAcceleration(event.target.value)
              }
            />
            <input
              aria-label="Acceleration slider"
              type="range"
              step="any"
              min={
                movement.limits.acceleration_world_units_per_second_squared
                  .minimum
              }
              max={
                movement.limits.acceleration_world_units_per_second_squared
                  .maximum
              }
              value={controls.acceleration}
              disabled={controls.validationMessage !== null}
              onChange={(event) =>
                controls.editAcceleration(event.target.value)
              }
            />
            <label htmlFor={speedId}>Normal top speed (wu/s)</label>
            <input
              id={speedId}
              type="number"
              step="any"
              min={
                movement.limits.normal_top_speed_world_units_per_second.minimum
              }
              max={
                movement.limits.normal_top_speed_world_units_per_second.maximum
              }
              value={controls.normalTopSpeed}
              aria-invalid={controls.validationMessage !== null}
              onChange={(event) =>
                controls.editNormalTopSpeed(event.target.value)
              }
            />
            <input
              aria-label="Normal top speed slider"
              type="range"
              step="any"
              min={
                movement.limits.normal_top_speed_world_units_per_second.minimum
              }
              max={
                movement.limits.normal_top_speed_world_units_per_second.maximum
              }
              value={controls.normalTopSpeed}
              disabled={controls.validationMessage !== null}
              onChange={(event) =>
                controls.editNormalTopSpeed(event.target.value)
              }
            />
          </fieldset>
        </>
      )}
      <p id={validationId} className="MovementTuningHint">
        {controls.validationMessage ??
          (controls.dirty
            ? 'Unsubmitted local edits.'
            : 'Draft matches the current room values.')}
      </p>
      {controls.needsReview ? (
        <div className="MovementTuningReview">
          <p>
            Your draft is kept. Review the current values above before applying
            against the latest revision.
          </p>
          <button
            type="button"
            disabled={!controls.canReview}
            onClick={controls.reviewCurrent}
          >
            Review current values; keep draft
          </button>
        </div>
      ) : null}
      <div className="MovementTuningActions">
        <button
          type="button"
          disabled={controls.applyDisabledReason !== null}
          onClick={controls.apply}
        >
          Apply movement tuning
        </button>
        <button
          type="button"
          disabled={controls.resetDisabledReason !== null}
          onClick={controls.reset}
        >
          Reset to authored defaults
        </button>
      </div>
      {controls.applyDisabledReason === null ? null : (
        <p className="MovementTuningHint">{controls.applyDisabledReason}</p>
      )}
      {controls.localSubmissionMessage === null ? null : (
        <p role="alert">{controls.localSubmissionMessage}</p>
      )}
      <p
        role="status"
        aria-live="polite"
        className={`MovementTuningOutcome MovementTuningOutcome-${controls.outcome.kind}`}
      >
        {controls.outcome.message}
      </p>
    </section>
  );
}
