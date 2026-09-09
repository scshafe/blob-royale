import { fireEvent, render, screen, within } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { SimulationShell } from './SimulationShell';

describe('SimulationShell', () => {
  it('names the app and the directory level, with no way up from the top', () => {
    render(
      <SimulationShell lobbyId={null} onLeave={vi.fn()}>
        <p>content</p>
      </SimulationShell>,
    );

    expect(
      screen.getByRole('heading', { level: 1, name: 'Blob Royale' }),
    ).toBeVisible();
    const breadcrumbs = screen.getByRole('navigation', { name: 'Rooms' });
    expect(within(breadcrumbs).getByText('Rooms')).toHaveAttribute(
      'aria-current',
      'page',
    );
    expect(within(breadcrumbs).queryByRole('link')).toBeNull();
    expect(screen.queryByRole('button', { name: 'Leave room' })).toBeNull();
    expect(screen.getByRole('main')).toHaveTextContent('content');
  });

  it('shows the room as the current level, with two ways up', () => {
    const onLeave = vi.fn();
    render(
      <SimulationShell lobbyId={2} onLeave={onLeave}>
        <p>content</p>
      </SimulationShell>,
    );

    // The same header as the directory: only the breadcrumb's last segment and the control change.
    expect(
      screen.getByRole('heading', { level: 1, name: 'Blob Royale' }),
    ).toBeVisible();
    const breadcrumbs = screen.getByRole('navigation', { name: 'Rooms' });
    expect(within(breadcrumbs).getByText('Room 2')).toHaveAttribute(
      'aria-current',
      'page',
    );

    fireEvent.click(within(breadcrumbs).getByRole('link', { name: 'Rooms' }));
    fireEvent.click(screen.getByRole('button', { name: 'Leave room' }));
    expect(onLeave).toHaveBeenCalledTimes(2);
  });
});
