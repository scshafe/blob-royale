import { render, screen, waitFor } from '@testing-library/react';
import userEvent from '@testing-library/user-event';
import axios from 'axios';
import { expect, it, vi } from 'vitest';

import Config from './Config.jsx';
import {
  expectedGameConfiguration,
  gameConfigurationResponse,
} from './fixtures/gameConfiguration.js';

vi.mock('axios', () => ({
  default: {
    get: vi.fn(),
  },
}));

it('loads server configuration and derives partition dimensions', async () => {
  axios.get.mockResolvedValueOnce({ data: gameConfigurationResponse });
  const onConfigReceived = vi.fn();
  const user = userEvent.setup();

  render(<Config onConfigReceived={onConfigReceived} />);
  await user.click(screen.getByRole('button', { name: 'get config' }));

  await waitFor(() => {
    expect(onConfigReceived).toHaveBeenCalledWith(expectedGameConfiguration);
  });
});
