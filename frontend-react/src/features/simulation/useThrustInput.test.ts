import { act, fireEvent, renderHook } from '@testing-library/react';
import { flushSync } from 'react-dom';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import { SimulationApiError } from './SimulationApiError';
import {
  ABILITY_COMMAND_MIN_INTERVAL_MILLISECONDS,
  CHARGE_KEY_CODE,
  SHIELD_KEY_CODE,
  THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS,
} from './simulationConstants';
import { createThrustInputControls } from './fixtures/thrustInputControls';
import {
  ABILITIES_AVAILABLE,
  ABILITY_KEY_CASES,
  REMOVED_DIRECTION_KEYS,
  THRUST_DIRECTION_CASES,
  THRUST_DOWN,
  THRUST_INVALID_AIMS,
  THRUST_LEFT,
  THRUST_REPLACEMENT_ENTITY_ID,
  THRUST_RIGHT,
  THRUST_STATIONARY_POINTER_UP,
  THRUST_UP,
  THRUST_ZERO,
  abilityUnavailableFor,
  thrustAim,
  thrustInputOptions,
} from './fixtures/thrustInputFrames';
import { cameraSessionIdentity } from './fixtures/simulationCameraFrames';
import {
  STUN_INPUT_GENERATION,
  STUN_INPUT_NEXT_GENERATION,
} from './fixtures/stunInputFrames';
import type { SessionCommand } from './simulationProtocolTypes';
import {
  useThrustInput,
  type SimulationAbility,
  type ThrustInputOptions,
} from './useThrustInput';

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

function renderInput(
  overrides: Partial<ThrustInputOptions> = {},
  sendCommand = createSender(),
) {
  const options = thrustInputOptions(sendCommand, overrides);
  return {
    ...renderHook(useThrustInput, { initialProps: options }),
    options,
    sendCommand,
  };
}

/** Returns whether the browser default survived, which is how a binding proves it took the key. */
function pressKey(
  code: string,
  target: HTMLElement | Window = window,
  fields: KeyboardEventInit = {},
): boolean {
  let defaultAllowed = true;
  act(() => {
    defaultAllowed = fireEvent.keyDown(target, {
      code,
      cancelable: true,
      ...fields,
    });
  });
  return defaultAllowed;
}

function releaseKey(
  code: string,
  target: HTMLElement | Window = window,
): boolean {
  let defaultAllowed = true;
  act(() => {
    defaultAllowed = fireEvent.keyUp(target, { code, cancelable: true });
  });
  return defaultAllowed;
}

function pressGo(
  target: HTMLElement | Window = window,
  fields: KeyboardEventInit = {},
): boolean {
  return pressKey('Space', target, fields);
}

function releaseGo(target: HTMLElement | Window = window): void {
  releaseKey('Space', target);
}

/** The other of the two abilities, so a suppression test cannot pass by blocking everything. */
function otherAbilityCase(
  ability: SimulationAbility,
): (typeof ABILITY_KEY_CASES)[number] {
  const other = ABILITY_KEY_CASES.find((entry) => entry.ability !== ability);
  if (other === undefined) throw new Error('TEST.ABILITY_CASE_MISSING');
  return other;
}

async function advance(
  milliseconds = THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS,
): Promise<void> {
  await act(async () => {
    await vi.advanceTimersByTimeAsync(milliseconds);
  });
}

beforeEach(() => {
  vi.useFakeTimers();
});

afterEach(() => {
  controls?.container.remove();
  controls = null;
  vi.useRealTimers();
  vi.restoreAllMocks();
});

describe('useThrustInput', () => {
  it('observes aim without propulsion and requires a fresh press after directionless activation', () => {
    const { result, sendCommand } = renderInput();
    expect(result.current.direction).toEqual(THRUST_ZERO);
    expect(result.current.lastNonzeroAimDirection).toBeNull();
    expect(pressGo()).toBe(true);
    act(() => result.current.observeAim(thrustAim()));
    expect(result.current.aimDirection).toEqual(THRUST_RIGHT);
    expect(result.current.lastNonzeroAimDirection).toEqual(THRUST_RIGHT);
    expect(sendCommand).not.toHaveBeenCalled();
    pressGo(window, { repeat: true });
    expect(sendCommand).not.toHaveBeenCalled();
    releaseGo();
    expect(pressGo()).toBe(false);
    expect(sendCommand).toHaveBeenCalledExactlyOnceWith({
      kind: 'set_thrust',
      payload: THRUST_RIGHT,
    });
  });

  it.each(THRUST_DIRECTION_CASES)(
    'uses zero-or-unit cursor direction for $name aim',
    ({ offset, expected }) => {
      const { result, sendCommand } = renderInput();
      act(() => result.current.observeAim(thrustAim(offset)));
      pressGo();
      expect(result.current.direction.x).toBeCloseTo(expected.x, 14);
      expect(result.current.direction.y).toBeCloseTo(expected.y, 14);
      const command = sendCommand.mock.calls[0]?.[0];
      if (command?.kind !== 'set_thrust')
        throw new Error('TEST.THRUST_NOT_SENT');
      expect(Math.hypot(command.payload.x, command.payload.y)).toBeCloseTo(
        1,
        14,
      );
    },
  );

  it('discards pointer distance and keeps near and far strength identical', async () => {
    const { result, sendCommand } = renderInput();
    act(() => result.current.observeAim(thrustAim(THRUST_UP, 1)));
    pressGo();
    act(() => result.current.observeAim(thrustAim(THRUST_UP, 1000)));
    await advance();
    expect(sendCommand).toHaveBeenCalledExactlyOnceWith({
      kind: 'set_thrust',
      payload: THRUST_UP,
    });
  });

  it('coalesces stationary-pointer geometry changes through the single 50 ms sender', async () => {
    const { result, sendCommand } = renderInput();
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    act(() => {
      result.current.observeAim(THRUST_STATIONARY_POINTER_UP);
      result.current.observeAim(thrustAim(THRUST_DOWN));
    });
    expect(sendCommand).toHaveBeenCalledTimes(1);
    expect(result.current.direction).toEqual(THRUST_DOWN);
    await advance(THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS - 1);
    expect(sendCommand).toHaveBeenCalledTimes(1);
    await advance(1);
    expect(sendCommand).toHaveBeenCalledTimes(2);
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: THRUST_DOWN,
    });
    await advance(1000);
    expect(sendCommand).toHaveBeenCalledTimes(2);
  });

  it('keeps observer and view stable across equal geometry and unrelated rerenders', async () => {
    const { result, rerender, options, sendCommand } = renderInput();
    const observe = result.current.observeAim;
    act(() => observe(thrustAim()));
    pressGo();
    const view = result.current;
    act(() => {
      observe(thrustAim());
      observe(thrustAim());
    });
    expect(result.current).toBe(view);
    rerender({ ...options });
    expect(result.current.observeAim).toBe(observe);
    expect(result.current.direction).toEqual(THRUST_RIGHT);
    pressGo(window, { repeat: true });
    await advance(1000);
    expect(sendCommand).toHaveBeenCalledTimes(1);
  });

  it('uses exact-center zero without remembered thrust and permits a valid hold to aim away again', async () => {
    const { result, sendCommand } = renderInput();
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    act(() => result.current.observeAim(thrustAim(THRUST_ZERO)));
    expect(result.current.direction).toEqual(THRUST_ZERO);
    expect(result.current.aimDirection).toEqual(THRUST_ZERO);
    expect(result.current.lastNonzeroAimDirection).toEqual(THRUST_RIGHT);
    await advance();
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: THRUST_ZERO,
    });
    act(() => result.current.observeAim(thrustAim(THRUST_LEFT)));
    await advance();
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: THRUST_LEFT,
    });
  });

  it('replaces queued nonzero with one release-to-coast zero', async () => {
    const { result, sendCommand } = renderInput();
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    act(() => result.current.observeAim(thrustAim(THRUST_UP)));
    releaseGo();
    expect(result.current.direction).toEqual(THRUST_ZERO);
    await advance();
    expect(sendCommand.mock.calls).toEqual([
      [{ kind: 'set_thrust', payload: THRUST_RIGHT }],
      [{ kind: 'set_thrust', payload: THRUST_ZERO }],
    ]);
    releaseGo();
    act(() => result.current.observeAim(thrustAim(THRUST_DOWN)));
    await advance();
    expect(sendCommand).toHaveBeenCalledTimes(2);
  });

  it('does not postpone zero release when wall time moves backward', async () => {
    const { result, sendCommand } = renderInput();
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    vi.setSystemTime(Date.now() - 60_000);
    releaseGo();
    await advance();
    expect(sendCommand).toHaveBeenCalledTimes(2);
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: THRUST_ZERO,
    });
  });

  it.each(['departure', 'camera', 'blur', 'context menu'] as const)(
    'cancels on %s without restoring held or repeated activation',
    async (kind) => {
      const { result, sendCommand } = renderInput();
      act(() => result.current.observeAim(thrustAim()));
      pressGo();
      act(() => {
        result.current.observeAim(thrustAim(THRUST_UP));
        if (kind === 'departure') result.current.observeAim(null);
        if (kind === 'camera')
          result.current.observeAim(thrustAim(THRUST_UP, 100, true));
        if (kind === 'blur') fireEvent.blur(window);
        if (kind === 'context menu')
          expect(fireEvent.contextMenu(window, { cancelable: true })).toBe(
            true,
          );
      });
      expect(result.current.direction).toEqual(THRUST_ZERO);
      await advance();
      expect(sendCommand).toHaveBeenLastCalledWith({
        kind: 'set_thrust',
        payload: THRUST_ZERO,
      });
      expect(result.current.lastNonzeroAimDirection).toEqual(THRUST_UP);
      act(() => result.current.observeAim(thrustAim(THRUST_LEFT)));
      pressGo(window, { repeat: true });
      await advance();
      expect(sendCommand).toHaveBeenCalledTimes(2);
      releaseGo();
      pressGo();
      expect(sendCommand).toHaveBeenLastCalledWith({
        kind: 'set_thrust',
        payload: THRUST_LEFT,
      });
    },
  );

  it('rejects Space during a camera gesture and never arms when that gesture ends', () => {
    const { result, sendCommand } = renderInput();
    act(() => result.current.observeAim(thrustAim(THRUST_RIGHT, 100, true)));
    pressGo();
    act(() => result.current.observeAim(thrustAim()));
    pressGo(window, { repeat: true });
    expect(sendCommand).not.toHaveBeenCalled();
    releaseGo();
    pressGo();
    expect(sendCommand).toHaveBeenCalledTimes(1);
  });

  it.each([
    'number',
    'range',
    'apply',
    'input',
    'textarea',
    'select',
    'editable',
    'camera',
    'link',
  ] as const)(
    'preserves native Space and directional keys on %s and requires fresh gameplay activation',
    async (name) => {
      const { result, sendCommand } = renderInput();
      const editor = editingControls();
      act(() => result.current.observeAim(thrustAim()));
      pressGo();
      act(() => {
        result.current.observeAim(thrustAim(THRUST_UP));
        editor[name].focus();
      });
      expect(result.current.direction).toEqual(THRUST_ZERO);
      expect(pressGo(editor[name])).toBe(true);
      expect(pressGo(window)).toBe(true);
      act(() => {
        expect(
          fireEvent.keyUp(editor[name], { code: 'Space', cancelable: true }),
        ).toBe(true);
        for (const code of [
          ...REMOVED_DIRECTION_KEYS,
          ...ABILITY_KEY_CASES.map((ability) => ability.code),
        ]) {
          expect(
            fireEvent.keyDown(editor[name], { code, cancelable: true }),
          ).toBe(true);
          expect(
            fireEvent.keyUp(editor[name], { code, cancelable: true }),
          ).toBe(true);
        }
      });
      await advance();
      // Still exactly the activation and its cancelling zero: an ability key pressed into a
      // focused control activates nothing, and the control keeps its own native default.
      expect(sendCommand).toHaveBeenCalledTimes(2);
      expect(sendCommand).toHaveBeenLastCalledWith({
        kind: 'set_thrust',
        payload: THRUST_ZERO,
      });
      act(() => editor.surface.focus());
      pressGo(editor.surface, { repeat: true });
      await advance();
      expect(sendCommand).toHaveBeenCalledTimes(2);
      releaseGo(editor.surface);
      pressGo(editor.surface);
      expect(sendCommand).toHaveBeenLastCalledWith({
        kind: 'set_thrust',
        payload: THRUST_UP,
      });
    },
  );

  it('cancels a UI pointer interaction even when it does not move focus', async () => {
    const { result, sendCommand } = renderInput();
    const editor = editingControls();
    act(() => {
      editor.surface.focus();
      result.current.observeAim(thrustAim());
    });
    pressGo(editor.surface);
    act(() => {
      expect(fireEvent.pointerDown(editor.apply, { cancelable: true })).toBe(
        true,
      );
    });
    expect(document.activeElement).toBe(editor.surface);
    expect(result.current.direction).toEqual(THRUST_ZERO);
    await advance();
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: THRUST_ZERO,
    });
  });

  it('recognizes contenteditable descendants without stealing their native defaults', () => {
    const { result, sendCommand } = renderInput();
    const editor = editingControls();
    act(() => {
      result.current.observeAim(thrustAim());
      editor.editable.focus();
    });
    expect(pressGo(editor.editableChild)).toBe(true);
    expect(sendCommand).not.toHaveBeenCalled();
  });

  it('removes all WASD and arrow thrust bindings outside the UI too', () => {
    const { result, sendCommand } = renderInput();
    act(() => {
      result.current.observeAim(thrustAim());
      for (const code of REMOVED_DIRECTION_KEYS) {
        expect(fireEvent.keyDown(window, { code, cancelable: true })).toBe(
          true,
        );
        expect(fireEvent.keyUp(window, { code, cancelable: true })).toBe(true);
      }
    });
    expect(result.current.direction).toEqual(THRUST_ZERO);
    expect(sendCommand).not.toHaveBeenCalled();
  });

  it.each(['altKey', 'ctrlKey', 'metaKey', 'isComposing'] as const)(
    'leaves %s Space to the browser',
    (modifier) => {
      const { result, sendCommand } = renderInput();
      act(() => result.current.observeAim(thrustAim()));
      expect(pressGo(window, { [modifier]: true })).toBe(true);
      expect(sendCommand).not.toHaveBeenCalled();
    },
  );

  it.each(['disabled', 'no session', 'no entity'] as const)(
    'never queues input while %s',
    async (kind) => {
      const overrides =
        kind === 'disabled'
          ? { enabled: false }
          : kind === 'no session'
            ? { session: null }
            : { ownEntityId: null };
      const { result, rerender, options, sendCommand } = renderInput(overrides);
      act(() => result.current.observeAim(thrustAim()));
      pressGo();
      await advance();
      expect(result.current.direction).toEqual(THRUST_ZERO);
      expect(result.current.lastNonzeroAimDirection).toBeNull();
      expect(sendCommand).not.toHaveBeenCalled();
      const available = thrustInputOptions(sendCommand);
      rerender({ ...options, ...available });
      act(() => result.current.observeAim(thrustAim()));
      pressGo(window, { repeat: true });
      expect(sendCommand).not.toHaveBeenCalled();
      releaseGo();
      pressGo();
      expect(sendCommand).toHaveBeenCalledTimes(1);
    },
  );

  it.each(['body loss', 'replacement', 'new welcome', 'disconnect'] as const)(
    'discards queued work and remembered aim on %s even when numeric identity can repeat',
    async (kind) => {
      const { result, rerender, options, sendCommand } = renderInput();
      const observe = result.current.observeAim;
      act(() => observe(thrustAim()));
      pressGo();
      act(() => observe(thrustAim(THRUST_UP)));
      const replacement =
        kind === 'body loss'
          ? { ...options, enabled: false }
          : kind === 'replacement'
            ? { ...options, ownEntityId: THRUST_REPLACEMENT_ENTITY_ID }
            : kind === 'new welcome'
              ? { ...options, session: cameraSessionIdentity() }
              : {
                  ...options,
                  enabled: false,
                  session: null,
                  ownEntityId: null,
                };
      rerender(replacement);
      expect(result.current.observeAim).toBe(observe);
      expect(result.current.direction).toEqual(THRUST_ZERO);
      expect(result.current.lastNonzeroAimDirection).toBeNull();
      await advance();
      expect(sendCommand).toHaveBeenCalledTimes(1);
      const resumed =
        kind === 'body loss' || kind === 'disconnect'
          ? {
              ...options,
              session:
                kind === 'disconnect'
                  ? cameraSessionIdentity()
                  : options.session,
            }
          : replacement;
      rerender(resumed);
      act(() => observe(thrustAim(THRUST_LEFT)));
      pressGo(window, { repeat: true });
      await advance();
      expect(sendCommand).toHaveBeenCalledTimes(1);
      releaseGo();
      pressGo();
      expect(sendCommand).toHaveBeenLastCalledWith({
        kind: 'set_thrust',
        payload: THRUST_LEFT,
      });
    },
  );

  it('cancels a replaced sender on the same body without resetting the send interval', async () => {
    const { result, rerender, options, sendCommand } = renderInput();
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    const nextSender = createSender();
    rerender({ ...options, sendCommand: nextSender });
    expect(result.current.direction).toEqual(THRUST_ZERO);
    expect(nextSender).not.toHaveBeenCalled();
    await advance();
    expect(sendCommand).toHaveBeenCalledTimes(1);
    expect(nextSender).toHaveBeenCalledExactlyOnceWith({
      kind: 'set_thrust',
      payload: THRUST_ZERO,
    });
  });

  it('retains the captured generation for held aim updates and the zero release', async () => {
    const { result, sendCommand } = renderInput({
      inputGeneration: STUN_INPUT_GENERATION,
    });
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    act(() => result.current.observeAim(thrustAim(THRUST_UP)));
    await advance();
    releaseGo();
    await advance();
    expect(sendCommand.mock.calls.map(([command]) => command)).toEqual([
      {
        kind: 'set_thrust',
        payload: { ...THRUST_RIGHT, input_generation: STUN_INPUT_GENERATION },
      },
      {
        kind: 'set_thrust',
        payload: { ...THRUST_UP, input_generation: STUN_INPUT_GENERATION },
      },
      {
        kind: 'set_thrust',
        payload: { ...THRUST_ZERO, input_generation: STUN_INPUT_GENERATION },
      },
    ]);
  });

  it.each(['pending nonzero', 'pending zero'] as const)(
    'discards %s on generation-only invalidation and keeps the same-body throttle',
    async (pending) => {
      const { result, rerender, options, sendCommand } = renderInput({
        inputGeneration: STUN_INPUT_GENERATION,
      });
      act(() => result.current.observeAim(thrustAim()));
      pressGo();
      if (pending === 'pending zero') releaseGo();
      else act(() => result.current.observeAim(thrustAim(THRUST_UP)));
      rerender({ ...options, inputGeneration: STUN_INPUT_NEXT_GENERATION });
      expect(result.current.direction).toEqual(THRUST_ZERO);
      act(() => result.current.observeAim(thrustAim()));
      pressGo(window, { repeat: true });
      expect(sendCommand).toHaveBeenCalledTimes(1);
      releaseGo();
      pressGo();
      expect(sendCommand).toHaveBeenCalledTimes(1);
      await advance();
      expect(sendCommand).toHaveBeenCalledTimes(2);
      expect(sendCommand).toHaveBeenLastCalledWith({
        kind: 'set_thrust',
        payload: {
          ...THRUST_RIGHT,
          input_generation: STUN_INPUT_NEXT_GENERATION,
        },
      });
    },
  );

  it('cancels an absent-generation activation across an entirely missed stun without geometry or repeats rearming it', async () => {
    const { result, rerender, options, sendCommand } = renderInput();
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    act(() => result.current.observeAim(thrustAim(THRUST_UP)));
    rerender({ ...options, inputGeneration: STUN_INPUT_GENERATION });
    act(() => result.current.observeAim(thrustAim(THRUST_LEFT)));
    pressGo(window, { repeat: true });
    await advance(1000);
    expect(sendCommand).toHaveBeenCalledTimes(1);
    expect(result.current.direction).toEqual(THRUST_ZERO);
    releaseGo();
    pressGo();
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { ...THRUST_LEFT, input_generation: STUN_INPUT_GENERATION },
    });
  });

  it('discards an old zero release without sending any command for the new generation', async () => {
    const { result, rerender, options, sendCommand } = renderInput({
      inputGeneration: STUN_INPUT_GENERATION,
    });
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    releaseGo();
    rerender({ ...options, inputGeneration: STUN_INPUT_NEXT_GENERATION });
    act(() => result.current.observeAim(thrustAim()));
    await advance(1000);
    expect(sendCommand).toHaveBeenCalledExactlyOnceWith({
      kind: 'set_thrust',
      payload: { ...THRUST_RIGHT, input_generation: STUN_INPUT_GENERATION },
    });
  });

  it('keeps a same-generation hold alive across new option objects', async () => {
    const { result, rerender, options, sendCommand } = renderInput({
      inputGeneration: STUN_INPUT_GENERATION,
    });
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    rerender({ ...options });
    act(() => result.current.observeAim(thrustAim(THRUST_UP)));
    await advance();
    expect(result.current.direction).toEqual(THRUST_UP);
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { ...THRUST_UP, input_generation: STUN_INPUT_GENERATION },
    });
  });

  it('keeps the original generation on a sender-only cancellation zero', async () => {
    const { result, rerender, options } = renderInput({
      inputGeneration: STUN_INPUT_GENERATION,
    });
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    const nextSender = createSender();
    rerender({ ...options, sendCommand: nextSender });
    expect(nextSender).not.toHaveBeenCalled();
    await advance();
    expect(nextSender).toHaveBeenCalledExactlyOnceWith({
      kind: 'set_thrust',
      payload: { ...THRUST_ZERO, input_generation: STUN_INPUT_GENERATION },
    });
  });

  it('never retags a timer fired before the generation render commits', async () => {
    const { result, rerender, options, sendCommand } = renderInput({
      inputGeneration: STUN_INPUT_GENERATION,
    });
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    act(() => result.current.observeAim(thrustAim(THRUST_UP)));
    await advance();
    rerender({ ...options, inputGeneration: STUN_INPUT_NEXT_GENERATION });
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: { ...THRUST_UP, input_generation: STUN_INPUT_GENERATION },
    });
    await advance();
    expect(sendCommand).toHaveBeenCalledTimes(2);
  });

  it.each([false, true])(
    'retires synchronous generation replacement before a sender returns %s',
    async (submitted) => {
      let invalidateInput: (() => void) | null = null;
      let invalidated = false;
      const sender = vi.fn((command: SessionCommand) => {
        void command;
        if (invalidated) return true;
        if (invalidateInput === null)
          throw new Error('TEST.INPUT_INVALIDATION_MISSING');
        invalidated = true;
        invalidateInput();
        return submitted;
      });
      const { result, rerender, options, sendCommand } = renderInput(
        { inputGeneration: STUN_INPUT_GENERATION },
        sender,
      );
      invalidateInput = () => {
        flushSync(() =>
          rerender({ ...options, inputGeneration: STUN_INPUT_NEXT_GENERATION }),
        );
        result.current.observeAim(thrustAim(THRUST_UP));
      };
      act(() => result.current.observeAim(thrustAim()));
      pressGo();
      expect(result.current.direction).toEqual(THRUST_ZERO);
      expect(result.current.aimDirection).toEqual(THRUST_UP);
      expect(result.current.lastNonzeroAimDirection).toEqual(THRUST_UP);
      expect(sendCommand).toHaveBeenCalledExactlyOnceWith({
        kind: 'set_thrust',
        payload: { ...THRUST_RIGHT, input_generation: STUN_INPUT_GENERATION },
      });
      releaseGo();
      pressGo();
      expect(sendCommand).toHaveBeenCalledTimes(1);
      await advance();
      expect(sendCommand).toHaveBeenCalledTimes(2);
      expect(sendCommand).toHaveBeenLastCalledWith({
        kind: 'set_thrust',
        payload: { ...THRUST_UP, input_generation: STUN_INPUT_NEXT_GENERATION },
      });
    },
  );

  it('does not retry a refused send on observation feedback and requires a fresh activation', async () => {
    const sender = createSender();
    sender.mockReturnValueOnce(false);
    const { result, sendCommand } = renderInput({}, sender);
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    expect(result.current.direction).toEqual(THRUST_ZERO);
    expect(result.current.aimDirection).toEqual(THRUST_RIGHT);
    act(() => {
      result.current.observeAim(thrustAim());
      result.current.observeAim(thrustAim(THRUST_UP));
    });
    pressGo(window, { repeat: true });
    await advance(1000);
    expect(sendCommand).toHaveBeenCalledTimes(1);
    releaseGo();
    pressGo();
    expect(sendCommand).toHaveBeenCalledTimes(2);
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: THRUST_UP,
    });
  });

  it('never replays a refused activation into a fresh welcome with equal numeric IDs', async () => {
    const sender = createSender();
    sender.mockReturnValue(false);
    const { result, rerender, options, sendCommand } = renderInput({}, sender);
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    rerender({ ...options, session: cameraSessionIdentity() });
    act(() => result.current.observeAim(thrustAim()));
    pressGo(window, { repeat: true });
    await advance();
    expect(sendCommand).toHaveBeenCalledTimes(1);
    expect(result.current.direction).toEqual(THRUST_ZERO);
  });

  it('sends a fresh activation after refusal even when aim equals the earlier successful direction', async () => {
    const sender = createSender();
    sender
      .mockReturnValueOnce(true)
      .mockReturnValueOnce(false)
      .mockReturnValueOnce(true);
    const { result, sendCommand } = renderInput({}, sender);
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    act(() => result.current.observeAim(thrustAim(THRUST_UP)));
    await advance();
    expect(result.current.direction).toEqual(THRUST_ZERO);
    act(() => result.current.observeAim(thrustAim()));
    await advance();
    expect(sendCommand).toHaveBeenCalledTimes(2);
    releaseGo();
    pressGo();
    expect(sendCommand).toHaveBeenCalledTimes(3);
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: THRUST_RIGHT,
    });
  });

  it.each(THRUST_INVALID_AIMS)(
    'fails visibly for invalid observed geometry %#',
    (observation) => {
      const { result, sendCommand } = renderInput();
      expect(() => result.current.observeAim(observation)).toThrow(
        SimulationApiError,
      );
      expect(sendCommand).not.toHaveBeenCalled();
    },
  );

  it.each([false, true])(
    'does not publish a retired lifetime after synchronous sender replacement returns %s',
    async (submitted) => {
      let replaceLifetime: (() => void) | null = null;
      const sender = vi.fn((command: SessionCommand) => {
        void command;
        if (replaceLifetime === null)
          throw new Error('TEST.INPUT_REPLACEMENT_MISSING');
        replaceLifetime();
        return submitted;
      });
      const { result, rerender, options, sendCommand } = renderInput(
        {},
        sender,
      );
      replaceLifetime = () => {
        flushSync(() =>
          rerender({ ...options, session: cameraSessionIdentity() }),
        );
        result.current.observeAim(thrustAim(THRUST_UP));
      };
      act(() => result.current.observeAim(thrustAim()));
      pressGo();
      expect(result.current.direction).toEqual(THRUST_ZERO);
      expect(result.current.aimDirection).toEqual(THRUST_UP);
      expect(result.current.lastNonzeroAimDirection).toEqual(THRUST_UP);
      await advance();
      expect(sendCommand).toHaveBeenCalledTimes(1);
    },
  );

  it.each(ABILITY_KEY_CASES)(
    'encodes the never-invalidated $ability payload its own schema requires',
    ({ ability, code }) => {
      const { result, sendCommand } = renderInput();
      act(() => result.current.observeAim(thrustAim()));
      expect(pressKey(code)).toBe(false);
      // Serialized rather than compared as an object: the whole point is which member is on the
      // wire. Shield's schema requires `input_generation` and spells never-invalidated as an
      // explicit null; charge omits the member exactly as `set_thrust` does, and a deep-equality
      // assertion would treat an accidental `undefined` as absence and pass on a broken payload.
      expect(JSON.stringify(sendCommand.mock.calls[0]?.[0])).toBe(
        ability === 'shield'
          ? '{"kind":"shield","payload":{"input_generation":null}}'
          : '{"kind":"charge","payload":{"x":1,"y":0}}',
      );
      expect(sendCommand).toHaveBeenCalledTimes(1);
    },
  );

  it.each(ABILITY_KEY_CASES)(
    'states the generation it believes it holds in the $ability payload',
    ({ ability, code }) => {
      const { result, sendCommand } = renderInput({
        inputGeneration: STUN_INPUT_GENERATION,
      });
      act(() => result.current.observeAim(thrustAim()));
      pressKey(code);
      expect(JSON.stringify(sendCommand.mock.calls[0]?.[0])).toBe(
        ability === 'shield'
          ? '{"kind":"shield","payload":{"input_generation":12900}}'
          : '{"kind":"charge","payload":{"x":1,"y":0,"input_generation":12900}}',
      );
    },
  );

  it.each(ABILITY_KEY_CASES)(
    'sends a second identical $ability pulse instead of swallowing it as unchanged',
    async ({ code }) => {
      const { result, sendCommand } = renderInput();
      act(() => result.current.observeAim(thrustAim()));
      pressKey(code);
      releaseKey(code);
      await advance(ABILITY_COMMAND_MIN_INTERVAL_MILLISECONDS);
      pressKey(code);
      expect(sendCommand).toHaveBeenCalledTimes(2);
      expect(sendCommand.mock.calls[0]).toEqual(sendCommand.mock.calls[1]);
    },
  );

  it.each(ABILITY_KEY_CASES)(
    'drops a $ability press inside the minimum interval without parking it',
    async ({ code }) => {
      const { result, sendCommand } = renderInput();
      act(() => result.current.observeAim(thrustAim()));
      pressKey(code);
      releaseKey(code);
      await advance(ABILITY_COMMAND_MIN_INTERVAL_MILLISECONDS - 1);
      pressKey(code);
      releaseKey(code);
      expect(sendCommand).toHaveBeenCalledTimes(1);
      await advance(1000);
      expect(sendCommand).toHaveBeenCalledTimes(1);
      pressKey(code);
      expect(sendCommand).toHaveBeenCalledTimes(2);
    },
  );

  it.each(ABILITY_KEY_CASES)(
    'refuses a held $ability key through the repeat flag and through its own latch',
    async ({ code }) => {
      const { result, sendCommand } = renderInput();
      act(() => result.current.observeAim(thrustAim()));
      pressKey(code);
      await advance(1000);
      expect(pressKey(code, window, { repeat: true })).toBe(false);
      expect(pressKey(code)).toBe(false);
      expect(sendCommand).toHaveBeenCalledTimes(1);
      expect(releaseKey(code)).toBe(false);
      pressKey(code);
      expect(sendCommand).toHaveBeenCalledTimes(2);
    },
  );

  it.each(ABILITY_KEY_CASES)(
    'clears a held $ability latch on a blur that swallows the release',
    async ({ code }) => {
      const { result, sendCommand } = renderInput();
      act(() => result.current.observeAim(thrustAim()));
      pressKey(code);
      act(() => {
        fireEvent.blur(window);
      });
      await advance(1000);
      pressKey(code);
      expect(sendCommand).toHaveBeenCalledTimes(2);
    },
  );

  it.each(ABILITY_KEY_CASES)(
    'skips a $ability activation while the frame publishes a reason it cannot act',
    async ({ ability, code }) => {
      const { result, rerender, options, sendCommand } = renderInput({
        abilityUnavailable: abilityUnavailableFor(ability),
      });
      act(() => result.current.observeAim(thrustAim()));
      pressKey(code);
      act(() => result.current.activateAbility(ability));
      expect(sendCommand).not.toHaveBeenCalled();
      pressKey(otherAbilityCase(ability).code);
      expect(sendCommand).toHaveBeenCalledTimes(1);
      releaseKey(code);
      await advance(1000);
      // The same options object identity everywhere else: availability must reach the handler
      // without rebuilding the effect, because a rebuild would drop the thrust a player is holding.
      rerender({ ...options, abilityUnavailable: ABILITIES_AVAILABLE });
      pressKey(code);
      expect(sendCommand).toHaveBeenCalledTimes(2);
    },
  );

  it('keeps remembered aim across a stun so an off-canvas charge still has a direction', () => {
    const { result, rerender, options, sendCommand } = renderInput({
      inputGeneration: STUN_INPUT_GENERATION,
    });
    act(() => result.current.observeAim(thrustAim(THRUST_UP)));
    rerender({
      ...options,
      inputLocked: true,
      inputGeneration: STUN_INPUT_NEXT_GENERATION,
    });
    expect(result.current.lastNonzeroAimDirection).toEqual(THRUST_UP);
    pressKey(CHARGE_KEY_CODE);
    pressKey(SHIELD_KEY_CODE);
    expect(sendCommand).not.toHaveBeenCalled();
    rerender({ ...options, inputGeneration: STUN_INPUT_NEXT_GENERATION });
    expect(result.current.lastNonzeroAimDirection).toEqual(THRUST_UP);
    expect(sendCommand).not.toHaveBeenCalled();
    releaseKey(CHARGE_KEY_CODE);
    pressKey(CHARGE_KEY_CODE);
    expect(sendCommand).toHaveBeenCalledExactlyOnceWith({
      kind: 'charge',
      payload: { ...THRUST_UP, input_generation: STUN_INPUT_NEXT_GENERATION },
    });
  });

  it.each(ABILITY_KEY_CASES)(
    'never activates $ability from a camera gesture, by key or by button',
    ({ ability, code }) => {
      const { result, sendCommand } = renderInput();
      act(() => result.current.observeAim(thrustAim(THRUST_RIGHT, 100, true)));
      expect(pressKey(code)).toBe(true);
      act(() => result.current.activateAbility(ability));
      expect(sendCommand).not.toHaveBeenCalled();
      act(() => result.current.observeAim(thrustAim()));
      pressKey(code);
      expect(sendCommand).toHaveBeenCalledTimes(1);
    },
  );

  it('activates shield without any aim and leaves charge unavailable until one exists', () => {
    const { result, sendCommand } = renderInput();
    expect(result.current.lastNonzeroAimDirection).toBeNull();
    pressKey(CHARGE_KEY_CODE);
    expect(sendCommand).not.toHaveBeenCalled();
    pressKey(SHIELD_KEY_CODE);
    expect(sendCommand).toHaveBeenCalledExactlyOnceWith({
      kind: 'shield',
      payload: { input_generation: null },
    });
    act(() => result.current.observeAim(thrustAim(THRUST_LEFT)));
    releaseKey(CHARGE_KEY_CODE);
    pressKey(CHARGE_KEY_CODE);
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'charge',
      payload: THRUST_LEFT,
    });
  });

  it.each(ABILITY_KEY_CASES)(
    'activates $ability from a button through the same rate discipline as the key',
    async ({ ability }) => {
      const { result, sendCommand } = renderInput();
      act(() => result.current.observeAim(thrustAim()));
      act(() => result.current.activateAbility(ability));
      expect(sendCommand).toHaveBeenCalledTimes(1);
      // A focused button fires click on Enter keydown and held Enter repeats, and a button has no
      // key latch of its own: the shared interval is what refuses the repeat behind it.
      act(() => {
        result.current.activateAbility(ability);
        result.current.activateAbility(ability);
      });
      expect(sendCommand).toHaveBeenCalledTimes(1);
      await advance(ABILITY_COMMAND_MIN_INTERVAL_MILLISECONDS);
      act(() => result.current.activateAbility(ability));
      expect(sendCommand).toHaveBeenCalledTimes(2);
    },
  );

  it('leaves held thrust running when an ability pulse is refused', async () => {
    const sender = createSender();
    sender.mockReturnValueOnce(true).mockReturnValueOnce(false);
    const { result, sendCommand } = renderInput({}, sender);
    act(() => result.current.observeAim(thrustAim()));
    pressGo();
    pressKey(SHIELD_KEY_CODE);
    expect(result.current.direction).toEqual(THRUST_RIGHT);
    act(() => result.current.observeAim(thrustAim(THRUST_UP)));
    await advance();
    expect(result.current.direction).toEqual(THRUST_UP);
    expect(sendCommand).toHaveBeenCalledTimes(3);
    expect(sendCommand).toHaveBeenLastCalledWith({
      kind: 'set_thrust',
      payload: THRUST_UP,
    });
  });

  it('retires the observer, keyboard listeners, and pending nonzero on unmount', async () => {
    const { result, unmount, sendCommand } = renderInput();
    const observe = result.current.observeAim;
    const activate = result.current.activateAbility;
    act(() => observe(thrustAim()));
    pressGo();
    act(() => observe(thrustAim(THRUST_UP)));
    unmount();
    observe(thrustAim(THRUST_DOWN));
    pressGo();
    for (const { ability, code } of ABILITY_KEY_CASES) {
      activate(ability);
      pressKey(code);
    }
    await advance(1000);
    expect(sendCommand).toHaveBeenCalledTimes(1);
  });
});
