import { Fragment, useId } from 'react';

import type { MovementTuningControls } from './useMovementTuning';

export interface MovementTuningPanelProps {
  readonly controls: MovementTuningControls;
}

/** Accessible room-wide tuning; all drafts and submissions have one owner in useMovementTuning. */
export function MovementTuningPanel({ controls }: MovementTuningPanelProps) {
  const headingId = useId();
  const fieldsId = useId();
  const validationId = useId();
  const authorityId = useId();
  if (!controls.visible) return null;
  const movement = controls.authoritative;
  const fields =
    movement === null
      ? []
      : [
          {
            key: 'acceleration',
            label: 'Acceleration (wu/s²)',
            sliderLabel: 'Acceleration slider',
            value: controls.acceleration,
            edit: controls.editAcceleration,
            limits: movement.limits.acceleration_world_units_per_second_squared,
            unavailableMessage: null,
          },
          {
            key: 'normal-speed',
            label: 'Normal top speed (wu/s)',
            sliderLabel: 'Normal top speed slider',
            value: controls.normalTopSpeed,
            edit: controls.editNormalTopSpeed,
            limits: movement.limits.normal_top_speed_world_units_per_second,
            unavailableMessage: null,
          },
          {
            key: 'charge-boost',
            label: 'Charge boost',
            sliderLabel: 'Charge boost slider',
            value: controls.chargeSpeedFraction,
            edit: controls.editChargeSpeedFraction,
            limits: movement.limits.charge_speed_fraction,
            unavailableMessage: 'Charge is unavailable in this room.',
          },
          {
            key: 'lethal-rate',
            label: 'Dangerous objects per second',
            sliderLabel: 'Dangerous objects per second slider',
            value: controls.lethalSpawnRate,
            edit: controls.editLethalSpawnRate,
            limits: movement.limits.lethal_spawn_rate_per_second,
            unavailableMessage:
              'This room has no dangerous crossing objects configured.',
          },
          {
            key: 'nonlethal-rate',
            label: 'Non-dangerous objects per second',
            sliderLabel: 'Non-dangerous objects per second slider',
            value: controls.nonlethalSpawnRate,
            edit: controls.editNonlethalSpawnRate,
            limits: movement.limits.nonlethal_spawn_rate_per_second,
            unavailableMessage:
              'This room has no non-dangerous crossing objects configured.',
          },
        ];
  const draftChargeBoost =
    controls.chargeSpeedFraction.trim() === '' ||
    controls.normalTopSpeed.trim() === ''
      ? null
      : Number(controls.chargeSpeedFraction) * Number(controls.normalTopSpeed);

  return (
    <section
      className="MovementTuningPanel"
      aria-labelledby={headingId}
      data-gameplay-input="blocked"
    >
      <h3 id={headingId}>Room tuning</h3>
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
            aria-label="Authoritative room values"
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
              <dt>Current charge boost</dt>
              <dd>
                {movement.current.charge_speed_fraction *
                  movement.current.normal_top_speed_world_units_per_second}{' '}
                wu/s added
              </dd>
            </div>
            <div>
              <dt>Current dangerous objects per second</dt>
              <dd>{movement.current.lethal_spawn_rate_per_second}</dd>
            </div>
            <div>
              <dt>Current non-dangerous objects per second</dt>
              <dd>{movement.current.nonlethal_spawn_rate_per_second}</dd>
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
                ·{' '}
                {movement.defaults.charge_speed_fraction *
                  movement.defaults
                    .normal_top_speed_world_units_per_second}{' '}
                wu/s charge boost ·{' '}
                {movement.defaults.lethal_spawn_rate_per_second} dangerous/s ·{' '}
                {movement.defaults.nonlethal_spawn_rate_per_second}{' '}
                non-dangerous/s
              </dd>
            </div>
          </dl>
          <fieldset
            disabled={!controls.canEdit}
            className="MovementTuningFields"
            aria-describedby={`${authorityId} ${validationId}`}
          >
            <legend>Local draft</legend>
            {fields.map((field) => {
              const unavailable = field.limits.minimum === field.limits.maximum;
              const id = `${fieldsId}-${field.key}`;
              return (
                <Fragment key={field.key}>
                  <label htmlFor={id}>{field.label}</label>
                  <input
                    id={id}
                    type="number"
                    step="any"
                    min={field.limits.minimum}
                    max={field.limits.maximum}
                    value={field.value}
                    disabled={unavailable}
                    aria-invalid={controls.validationMessage !== null}
                    aria-describedby={
                      field.key === 'charge-boost'
                        ? `${fieldsId}-charge-hint`
                        : undefined
                    }
                    onChange={(event) => field.edit(event.target.value)}
                  />
                  <input
                    aria-label={field.sliderLabel}
                    type="range"
                    step="any"
                    min={field.limits.minimum}
                    max={field.limits.maximum}
                    value={field.value}
                    disabled={
                      unavailable || controls.validationMessage !== null
                    }
                    aria-describedby={
                      field.key === 'charge-boost'
                        ? `${fieldsId}-charge-hint`
                        : undefined
                    }
                    onChange={(event) => field.edit(event.target.value)}
                  />
                  {unavailable && field.unavailableMessage !== null ? (
                    <p className="MovementTuningHint">
                      {field.unavailableMessage}
                    </p>
                  ) : null}
                  {field.key === 'charge-boost' ? (
                    <p
                      id={`${fieldsId}-charge-hint`}
                      className="MovementTuningHint"
                    >
                      {draftChargeBoost !== null &&
                      Number.isFinite(draftChargeBoost) &&
                      controls.validationMessage === null
                        ? `Draft charge adds ${draftChargeBoost} wu/s to velocity.`
                        : 'Enter valid charge and normal speed values to see the added speed.'}{' '}
                      Boost is a fraction of normal top speed. Zero disables
                      charge.
                    </p>
                  ) : null}
                </Fragment>
              );
            })}
            <p className="MovementTuningHint">
              Objects arrive at random. Rates are averages while the room has
              capacity for new objects. Rate changes affect future spawns; set a
              rate to zero to stop those new spawns.
            </p>
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
          Apply room tuning
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
