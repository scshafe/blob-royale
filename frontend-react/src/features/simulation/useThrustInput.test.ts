import { act, fireEvent, renderHook } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS } from './simulationConstants';
import { createThrustInputControls } from './fixtures/thrustInputControls';
import type { SessionCommand } from './simulationProtocolTypes';
import { useThrustInput } from './useThrustInput';

let controls: ReturnType<typeof createThrustInputControls> | null = null;

function editingControls(): ReturnType<typeof createThrustInputControls> {
  controls = createThrustInputControls();
  return controls;
}

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
  controls?.container.remove();
  controls = null;
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
    if (diagonal?.kind !== 'set_thrust') {
      throw new Error('TEST.THRUST_NOT_SENT');
    }
    expect(diagonal.payload.x).toBeCloseTo(Math.SQRT1_2, 12);
    expect(diagonal.payload.y).toBeCloseTo(-Math.SQRT1_2, 12);
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

  it.each([
    'number',
    'range',
    'apply',
    'input',
    'textarea',
    'select',
    'editable',
  ] as const)(
    'leaves native typing and arrow keys untouched in %s controls',
    (name) => {
      vi.useFakeTimers();
      const sendCommand = createSender();
      const { result } = renderHook(() =>
        useThrustInput({ enabled: true, sendCommand }),
      );
      const control = editingControls()[name];
      act(() => {
        control.focus();
        for (const code of [
          'KeyW',
          'KeyA',
          'KeyS',
          'KeyD',
          'ArrowUp',
          'ArrowLeft',
          'ArrowDown',
          'ArrowRight',
        ]) {
          expect(fireEvent.keyDown(control, { code, cancelable: true })).toBe(
            true,
          );
          expect(fireEvent.keyUp(control, { code, cancelable: true })).toBe(
            true,
          );
        }
        // A window-targeted event must still respect the currently focused editor.
        expect(
          fireEvent.keyDown(window, { code: 'ArrowUp', cancelable: true }),
        ).toBe(true);
      });
      expect(sendCommand).not.toHaveBeenCalled();
      expect(result.current).toEqual({ x: 0, y: 0 });
    },
  );

  it('recognizes contenteditable descendants without stealing their key defaults', () => {
    vi.useFakeTimers();
    const sendCommand = createSender();
    renderHook(() => useThrustInput({ enabled: true, sendCommand }));
    const editor = editingControls();
    act(() => {
      editor.editable.focus();
      expect(
        fireEvent.keyDown(editor.editableChild, {
          code: 'KeyW',
          cancelable: true,
        }),
      ).toBe(true);
      expect(
        fireEvent.keyUp(editor.editableChild, {
          code: 'KeyW',
          cancelable: true,
        }),
      ).toBe(true);
    });
    expect(sendCommand).not.toHaveBeenCalled();
  });

  it.each(['number', 'input', 'textarea', 'editable', 'apply'] as const)(
    'clears held and queued thrust when %s gains focus and sends one throttled zero',
    async (name) => {
      vi.useFakeTimers();
      const sendCommand = createSender();
      const { result } = renderHook(() =>
        useThrustInput({ enabled: true, sendCommand }),
      );
      const editor = editingControls();
      act(() => {
        fireEvent.keyDown(window, { code: 'KeyD' });
        fireEvent.keyDown(window, { code: 'KeyW' });
        editor[name].focus();
      });
      expect(result.current).toEqual({ x: 0, y: 0 });
      expect(sendCommand).toHaveBeenCalledTimes(1);
      await advance(THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS);
      expect(sendCommand).toHaveBeenCalledTimes(2);
      expect(sendCommand).toHaveBeenLastCalledWith({
        kind: 'set_thrust',
        payload: { x: 0, y: 0 },
      });
      act(() => {
        editor.range.focus();
        editor.apply.focus();
        fireEvent.keyUp(editor.apply, { code: 'KeyW' });
        fireEvent.blur(window);
      });
      await advance(THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS);
      expect(sendCommand).toHaveBeenCalledTimes(2);
    },
  );

  it('requires a fresh gameplay press after editing instead of reviving held or repeated keys', async () => {
    vi.useFakeTimers();
    const sendCommand = createSender();
    const { result } = renderHook(() =>
      useThrustInput({ enabled: true, sendCommand }),
    );
    const editor = editingControls();
    act(() => {
      fireEvent.keyDown(window, { code: 'KeyW' });
      editor.number.focus();
    });
    await advance(THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS);
    act(() => {
      editor.camera.focus();
      fireEvent.keyDown(editor.camera, { code: 'KeyW', repeat: true });
      fireEvent.keyUp(editor.camera, { code: 'KeyW' });
    });
    await advance(THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS);
    expect(result.current).toEqual({ x: 0, y: 0 });
    expect(sendCommand).toHaveBeenCalledTimes(2);
    act(() => {
      fireEvent.keyDown(editor.camera, { code: 'KeyW' });
    });
    expect(sendCommand).toHaveBeenCalledTimes(3);
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 0, y: -1 },
    });
  });

  it('does not carry input received during editing into a newly enabled body', async () => {
    vi.useFakeTimers();
    const sendCommand = createSender();
    const { result, rerender } = renderHook(
      ({ enabled }: { enabled: boolean }) =>
        useThrustInput({ enabled, sendCommand }),
      { initialProps: { enabled: false } },
    );
    const editor = editingControls();
    act(() => {
      fireEvent.keyDown(window, { code: 'KeyW' });
      editor.number.focus();
      fireEvent.keyDown(editor.number, { code: 'ArrowRight' });
    });
    rerender({ enabled: true });
    await advance(THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS);
    expect(result.current).toEqual({ x: 0, y: 0 });
    expect(sendCommand).not.toHaveBeenCalled();
    act(() => {
      editor.camera.focus();
      fireEvent.keyDown(editor.camera, { code: 'KeyW', repeat: true });
    });
    expect(sendCommand).not.toHaveBeenCalled();
    act(() => {
      fireEvent.keyDown(editor.camera, { code: 'KeyD' });
    });
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { x: 1, y: 0 },
    });
  });

  it('preserves arrow steering from unmarked camera buttons', () => {
    vi.useFakeTimers();
    const sendCommand = createSender();
    renderHook(() => useThrustInput({ enabled: true, sendCommand }));
    const camera = editingControls().camera;
    act(() => {
      camera.focus();
      expect(
        fireEvent.keyDown(camera, { code: 'ArrowRight', cancelable: true }),
      ).toBe(false);
    });
    expect(sendCommand).toHaveBeenCalledWith({
      kind: 'set_thrust',
      payload: { x: 1, y: 0 },
    });
  });
});
