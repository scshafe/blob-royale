import { useEffect, useRef, useState } from 'react';

import { THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS } from './simulationConstants';
import type { SimulationCommandSender } from './useSimulationConnection';

export interface ThrustDirection {
  readonly x: number;
  readonly y: number;
}

export interface ThrustInputOptions {
  /** False while this session owns no body: an eliminated or deferred player steers nothing. */
  readonly enabled: boolean;
  readonly sendCommand: SimulationCommandSender;
}

const ZERO_THRUST: ThrustDirection = Object.freeze({ x: 0, y: 0 });

/**
 * Keyboard steering, by physical key so a non-QWERTY layout steers with the same three fingers.
 * Screen y grows downward, which is also the world's y direction, so "up" is negative y.
 */
const THRUST_KEY_DIRECTIONS: Readonly<Record<string, ThrustDirection>> =
  Object.freeze({
    ArrowDown: Object.freeze({ x: 0, y: 1 }),
    ArrowLeft: Object.freeze({ x: -1, y: 0 }),
    ArrowRight: Object.freeze({ x: 1, y: 0 }),
    ArrowUp: Object.freeze({ x: 0, y: -1 }),
    KeyA: Object.freeze({ x: -1, y: 0 }),
    KeyD: Object.freeze({ x: 1, y: 0 }),
    KeyS: Object.freeze({ x: 0, y: 1 }),
    KeyW: Object.freeze({ x: 0, y: -1 }),
  });

function directionForPressedKeys(
  pressedKeys: ReadonlySet<string>,
): ThrustDirection {
  let x = 0;
  let y = 0;
  for (const pressedKey of pressedKeys) {
    const direction = THRUST_KEY_DIRECTIONS[pressedKey];
    if (direction !== undefined) {
      x += direction.x;
      y += direction.y;
    }
  }

  const magnitude = Math.hypot(x, y);
  if (magnitude === 0) {
    return ZERO_THRUST;
  }
  // A unit direction, so a diagonal never asks for more thrust than a cardinal. The mode clamps
  // magnitude as well; sending a unit vector means the two never disagree about what was intended.
  return Object.freeze({ x: x / magnitude, y: y / magnitude });
}

function sameDirection(
  left: ThrustDirection | null,
  right: ThrustDirection,
): boolean {
  return left !== null && left.x === right.x && left.y === right.y;
}

/**
 * @canonical thrust_input -- the only place a keyboard becomes a command.
 *
 * Sends `set_thrust` on change and at most once every 50 ms, never once per frame: a thrust is a
 * level that persists on the server until the next command, so releasing a key MUST send zero and
 * holding one MUST send nothing further. Input is ignored entirely while this session owns no body.
 */
export function useThrustInput({
  enabled,
  sendCommand,
}: ThrustInputOptions): ThrustDirection {
  const [direction, setDirection] = useState<ThrustDirection>(ZERO_THRUST);
  const pressedKeys = useRef<Set<string>>(new Set<string>());
  const lastSentDirection = useRef<ThrustDirection | null>(null);
  const lastSentAtMilliseconds = useRef<number>(Number.NEGATIVE_INFINITY);
  const pendingSendTimer = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    const clearPendingSend = (): void => {
      if (pendingSendTimer.current !== null) {
        clearTimeout(pendingSendTimer.current);
        pendingSendTimer.current = null;
      }
    };

    const flush = (): void => {
      const desired = directionForPressedKeys(pressedKeys.current);
      setDirection(desired);
      if (!enabled || sameDirection(lastSentDirection.current, desired)) {
        return;
      }

      const now = Date.now();
      const millisecondsSinceSend = now - lastSentAtMilliseconds.current;
      if (millisecondsSinceSend < THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS) {
        if (pendingSendTimer.current === null) {
          pendingSendTimer.current = setTimeout(() => {
            pendingSendTimer.current = null;
            flush();
          }, THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS - millisecondsSinceSend);
        }
        return;
      }

      if (sendCommand({ kind: 'set_thrust', payload: desired })) {
        lastSentDirection.current = desired;
        lastSentAtMilliseconds.current = now;
      }
    };

    const handleKeyDown = (event: KeyboardEvent): void => {
      if (
        !(event.code in THRUST_KEY_DIRECTIONS) ||
        event.altKey ||
        event.ctrlKey ||
        event.metaKey
      ) {
        return;
      }
      event.preventDefault();
      if (event.repeat || pressedKeys.current.has(event.code)) {
        return;
      }
      pressedKeys.current.add(event.code);
      flush();
    };

    const handleKeyUp = (event: KeyboardEvent): void => {
      if (!pressedKeys.current.delete(event.code)) {
        return;
      }
      event.preventDefault();
      flush();
    };

    const handleBlur = (): void => {
      if (pressedKeys.current.size === 0) {
        return;
      }
      // A key released while the window is unfocused never reports keyup, and a thrust persists
      // until the next command, so a lost focus would otherwise leave a blob accelerating forever.
      pressedKeys.current.clear();
      flush();
    };

    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('keyup', handleKeyUp);
    window.addEventListener('blur', handleBlur);

    if (enabled) {
      // A newly seated body starts at rest, so zero is what the server already believes.
      lastSentDirection.current = ZERO_THRUST;
      flush();
    } else {
      clearPendingSend();
      lastSentDirection.current = null;
      setDirection(directionForPressedKeys(pressedKeys.current));
    }

    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('keyup', handleKeyUp);
      window.removeEventListener('blur', handleBlur);
      clearPendingSend();
    };
  }, [enabled, sendCommand]);

  return direction;
}
