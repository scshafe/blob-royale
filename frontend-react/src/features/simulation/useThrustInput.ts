import { useCallback, useLayoutEffect, useRef, useState } from 'react';

import { SimulationApiError } from './SimulationApiError';
import {
  THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS,
  THRUST_GO_KEY_CODE,
} from './simulationConstants';
import type {
  SimulationCommandSender,
  SimulationSessionIdentity,
} from './useSimulationConnection';

export interface ThrustDirection {
  readonly x: number;
  readonly y: number;
}

export interface ThrustInputOptions {
  /** False while this session owns no body: an eliminated or deferred player steers nothing. */
  readonly enabled: boolean;
  readonly session: SimulationSessionIdentity | null;
  readonly ownEntityId: number | null;
  readonly sendCommand: SimulationCommandSender;
}

/** Current pointer and rendered body centre in logical CSS pixels, never backing-buffer pixels. */
export interface ThrustAimObservation {
  readonly pointer: ThrustDirection;
  readonly projectedBodyCenter: ThrustDirection;
  readonly cameraGestureActive: boolean;
}

export interface ThrustInputControls {
  readonly direction: ThrustDirection;
  readonly aimDirection: ThrustDirection;
  readonly lastNonzeroAimDirection: ThrustDirection | null;
  /** Stable observational boundary; null means this surface has no usable cursor/body geometry. */
  readonly observeAim: (observation: ThrustAimObservation | null) => void;
}

interface ThrustInputView {
  readonly direction: ThrustDirection;
  readonly aimDirection: ThrustDirection;
  readonly lastNonzeroAimDirection: ThrustDirection | null;
}

interface ThrustTransmission {
  readonly session: SimulationSessionIdentity | null;
  readonly ownEntityId: number | null;
  readonly enabled: boolean;
  direction: ThrustDirection;
  sentAtMilliseconds: number;
}

const ZERO_THRUST: ThrustDirection = Object.freeze({ x: 0, y: 0 });
const EMPTY_INPUT_VIEW: ThrustInputView = Object.freeze({
  direction: ZERO_THRUST,
  aimDirection: ZERO_THRUST,
  lastNonzeroAimDirection: null,
});

/**
 * Canvas projection already shares the world's downward-positive y axis. Pointer distance is
 * discarded here, at the one normalization owner; exact centre never reuses remembered aim.
 */
function directionForAim(observation: ThrustAimObservation): ThrustDirection {
  const x = observation.pointer.x - observation.projectedBodyCenter.x;
  const y = observation.pointer.y - observation.projectedBodyCenter.y;
  const magnitude = Math.hypot(x, y);
  if (
    !Number.isFinite(observation.pointer.x) ||
    !Number.isFinite(observation.pointer.y) ||
    !Number.isFinite(observation.projectedBodyCenter.x) ||
    !Number.isFinite(observation.projectedBodyCenter.y) ||
    !Number.isFinite(magnitude)
  ) {
    throw new SimulationApiError(
      'SIMULATION.SESSION_INVARIANT_VIOLATION',
      'Cannot normalize cursor aim from non-finite canvas geometry.',
      { context: { observation, operation: 'normalize cursor aim' } },
    );
  }
  if (magnitude === 0) {
    return ZERO_THRUST;
  }
  return Object.freeze({
    x: x === 0 ? 0 : x / magnitude,
    y: y === 0 ? 0 : y / magnitude,
  });
}

function sameDirection(
  left: ThrustDirection | null,
  right: ThrustDirection | null,
): boolean {
  return (
    left === right ||
    (left !== null &&
      right !== null &&
      left.x === right.x &&
      left.y === right.y)
  );
}

/** Native UI activation belongs to the focused control, including the camera's Space buttons. */
function blocksGameplayInput(target: EventTarget | null): boolean {
  return (
    target instanceof Element &&
    target.closest(
      'input, textarea, select, button, a[href], summary, [role="button"], [role="link"], [contenteditable]:not([contenteditable="false"]), [data-gameplay-input="blocked"]',
    ) !== null
  );
}

/**
 * @canonical thrust_input -- cursor aim, held Space, and the sole change-only thrust sender.
 * @extension-point thrust_aim_observation -- Canvas publishes projected CSS geometry, never commands.
 *
 * Sends `set_thrust` on change and at most once every 50 ms, never once per frame: a thrust is a
 * level that persists on the server until the next command. Aim can change independently of go;
 * remembered nonzero aim is available for later abilities but never substitutes for centre thrust.
 * Null geometry, camera gestures, UI interaction, and blur cancel activation, replacing any queued
 * nonzero with zero through this same timer. Resuming requires a fresh Space press, not key repeat.
 * A locally refused send also cancels go; observation/render feedback never retries it. Only a
 * fresh activation may make another attempt. Throttling uses monotonic elapsed time, not wall time.
 *
 * enabled must include actual physics_body presence. The immutable welcome, owned entity ID, and
 * observed availability delimit input lifetime; fresh snapshot/body objects do not. Replacement
 * or disconnect discards pending work and remembered aim, never replaying it into a new body.
 * Between-snapshot same-entity recreation has no wire identity and cannot be inferred here.
 *
 * The layout effect establishes this owner before Canvas publishes passive-effect geometry;
 * pointer handlers call the stable observer directly. Invalid numeric geometry raises
 * SIMULATION.SESSION_INVARIANT_VIOLATION rather than inventing a usable direction.
 */
export function useThrustInput({
  enabled,
  session,
  ownEntityId,
  sendCommand,
}: ThrustInputOptions): ThrustInputControls {
  const [view, setView] = useState<ThrustInputView>(EMPTY_INPUT_VIEW);
  const observer = useRef<ThrustInputControls['observeAim'] | null>(null);
  const transmission = useRef<ThrustTransmission | null>(null);
  const observeAim = useCallback(
    (observation: ThrustAimObservation | null): void => {
      observer.current?.(observation);
    },
    [],
  );

  useLayoutEffect(() => {
    const canSend = enabled && session !== null && ownEntityId !== null;
    let active = true;
    let hasObservation = false;
    let cameraGestureActive = false;
    let goHeld = false;
    let awaitingFreshActivation = false;
    let sendRequiredAfterRefusal = false;
    let aimDirection = ZERO_THRUST;
    let lastNonzeroAimDirection: ThrustDirection | null = null;
    const previousTransmission = transmission.current;
    const sameBody =
      canSend &&
      previousTransmission?.enabled === true &&
      previousTransmission.session === session &&
      previousTransmission.ownEntityId === ownEntityId;
    // Only replacing the sender leaves the same body's last command in force. Cancel it with the
    // new capability, preserving its throttle; a newly seated body already starts at rest.
    const currentTransmission: ThrustTransmission = {
      session,
      ownEntityId,
      enabled: canSend,
      direction: sameBody ? previousTransmission.direction : ZERO_THRUST,
      sentAtMilliseconds: sameBody
        ? previousTransmission.sentAtMilliseconds
        : Number.NEGATIVE_INFINITY,
    };
    transmission.current = currentTransmission;
    let pendingSendTimer: ReturnType<typeof setTimeout> | null = null;

    const clearPendingSend = (): void => {
      if (pendingSendTimer !== null) {
        clearTimeout(pendingSendTimer);
        pendingSendTimer = null;
      }
    };

    const publishView = (): void => {
      const desired = goHeld ? aimDirection : ZERO_THRUST;
      const nextView = {
        direction: desired,
        aimDirection,
        lastNonzeroAimDirection,
      };
      setView((previous) =>
        sameDirection(previous.direction, nextView.direction) &&
        sameDirection(previous.aimDirection, nextView.aimDirection) &&
        sameDirection(
          previous.lastNonzeroAimDirection,
          nextView.lastNonzeroAimDirection,
        )
          ? previous
          : nextView,
      );
    };

    const flush = (): void => {
      if (!active) return;
      const desired = goHeld ? aimDirection : ZERO_THRUST;
      publishView();
      if (
        !canSend ||
        awaitingFreshActivation ||
        (!sendRequiredAfterRefusal &&
          sameDirection(currentTransmission.direction, desired))
      ) {
        clearPendingSend();
        return;
      }

      const now = performance.now();
      const millisecondsSinceSend =
        now - currentTransmission.sentAtMilliseconds;
      if (millisecondsSinceSend < THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS) {
        if (pendingSendTimer === null) {
          pendingSendTimer = setTimeout(() => {
            pendingSendTimer = null;
            flush();
          }, THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS - millisecondsSinceSend);
        }
        return;
      }
      clearPendingSend();
      const submitted = sendCommand({ kind: 'set_thrust', payload: desired });
      // The capability may synchronously replace this welcome/body while reporting its result.
      // A retired closure cannot publish old aim or mutate the replacement lifetime afterward.
      if (!active) return;
      if (submitted) {
        currentTransmission.direction = desired;
        currentTransmission.sentAtMilliseconds = now;
        sendRequiredAfterRefusal = false;
      } else {
        goHeld = false;
        awaitingFreshActivation = true;
        sendRequiredAfterRefusal = true;
        publishView();
      }
    };

    const cancelActivation = (): void => {
      goHeld = false;
      flush();
    };

    const observe = (observation: ThrustAimObservation | null): void => {
      if (!active || !canSend) return;
      const nextAim =
        observation === null ? ZERO_THRUST : directionForAim(observation);
      hasObservation = observation !== null;
      cameraGestureActive = observation?.cameraGestureActive ?? false;
      aimDirection = nextAim;
      if (!sameDirection(nextAim, ZERO_THRUST))
        lastNonzeroAimDirection = nextAim;
      if (!hasObservation || cameraGestureActive) goHeld = false;
      flush();
    };
    observer.current = observe;

    const isUiInteraction = (event: KeyboardEvent): boolean =>
      blocksGameplayInput(event.target) ||
      blocksGameplayInput(document.activeElement);

    const handleKeyDown = (event: KeyboardEvent): void => {
      if (isUiInteraction(event)) {
        cancelActivation();
        return;
      }
      if (
        event.code !== THRUST_GO_KEY_CODE ||
        event.altKey ||
        event.ctrlKey ||
        event.metaKey ||
        event.isComposing ||
        event.defaultPrevented ||
        !canSend ||
        !hasObservation ||
        cameraGestureActive
      ) {
        return;
      }
      event.preventDefault();
      if (event.repeat || goHeld) {
        return;
      }
      awaitingFreshActivation = false;
      goHeld = true;
      flush();
    };

    const handleKeyUp = (event: KeyboardEvent): void => {
      if (isUiInteraction(event)) {
        cancelActivation();
        return;
      }
      if (event.code !== THRUST_GO_KEY_CODE || !goHeld) {
        return;
      }
      event.preventDefault();
      cancelActivation();
    };

    const handleUiInteraction = (event: Event): void => {
      if (blocksGameplayInput(event.target)) {
        cancelActivation();
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('keyup', handleKeyUp);
    window.addEventListener('blur', cancelActivation);
    window.addEventListener('focusin', handleUiInteraction);
    window.addEventListener('pointerdown', handleUiInteraction);
    window.addEventListener('contextmenu', cancelActivation);
    flush();

    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('keyup', handleKeyUp);
      active = false;
      if (observer.current === observe) observer.current = null;
      window.removeEventListener('blur', cancelActivation);
      window.removeEventListener('focusin', handleUiInteraction);
      window.removeEventListener('pointerdown', handleUiInteraction);
      window.removeEventListener('contextmenu', cancelActivation);
      clearPendingSend();
    };
  }, [enabled, session, ownEntityId, sendCommand]);

  return { ...view, observeAim };
}
