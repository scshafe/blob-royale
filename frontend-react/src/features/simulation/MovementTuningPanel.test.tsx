import { fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { MovementTuningPanel } from './MovementTuningPanel';
import {
  movementTuningConnection,
  pendingTuning,
  resolvedTuning,
  TUNING_UI_CURRENT,
  TUNING_UI_DEFAULTS,
  TUNING_UI_DRAFT,
  TUNING_UI_PEER,
  TUNING_UI_REVISION,
  withMovement,
} from './fixtures/movementTuningControlsFrames';
import type {
  SessionCommand,
  SessionSetMovementTuningCommand,
} from './simulationProtocolTypes';
import type { SimulationConnection } from './useSimulationConnection';
import { useMovementTuning } from './useMovementTuning';

function PanelHarness({
  connection,
}: {
  readonly connection: SimulationConnection;
}) {
  const controls = useMovementTuning({
    lobbyId: connection.session?.lobbyId ?? null,
    connection,
  });
  return <MovementTuningPanel controls={controls} />;
}

function panelConnection() {
  const sender = vi.fn((command: SessionCommand) => {
    void command;
    return true;
  });
  return { sender, connection: movementTuningConnection(sender) };
}

function tuningCommand(): SessionSetMovementTuningCommand {
  return {
    kind: 'set_movement_tuning',
    payload: {
      ...TUNING_UI_DRAFT,
      tuning_request_id: 1,
      expected_revision: TUNING_UI_REVISION,
    },
  };
}

function editDraft() {
  fireEvent.change(
    screen.getByRole('spinbutton', { name: 'Acceleration (wu/s²)' }),
    {
      target: {
        value: String(
          TUNING_UI_DRAFT.acceleration_world_units_per_second_squared,
        ),
      },
    },
  );
  fireEvent.change(
    screen.getByRole('slider', { name: 'Normal top speed slider' }),
    {
      target: {
        value: String(TUNING_UI_DRAFT.normal_top_speed_world_units_per_second),
      },
    },
  );
  fireEvent.change(
    screen.getByRole('slider', { name: 'Charge boost slider' }),
    {
      target: { value: String(TUNING_UI_DRAFT.charge_speed_fraction) },
    },
  );
  fireEvent.change(
    screen.getByRole('spinbutton', { name: 'Dangerous objects per second' }),
    {
      target: { value: String(TUNING_UI_DRAFT.lethal_spawn_rate_per_second) },
    },
  );
  fireEvent.change(
    screen.getByRole('slider', {
      name: 'Non-dangerous objects per second slider',
    }),
    {
      target: {
        value: String(TUNING_UI_DRAFT.nonlethal_spawn_rate_per_second),
      },
    },
  );
}

afterEach(() => {
  vi.useRealTimers();
});

describe('MovementTuningPanel', () => {
  it('labels all five numeric and range controls with published values, limits, and defaults', () => {
    const { connection } = panelConnection();
    render(<PanelHarness connection={connection} />);
    const panel = screen.getByRole('region', { name: 'Room tuning' });
    expect(panel).toHaveAttribute('data-gameplay-input', 'blocked');
    const acceleration = within(panel).getByRole('spinbutton', {
      name: 'Acceleration (wu/s²)',
    });
    const speed = within(panel).getByRole('spinbutton', {
      name: 'Normal top speed (wu/s)',
    });
    expect(acceleration).toHaveValue(
      TUNING_UI_CURRENT.acceleration_world_units_per_second_squared,
    );
    expect(speed).toHaveValue(
      TUNING_UI_CURRENT.normal_top_speed_world_units_per_second,
    );
    expect(acceleration).toHaveAttribute(
      'min',
      String(
        connection.match?.movement.limits
          .acceleration_world_units_per_second_squared.minimum,
      ),
    );
    expect(acceleration).toHaveAttribute(
      'max',
      String(
        connection.match?.movement.limits
          .acceleration_world_units_per_second_squared.maximum,
      ),
    );
    expect(speed).toHaveAttribute(
      'min',
      String(
        connection.match?.movement.limits
          .normal_top_speed_world_units_per_second.minimum,
      ),
    );
    expect(speed).toHaveAttribute(
      'max',
      String(
        connection.match?.movement.limits
          .normal_top_speed_world_units_per_second.maximum,
      ),
    );
    expect(
      within(panel).getByRole('slider', { name: 'Acceleration slider' }),
    ).toHaveValue(
      String(TUNING_UI_CURRENT.acceleration_world_units_per_second_squared),
    );
    expect(
      within(panel).getByRole('slider', { name: 'Normal top speed slider' }),
    ).toHaveValue(
      String(TUNING_UI_CURRENT.normal_top_speed_world_units_per_second),
    );
    expect(within(panel).getAllByRole('spinbutton')).toHaveLength(5);
    expect(within(panel).getAllByRole('slider')).toHaveLength(5);
    for (const field of [
      { name: 'Charge boost', wire: 'charge_speed_fraction' },
      {
        name: 'Dangerous objects per second',
        wire: 'lethal_spawn_rate_per_second',
      },
      {
        name: 'Non-dangerous objects per second',
        wire: 'nonlethal_spawn_rate_per_second',
      },
    ] as const) {
      const numeric = within(panel).getByRole('spinbutton', {
        name: field.name,
      });
      const range = within(panel).getByRole('slider', {
        name: `${field.name} slider`,
      });
      expect(numeric).toHaveValue(TUNING_UI_CURRENT[field.wire]);
      expect(range).toHaveValue(String(TUNING_UI_CURRENT[field.wire]));
      expect(numeric).toHaveAttribute(
        'max',
        String(connection.match?.movement.limits[field.wire].maximum),
      );
      expect(range).toHaveAttribute(
        'max',
        String(connection.match?.movement.limits[field.wire].maximum),
      );
    }
    expect(
      within(panel).getByLabelText('Authoritative room values'),
    ).toHaveTextContent(
      `${TUNING_UI_DEFAULTS.acceleration_world_units_per_second_squared} wu/s² · ${TUNING_UI_DEFAULTS.normal_top_speed_world_units_per_second} wu/s`,
    );
  });

  it('coalesces numeric and slider edits into one explicit atomic Apply without optimistic success', () => {
    vi.useFakeTimers();
    const { connection, sender } = panelConnection();
    render(<PanelHarness connection={connection} />);
    editDraft();
    expect(sender).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole('button', { name: 'Apply room tuning' }));
    expect(sender).toHaveBeenCalledExactlyOnceWith(tuningCommand());
    expect(screen.getByRole('status')).not.toHaveTextContent(
      /applied at revision/,
    );
    expect(
      screen.getByRole('button', { name: 'Apply room tuning' }),
    ).toBeDisabled();
  });

  it('keeps a peer-conflicted draft visible and requires explicit review before Apply', () => {
    vi.useFakeTimers();
    const { connection, sender } = panelConnection();
    const view = render(<PanelHarness connection={connection} />);
    editDraft();
    const newer = withMovement(connection, {
      current: TUNING_UI_PEER,
      revision: TUNING_UI_REVISION + 1,
    });
    view.rerender(<PanelHarness connection={newer} />);
    expect(
      screen.getByRole('spinbutton', { name: 'Acceleration (wu/s²)' }),
    ).toHaveValue(TUNING_UI_DRAFT.acceleration_world_units_per_second_squared);
    expect(
      screen.getByLabelText('Authoritative room values'),
    ).toHaveTextContent(
      `${TUNING_UI_PEER.acceleration_world_units_per_second_squared} wu/s²`,
    );
    expect(
      screen.getByRole('button', { name: 'Apply room tuning' }),
    ).toBeDisabled();
    fireEvent.click(
      screen.getByRole('button', { name: 'Review current values; keep draft' }),
    );
    expect(sender).not.toHaveBeenCalled();
    expect(
      screen.getByRole('spinbutton', { name: 'Normal top speed (wu/s)' }),
    ).toHaveValue(TUNING_UI_DRAFT.normal_top_speed_world_units_per_second);
    fireEvent.click(screen.getByRole('button', { name: 'Apply room tuning' }));
    expect(sender).toHaveBeenCalledExactlyOnceWith({
      ...tuningCommand(),
      payload: {
        ...tuningCommand().payload,
        expected_revision: TUNING_UI_REVISION + 1,
      },
    });
  });

  it('explains invalid local values while Reset explicitly submits authored defaults', () => {
    vi.useFakeTimers();
    const { connection, sender } = panelConnection();
    render(<PanelHarness connection={connection} />);
    const acceleration = screen.getByRole('spinbutton', {
      name: 'Acceleration (wu/s²)',
    });
    fireEvent.change(acceleration, { target: { value: '' } });
    expect(acceleration).toHaveAttribute('aria-invalid', 'true');
    expect(
      screen.getAllByText(
        'Enter finite values inside all published room limits.',
      ).length,
    ).toBeGreaterThan(0);
    expect(
      screen.getByRole('button', { name: 'Apply room tuning' }),
    ).toBeDisabled();
    expect(sender).not.toHaveBeenCalled();
    fireEvent.click(
      screen.getByRole('button', { name: 'Reset to authored defaults' }),
    );
    expect(sender).toHaveBeenCalledExactlyOnceWith({
      kind: 'set_movement_tuning',
      payload: {
        ...TUNING_UI_DEFAULTS,
        tuning_request_id: 1,
        expected_revision: TUNING_UI_REVISION,
      },
    });
  });

  it('disables edits and both submission buttons while awaiting a correlated outcome', () => {
    const { connection, sender } = panelConnection();
    render(
      <PanelHarness
        connection={{
          ...connection,
          movementTuning: pendingTuning(tuningCommand()),
        }}
      />,
    );
    expect(
      screen.getByRole('spinbutton', { name: 'Acceleration (wu/s²)' }),
    ).toBeDisabled();
    expect(
      screen.getByRole('slider', { name: 'Normal top speed slider' }),
    ).toBeDisabled();
    expect(
      screen.getByRole('button', { name: 'Apply room tuning' }),
    ).toBeDisabled();
    expect(
      screen.getByRole('button', { name: 'Reset to authored defaults' }),
    ).toBeDisabled();
    expect(screen.getByRole('status')).toHaveTextContent(
      'pending server confirmation',
    );
    expect(sender).not.toHaveBeenCalled();
  });

  it('retains unknown wording after current values are reviewed without resubmitting', () => {
    const { connection, sender } = panelConnection();
    render(
      <PanelHarness
        connection={{
          ...connection,
          movementTuning: {
            status: 'unknown',
            request: tuningCommand().payload,
          },
        }}
      />,
    );
    expect(screen.getByRole('status')).toHaveTextContent('unknown outcome');
    expect(
      screen.getByRole('button', { name: 'Apply room tuning' }),
    ).toBeDisabled();
    fireEvent.click(
      screen.getByRole('button', { name: 'Review current values; keep draft' }),
    );
    expect(screen.getByRole('status')).toHaveTextContent('unknown outcome');
    expect(
      screen.getByRole('button', { name: 'Apply room tuning' }),
    ).toBeEnabled();
    expect(sender).not.toHaveBeenCalled();
  });

  it('shows historical committed application separately from newer shared values', () => {
    const { connection, sender } = panelConnection();
    const newer = withMovement(connection, {
      current: TUNING_UI_PEER,
      revision: TUNING_UI_REVISION + 2,
    });
    render(
      <PanelHarness
        connection={{
          ...newer,
          movementTuning: resolvedTuning(tuningCommand()),
        }}
      />,
    );
    expect(screen.getByRole('status')).toHaveTextContent(
      `applied at revision ${TUNING_UI_REVISION + 1}`,
    );
    const current = screen.getByLabelText('Authoritative room values');
    expect(
      within(current).getByText(String(TUNING_UI_REVISION + 2), {
        selector: 'dd',
      }),
    ).toBeVisible();
    expect(current).toHaveTextContent(
      `${TUNING_UI_PEER.acceleration_world_units_per_second_squared} wu/s²`,
    );
    expect(sender).not.toHaveBeenCalled();
  });

  it('shows the charge as added velocity and derives its draft boost from the draft normal speed', () => {
    const { connection } = panelConnection();
    render(<PanelHarness connection={connection} />);
    expect(
      screen.getByLabelText('Authoritative room values'),
    ).toHaveTextContent(
      `${TUNING_UI_CURRENT.charge_speed_fraction * TUNING_UI_CURRENT.normal_top_speed_world_units_per_second} wu/s added`,
    );
    editDraft();
    expect(screen.getByText(/Draft charge adds/)).toHaveTextContent(
      `Draft charge adds ${TUNING_UI_DRAFT.charge_speed_fraction * TUNING_UI_DRAFT.normal_top_speed_world_units_per_second} wu/s to velocity.`,
    );
    expect(screen.getByText(/Objects arrive at random/)).toHaveTextContent(
      'Rate changes affect future spawns',
    );
  });

  it.each([
    {
      label: 'Dangerous objects per second',
      field: 'lethal_spawn_rate_per_second',
    },
    {
      label: 'Non-dangerous objects per second',
      field: 'nonlethal_spawn_rate_per_second',
    },
  ] as const)(
    'disables the $label controls when that class is absent while preserving other edits',
    ({ label, field }) => {
      const { connection, sender } = panelConnection();
      if (connection.match === null)
        throw new Error('TEST.TUNING_MATCH_MISSING');
      const absent = withMovement(connection, {
        current: { ...TUNING_UI_CURRENT, [field]: 0 },
        defaults: { ...TUNING_UI_DEFAULTS, [field]: 0 },
        limits: {
          ...connection.match.movement.limits,
          [field]: { minimum: 0, maximum: 0 },
        },
      });
      render(<PanelHarness connection={absent} />);
      expect(screen.getByRole('spinbutton', { name: label })).toBeDisabled();
      expect(
        screen.getByRole('slider', { name: `${label} slider` }),
      ).toBeDisabled();
      expect(
        screen.getByRole('spinbutton', { name: 'Charge boost' }),
      ).toBeEnabled();
      fireEvent.click(
        screen.getByRole('button', { name: 'Apply room tuning' }),
      );
      expect(sender).toHaveBeenCalledExactlyOnceWith({
        kind: 'set_movement_tuning',
        payload: {
          ...TUNING_UI_CURRENT,
          [field]: 0,
          tuning_request_id: 1,
          expected_revision: TUNING_UI_REVISION,
        },
      });
    },
  );

  it('does not offer controls in a session that does not advertise tuning', () => {
    const { connection, sender } = panelConnection();
    if (connection.session === null)
      throw new Error('TEST.TUNING_SESSION_MISSING');
    render(
      <PanelHarness
        connection={{
          ...connection,
          session: {
            ...connection.session,
            acceptedCommandKinds:
              connection.session.acceptedCommandKinds.filter(
                (kind) => kind !== 'set_movement_tuning',
              ),
          },
        }}
      />,
    );
    expect(screen.queryByRole('region', { name: 'Room tuning' })).toBeNull();
    expect(sender).not.toHaveBeenCalled();
  });
});
