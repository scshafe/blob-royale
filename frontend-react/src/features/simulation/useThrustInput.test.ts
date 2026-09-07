import { act, fireEvent, renderHook } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS } from './simulationConstants';
import type { SessionCommand } from './simulationProtocolTypes';
import { useThrustInput } from './useThrustInput';

function createSender() {
  return vi.fn((command: SessionCommand) => {
    void command;
    return true;
  });
}

async function advance(milliseconds: number): Promise<void> {
  await act(async () => {
    await vi.advanceTimersByTimeAsync(milliseconds);
  });
}

afterEach(() => {
  vi.useRealTimers();
  vi.restoreAllMocks();
});

describe('useThrustInput', () => {
  it('sends nothing until a key is pressed', () => {
    vi.useFakeTimers();
    const sendCommand = createSender();

    const { result } = renderHook(() =>
      useThrustInput({ enabled: true, sendCommand }),
    );

    expect(sendCommand).not.toHaveBeenCalled();
    expect(result.current).toEqual({ x: 0, y: 0 });
  });

  it('maps WASD and the arrow keys to one unit direction', async () => {
    vi.useFakeTimers();
    const sendCommand = createSender();
    const { result } = renderHook(() =>
      useThrustInput({ enabled: true, sendCommand }),
    );

    act(() => {
      fireEvent.keyDown(window, { code: 'KeyW' });
    });
    expect(sendCommand).toHaveBeenCalledWith({
      kind: 'set_thrust',
      payload: { x: 0, y: -1 },
    });

    act(() => {
      fireEvent.keyDown(window, { code: 'ArrowRight' });
    });
    await advance(THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS);

    const diagonal = sendCommand.mock.calls[1]?.[0];
    expect(diagonal?.payload.x).toBeCloseTo(Math.SQRT1_2, 12);
    expect(diagonal?.payload.y).toBeCloseTo(-Math.SQRT1_2, 12);
    expect(result.current.x).toBeCloseTo(Math.SQRT1_2, 12);
  });

  it('sends at most one command every 50 ms and none while a key is held', async () => {
    vi.useFakeTimers();
    const sendCommand = createSender();
    renderHook(() => useThrustInput({ enabled: true, sendCommand }));

    act(() => {
      fireEvent.keyDown(window, { code: 'KeyD' });
    });
    expect(sendCommand).toHaveBeenCalledTimes(1);

    act(() => {
      fireEvent.keyDown(window, { code: 'KeyD', repeat: true });
      fireEvent.keyDown(window, { code: 'KeyW' });
    });
    expect(sendCommand).toHaveBeenCalledTimes(1);

    await advance(THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS - 1);
    expect(sendCommand).toHaveBeenCalledTimes(1);

    await advance(1);
    expect(sendCommand).toHaveBeenCalledTimes(2);

    await advance(1_000);
    expect(sendCommand).toHaveBeenCalledTimes(2);
  });

  it('sends zero when the last key is released', async () => {
    vi.useFakeTimers();
    const sendCommand = createSender();
    renderHook(() => useThrustInput({ enabled: true, sendCommand }));

    act(() => {
      fireEvent.keyDown(window, { code: 'KeyA' });
    });
    act(() => {
      fireEvent.keyUp(window, { code: 'KeyA' });
    });
    await advance(THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS);

    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 0, y: 0 },
    });
  });

  it('clears held keys when the window loses focus', async () => {
    vi.useFakeTimers();
    const sendCommand = createSender();
    renderHook(() => useThrustInput({ enabled: true, sendCommand }));

    act(() => {
      fireEvent.keyDown(window, { code: 'KeyS' });
    });
    act(() => {
      fireEvent.blur(window);
    });
    await advance(THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS);

    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 0, y: 0 },
    });
  });

  it('ignores keys entirely while this session owns no body', async () => {
    vi.useFakeTimers();
    const sendCommand = createSender();
    const { rerender, result } = renderHook(
      ({ enabled }: { enabled: boolean }) =>
        useThrustInput({ enabled, sendCommand }),
      { initialProps: { enabled: false } },
    );

    act(() => {
      fireEvent.keyDown(window, { code: 'KeyW' });
    });
    await advance(1_000);

    expect(sendCommand).not.toHaveBeenCalled();
    expect(result.current).toEqual({ x: 0, y: -1 });

    rerender({ enabled: true });
    await advance(THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS);

    expect(sendCommand).toHaveBeenCalledWith({
      kind: 'set_thrust',
      payload: { x: 0, y: -1 },
    });
  });

  it('leaves keys with a modifier to the browser', () => {
    vi.useFakeTimers();
    const sendCommand = createSender();
    renderHook(() => useThrustInput({ enabled: true, sendCommand }));

    act(() => {
      fireEvent.keyDown(window, { code: 'KeyW', metaKey: true });
    });

    expect(sendCommand).not.toHaveBeenCalled();
  });
});
