import { useCallback, useLayoutEffect, useRef, useState } from 'react';

import { SimulationApiError } from './SimulationApiError';
import {
  CHARGE_KEY_CODE,
  SHIELD_KEY_CODE,
  ROTATE_LEFT_KEY_CODE,
  ROTATE_RIGHT_KEY_CODE,
  THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS,
  THRUST_BRAKE_KEY_CODE,
  INPUT_COMMAND_BUDGET_CAPACITY,
  INPUT_COMMAND_BUDGET_REFILL_PER_SECOND,
  INPUT_COMMAND_PULSE_MINIMUM_TOKENS,
} from './simulationConstants';
import type {
  SessionCommand,
  SessionCommandKind,
} from './simulationProtocolTypes';
import type {
  SimulationCommandSender,
  SimulationSessionIdentity,
} from './useSimulationConnection';

export interface ThrustDirection {
  readonly x: number;
  readonly y: number;
}

/**
 * The two pulse abilities a session can activate, named by the command kinds protocol v3 already
 * registers rather than by a fresh string union of this file's own: `Extract` makes a renamed or
 * retired kind a compile error here instead of a binding that silently sends nothing.
 */
export type SimulationAbility = Extract<
  SessionCommandKind,
  'charge' | 'shield'
>;

/**
 * Per-ability activation suppression the client can already see this frame, keyed so a caller
 * cannot supply one ability and silently forget the other.
 *
 * A live published cooldown is the member this hook exists to obey -- it is the contract's primary
 * gameplay gate; the separate shared token bucket bounds transport traffic. This is "cannot act now" rather
 * than "cooling", because the composition belongs beside the control that also explains it to the
 * player: an unadvertised command kind and active shield protection blocking charge are equally
 * visible client-side, and a key press must never do what the button says is impossible.
 */
export type AbilityUnavailability = Readonly<
  Record<SimulationAbility, boolean>
>;

export interface ThrustInputOptions {
  /** False while this session owns no body: an eliminated or deferred player steers nothing. */
  readonly enabled: boolean;
  readonly session: SimulationSessionIdentity | null;
  readonly ownEntityId: number | null;
  /** Authoritative snapshot containment; elapsed browser time never unlocks input. */
  readonly inputLocked: boolean;
  /** Absence means this entity has never invalidated input; a present token is positive. */
  readonly inputGeneration: number | undefined;
  /** Published reasons an ability cannot act now; changing it never disturbs held thrust. */
  readonly abilityUnavailable: AbilityUnavailability;
  readonly rotationUnavailable: boolean;
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
  /**
   * Stable activation boundary for an on-screen button, and the same guarded path the ability keys
   * take: availability, the rate discipline, the generation token and the charge direction are all
   * resolved inside the effect, so a button cannot activate something a key could not. It returns
   * nothing on purpose -- a successful send is not an activation, and only the published component
   * proves the tick admitted the pulse -- so no caller can render readiness from having called it.
   */
  readonly activateAbility: (ability: SimulationAbility) => void;
  readonly rotateVelocity: (direction: 'left' | 'right') => void;
  readonly braking: boolean;
  /** Local transmission availability, independent of any gameplay cooldown. */
  readonly commandBudgetUnavailable: boolean;
}

interface ThrustInputView {
  readonly braking: boolean;
  readonly commandBudgetUnavailable: boolean;
  readonly direction: ThrustDirection;
  readonly aimDirection: ThrustDirection;
  readonly lastNonzeroAimDirection: ThrustDirection | null;
}

/**
 * What one body's input carries across an effect rebuild. Named for the session's input rather than
 * for thrust alone: remembered aim and the last transmitted level belong to the body, while the
 * command budget separately belongs to the session and survives respawn.
 */
interface InputTransmission {
  readonly session: SimulationSessionIdentity | null;
  readonly ownEntityId: number | null;
  readonly enabled: boolean;
  readonly observedInputGeneration: number | undefined;
  inputGeneration: number | undefined;
  direction: ThrustDirection;
  sentAtMilliseconds: number;
  lastNonzeroAimDirection: ThrustDirection | null;
  braking: boolean;
}

const ZERO_THRUST: ThrustDirection = Object.freeze({ x: 0, y: 0 });
const EMPTY_INPUT_VIEW: ThrustInputView = Object.freeze({
  braking: false,
  commandBudgetUnavailable: false,
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

/**
 * The direction one charge would commit, and the one rule both the send and any availability
 * readout may use. Current aim wins whenever it exists; an exact-centre cursor falls back to the
 * remembered nonzero aim, which is the opposite of the centre-thrust rule above and deliberately
 * so: a level with no direction is zero thrust, while a pulse with no direction is a zero vector
 * the server refuses outright, and remembered aim is what this owner has always kept "available for
 * later abilities". Null means no aim has ever existed on this body -- the cursor has never been
 * inside the canvas -- and there is no client-authored default that would not be the client
 * choosing a player's direction for them.
 *
 * Deliberately not exported. Its null case is the same fact an availability readout needs -- "no
 * aim yet" -- but that reader already has it from the published `lastNonzeroAimDirection`, which is
 * null in exactly the same frames, and the direction itself must never be resolved outside this
 * effect: the exposed React state can lag these locals by a commit, so a button's `onClick` reading
 * it would charge along the aim of the frame before last.
 */
function chargeDirection(
  aimDirection: ThrustDirection,
  lastNonzeroAimDirection: ThrustDirection | null,
): ThrustDirection | null {
  return sameDirection(aimDirection, ZERO_THRUST)
    ? lastNonzeroAimDirection
    : aimDirection;
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

interface InputCommandBudget {
  readonly session: SimulationSessionIdentity | null;
  tokens: number;
  updatedAtMilliseconds: number;
}

/**
 * @canonical thrust_input -- cursor aim, held left mouse Go, Space brakes and pulse commands.
 * Levels are change-only and coalesce to the latest state. One per-session token bucket bounds
 * levels and pulses together; pulses reserve a release token and never queue. Session/body/generation
 * retirement discards pending levels, while only a new session resets the budget. A confirmed hit
 * may therefore recharge immediately without an artificial per-ability delay.
 */
export function useThrustInput({
  enabled,
  session,
  ownEntityId,
  inputLocked,
  inputGeneration,
  abilityUnavailable,
  rotationUnavailable,
  sendCommand,
}: ThrustInputOptions): ThrustInputControls {
  const [view, setView] = useState<ThrustInputView>(EMPTY_INPUT_VIEW);
  const observer = useRef<ThrustInputControls['observeAim'] | null>(null);
  const activator = useRef<ThrustInputControls['activateAbility'] | null>(null);
  const transmission = useRef<InputTransmission | null>(null);
  const rotator = useRef<ThrustInputControls['rotateVelocity'] | null>(null);
  const budgetReference = useRef<InputCommandBudget | null>(null);
  const rotationSuppressed = useRef(rotationUnavailable);
  const unavailable = useRef(abilityUnavailable);
  const observeAim = useCallback(
    (observation: ThrustAimObservation | null): void => {
      observer.current?.(observation);
    },
    [],
  );
  const activateAbility = useCallback((ability: SimulationAbility): void => {
    activator.current?.(ability);
  }, []);

  const rotateVelocity = useCallback((direction: 'left' | 'right'): void => {
    rotator.current?.(direction);
  }, []);

  // Availability moves with almost every snapshot, and the input effect must not be rebuilt when it
  // does: a rebuild re-declares `goHeld`, so a cooldown merely starting would drop the thrust a
  // player is holding. This ref is that seam. It is written after every commit and read only from
  // inside an event handler, so an activation always sees the last committed frame while the effect
  // keeps the narrow lifetime dependency list Step 11a gave it.
  useLayoutEffect(() => {
    unavailable.current = abilityUnavailable;
    rotationSuppressed.current = rotationUnavailable;
  });

  useLayoutEffect(() => {
    const bodyAvailable = enabled && session !== null && ownEntityId !== null;
    const canSend = bodyAvailable && !inputLocked;
    let active = true;
    let hasObservation = false;
    let cameraGestureActive = false;
    let goHeld = false;
    let brakeHeld = false;
    let awaitingFreshActivation = false;
    let sendRequiredAfterRefusal = false;
    let aimDirection = ZERO_THRUST;
    // A pulse has no held state to send, but it has held state to *observe*: `event.repeat` is the
    // browser's claim and a synthetic or non-conforming event may omit it, so the second half of
    // the go key's `event.repeat || goHeld` rule is per-ability here too.
    const heldPulseKeys = new Set<string>();
    const previousTransmission = transmission.current;
    const sameBody =
      bodyAvailable &&
      previousTransmission?.enabled === true &&
      previousTransmission.session === session &&
      previousTransmission.ownEntityId === ownEntityId;
    const sameGeneration =
      sameBody &&
      previousTransmission.observedInputGeneration === inputGeneration;
    // Sender-only replacement cancels the old command with its original activation token. A
    // generation change instead discards it: even an old zero must never reach the new generation.
    let activationGeneration = sameGeneration
      ? previousTransmission.inputGeneration
      : undefined;
    const inheritedTransmission = sameBody ? previousTransmission : null;
    const currentTransmission: InputTransmission = {
      session,
      ownEntityId,
      enabled: bodyAvailable,
      observedInputGeneration: inputGeneration,
      inputGeneration: activationGeneration,
      direction:
        sameGeneration && !inputLocked
          ? previousTransmission.direction
          : ZERO_THRUST,
      sentAtMilliseconds: sameBody
        ? previousTransmission.sentAtMilliseconds
        : Number.NEGATIVE_INFINITY,
      // Remembered aim is discarded by the contracted set and by nothing else: body loss, body
      // replacement, a new welcome, disconnect -- exactly `sameBody`. A stun changes both
      // `inputLocked` and `inputGeneration`, and both rebuild this effect, so a closure-local reset
      // wiped the remembered aim on every stun: precisely the off-canvas, just-unstunned case the
      // last-nonzero fallback exists to cover.
      lastNonzeroAimDirection:
        inheritedTransmission?.lastNonzeroAimDirection ?? null,
      braking:
        sameGeneration && !inputLocked ? previousTransmission.braking : false,
    };
    transmission.current = currentTransmission;
    let lastNonzeroAimDirection = currentTransmission.lastNonzeroAimDirection;
    let pendingSendTimer: ReturnType<typeof setTimeout> | null = null;
    let budgetTimer: ReturnType<typeof setTimeout> | null = null;
    if (
      budgetReference.current === null ||
      budgetReference.current.session !== session
    ) {
      budgetReference.current = {
        session,
        tokens: INPUT_COMMAND_BUDGET_CAPACITY,
        updatedAtMilliseconds: performance.now(),
      };
    }
    const budget = budgetReference.current;
    const refillBudget = (): void => {
      const now = performance.now();
      budget.tokens = Math.min(
        INPUT_COMMAND_BUDGET_CAPACITY,
        budget.tokens +
          (Math.max(0, now - budget.updatedAtMilliseconds) *
            INPUT_COMMAND_BUDGET_REFILL_PER_SECOND) /
            1000,
      );
      budget.updatedAtMilliseconds = now;
    };

    const clearPendingSend = (): void => {
      if (pendingSendTimer !== null) {
        clearTimeout(pendingSendTimer);
        pendingSendTimer = null;
      }
    };

    const publishView = (): void => {
      const desired = goHeld && !brakeHeld ? aimDirection : ZERO_THRUST;
      refillBudget();
      const nextView = {
        braking: brakeHeld,
        commandBudgetUnavailable:
          budget.tokens < INPUT_COMMAND_PULSE_MINIMUM_TOKENS,
        direction: desired,
        aimDirection,
        lastNonzeroAimDirection,
      };
      setView((previous) =>
        previous.braking === nextView.braking &&
        previous.commandBudgetUnavailable ===
          nextView.commandBudgetUnavailable &&
        sameDirection(previous.direction, nextView.direction) &&
        sameDirection(previous.aimDirection, nextView.aimDirection) &&
        sameDirection(
          previous.lastNonzeroAimDirection,
          nextView.lastNonzeroAimDirection,
        )
          ? previous
          : nextView,
      );
      if (budgetTimer !== null) clearTimeout(budgetTimer);
      budgetTimer = null;
      if (nextView.commandBudgetUnavailable) {
        budgetTimer = setTimeout(
          () => {
            budgetTimer = null;
            if (active) publishView();
          },
          Math.ceil(
            ((INPUT_COMMAND_PULSE_MINIMUM_TOKENS - budget.tokens) * 1000) /
              INPUT_COMMAND_BUDGET_REFILL_PER_SECOND,
          ),
        );
      }
    };

    const flush = (beforePulse = false): void => {
      if (!active) return;
      const desired = goHeld && !brakeHeld ? aimDirection : ZERO_THRUST;
      publishView();
      if (
        !canSend ||
        awaitingFreshActivation ||
        (!sendRequiredAfterRefusal &&
          sameDirection(currentTransmission.direction, desired) &&
          currentTransmission.braking === brakeHeld &&
          currentTransmission.inputGeneration === activationGeneration)
      ) {
        clearPendingSend();
        return;
      }

      const now = performance.now();
      refillBudget();
      const waitMilliseconds = Math.max(
        beforePulse
          ? 0
          : THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS -
              (now - currentTransmission.sentAtMilliseconds),
        ((1 - budget.tokens) * 1000) / INPUT_COMMAND_BUDGET_REFILL_PER_SECOND,
      );
      if (waitMilliseconds > 0) {
        if (pendingSendTimer === null) {
          pendingSendTimer = setTimeout(() => {
            pendingSendTimer = null;
            flush();
          }, Math.ceil(waitMilliseconds));
        }
        return;
      }
      clearPendingSend();
      // Reserve the interval before invoking a capability that may synchronously retire this
      // closure. A same-body replacement must inherit the attempt's timestamp, even before return.
      const previousSentAtMilliseconds = currentTransmission.sentAtMilliseconds;
      currentTransmission.sentAtMilliseconds = now;
      budget.tokens -= 1;
      const brakingPayload =
        brakeHeld || currentTransmission.braking ? { braking: brakeHeld } : {};
      const submitted = sendCommand({
        kind: 'set_thrust',
        payload:
          activationGeneration === undefined
            ? { ...desired, ...brakingPayload }
            : {
                ...desired,
                ...brakingPayload,
                input_generation: activationGeneration,
              },
      });
      // The capability may synchronously replace this welcome/body while reporting its result.
      // A retired closure cannot publish old aim or mutate the replacement lifetime afterward.
      if (!active) return;
      if (submitted) {
        currentTransmission.direction = desired;
        currentTransmission.braking = brakeHeld;
        currentTransmission.inputGeneration = activationGeneration;
        currentTransmission.sentAtMilliseconds = now;
        sendRequiredAfterRefusal = false;
      } else {
        currentTransmission.sentAtMilliseconds = previousSentAtMilliseconds;
        goHeld = false;
        brakeHeld = false;
        awaitingFreshActivation = true;
        sendRequiredAfterRefusal = true;
        publishView();
      }
      publishView();
    };

    const cancelActivation = (): void => {
      // Every cancellation source releases the ability latches as well as go. Blur is the reason
      // this is not left to keyup: a blur swallows the release, and a latch that outlives the press
      // wedges the key -- the next real press reads as a repeat and the ability never fires again.
      heldPulseKeys.clear();
      goHeld = false;
      brakeHeld = false;
      flush();
    };

    const observe = (observation: ThrustAimObservation | null): void => {
      if (!active || !canSend) return;
      const nextAim =
        observation === null ? ZERO_THRUST : directionForAim(observation);
      hasObservation = observation !== null;
      cameraGestureActive = observation?.cameraGestureActive ?? false;
      aimDirection = nextAim;
      if (!sameDirection(nextAim, ZERO_THRUST)) {
        lastNonzeroAimDirection = nextAim;
        currentTransmission.lastNonzeroAimDirection = nextAim;
      }
      if (!hasObservation || cameraGestureActive) {
        goHeld = false;
      }
      // Braking has no heading requirement. Aim can disappear every paint while the pointer is
      // outside the arena; only a camera gesture or an explicit cancellation retires the hold.
      if (cameraGestureActive) {
        brakeHeld = false;
      }
      flush();
    };
    observer.current = observe;

    /**
     * The two payloads are encoded separately because the two schemas differ, and copying one onto
     * the other fails silently. `shield` *requires* `input_generation` and spells the
     * never-invalidated entity as an explicit `null`; reusing `set_thrust`'s omit-when-absent
     * expression there produces `{}`, which the closed envelope rejects inside `sendCommand`, so
     * the pulse would die in a console warning for every player who has never been stunned -- that
     * is, everyone, on their first shield of the match. `charge` is the one that matches
     * `set_thrust` exactly, because it always carries `x` and `y` and can never be truncated into a
     * defaulted pulse.
     *
     * The token is the generation this closure was built with, not the go key's captured
     * `activationGeneration`: a pulse is one discrete intent stating the generation the client
     * believes it holds right now, where a held thrust deliberately keeps the token of the press
     * that started it until that hold retires.
     */
    const abilityCommand = (
      ability: SimulationAbility,
    ): SessionCommand | null => {
      if (ability === 'shield') {
        return {
          kind: 'shield',
          payload: { input_generation: inputGeneration ?? null },
        };
      }
      const direction = chargeDirection(aimDirection, lastNonzeroAimDirection);
      if (direction === null) {
        return null;
      }
      return {
        kind: 'charge',
        payload:
          inputGeneration === undefined
            ? direction
            : { ...direction, input_generation: inputGeneration },
      };
    };

    // Pulses are immediate attempts. Latest held state gets priority, and an exhausted pulse is
    // visibly unavailable rather than retained for a later frame or disguised as a cooldown.
    const sendPulse = (command: SessionCommand): void => {
      // The pulse must observe the latest brake/Go level at the server. Spending from the same
      // bucket permits this ordered pair without bypassing the session's total traffic bound.
      flush(true);
      if (!active || pendingSendTimer !== null || sendRequiredAfterRefusal)
        return;
      refillBudget();
      if (budget.tokens < INPUT_COMMAND_PULSE_MINIMUM_TOKENS) {
        publishView();
        return;
      }
      budget.tokens -= 1;
      sendCommand(command);
      if (active) publishView();
    };
    const activate = (ability: SimulationAbility): void => {
      if (
        !active ||
        !canSend ||
        cameraGestureActive ||
        unavailable.current[ability] ||
        (ability === 'charge' && brakeHeld)
      )
        return;
      const command = abilityCommand(ability);
      if (command !== null) sendPulse(command);
    };
    const rotate = (direction: 'left' | 'right'): void => {
      if (
        !active ||
        !canSend ||
        cameraGestureActive ||
        rotationSuppressed.current
      )
        return;
      sendPulse({
        kind: 'rotate_velocity',
        payload:
          inputGeneration === undefined
            ? { direction }
            : { direction, input_generation: inputGeneration },
      });
    };
    activator.current = activate;
    rotator.current = rotate;
    const pulseForCode = (code: string): (() => void) | null => {
      if (code === SHIELD_KEY_CODE) return () => activate('shield');
      if (code === CHARGE_KEY_CODE) return () => activate('charge');
      if (code === ROTATE_LEFT_KEY_CODE) return () => rotate('left');
      if (code === ROTATE_RIGHT_KEY_CODE) return () => rotate('right');
      return null;
    };

    const isUiInteraction = (event: KeyboardEvent): boolean =>
      blocksGameplayInput(event.target) ||
      blocksGameplayInput(document.activeElement);

    const handleKeyDown = (event: KeyboardEvent): void => {
      if (isUiInteraction(event)) {
        cancelActivation();
        return;
      }
      const pulse = pulseForCode(event.code);
      if (pulse !== null) {
        // This list decides whether the key is *ours* to take from the browser, which is why it
        // repeats `canSend` and the camera gesture that `activate` also checks: an ability nobody
        // could activate leaves the code alone, and `activate` still holds the authoritative copy
        // for the button, which passes through no key guard at all.
        if (
          event.altKey ||
          event.ctrlKey ||
          event.metaKey ||
          event.isComposing ||
          event.defaultPrevented ||
          !canSend ||
          cameraGestureActive
        ) {
          return;
        }
        event.preventDefault();
        if (event.repeat || heldPulseKeys.has(event.code)) {
          return;
        }
        // Latched on every accepted press, not only on one that sends: the latch records that the
        // key is physically down, so a suppressed activation still refuses the repeat behind it.
        heldPulseKeys.add(event.code);
        pulse();
        return;
      }
      if (
        event.code !== THRUST_BRAKE_KEY_CODE ||
        event.altKey ||
        event.ctrlKey ||
        event.metaKey ||
        event.isComposing ||
        event.defaultPrevented ||
        !canSend ||
        cameraGestureActive
      ) {
        return;
      }
      event.preventDefault();
      if (event.repeat || brakeHeld) {
        return;
      }
      awaitingFreshActivation = false;
      activationGeneration = inputGeneration;
      brakeHeld = true;
      flush();
    };

    const handleKeyUp = (event: KeyboardEvent): void => {
      if (isUiInteraction(event)) {
        cancelActivation();
        return;
      }
      const pulse = pulseForCode(event.code);
      if (pulse !== null) {
        // A release sends nothing -- the server holds no ability state a client could clear -- so
        // the only work here is dropping the latch, and only for a press this owner actually took.
        if (!heldPulseKeys.delete(event.code)) {
          return;
        }
        event.preventDefault();
        return;
      }
      if (event.code !== THRUST_BRAKE_KEY_CODE || !brakeHeld) {
        return;
      }
      event.preventDefault();
      brakeHeld = false;
      flush();
    };

    const handleUiInteraction = (event: Event): void => {
      if (blocksGameplayInput(event.target)) {
        cancelActivation();
      }
    };

    const handlePointerDown = (event: globalThis.PointerEvent): void => {
      handleUiInteraction(event);
      if (
        event.button !== 0 ||
        !event.isPrimary ||
        event.pointerType !== 'mouse' ||
        !(event.target instanceof HTMLCanvasElement) ||
        event.target.dataset.gameplaySurface !== 'arena' ||
        event.altKey ||
        event.ctrlKey ||
        event.metaKey ||
        event.defaultPrevented ||
        blocksGameplayInput(document.activeElement) ||
        !canSend ||
        !hasObservation ||
        cameraGestureActive ||
        goHeld
      )
        return;
      event.preventDefault();
      awaitingFreshActivation = false;
      activationGeneration = inputGeneration;
      goHeld = true;
      flush();
    };
    const handlePointerUp = (event: globalThis.PointerEvent): void => {
      if (event.button !== 0 || !goHeld) return;
      goHeld = false;
      flush();
    };

    const handlePointerMove = (event: globalThis.PointerEvent): void => {
      // Releasing left while right remains held is pointermove, not pointerup. A held-state
      // observation can release Go but never re-arm it after cancellation or body replacement.
      if (
        goHeld &&
        event.isPrimary &&
        event.pointerType === 'mouse' &&
        (event.buttons & 1) === 0
      ) {
        goHeld = false;
        flush();
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    window.addEventListener('keyup', handleKeyUp);
    window.addEventListener('blur', cancelActivation);
    window.addEventListener('focusin', handleUiInteraction);
    window.addEventListener('pointerdown', handlePointerDown);
    window.addEventListener('pointerup', handlePointerUp);
    window.addEventListener('pointermove', handlePointerMove);
    window.addEventListener('pointercancel', cancelActivation);
    window.addEventListener('contextmenu', cancelActivation);
    flush();

    return () => {
      window.removeEventListener('keydown', handleKeyDown);
      window.removeEventListener('keyup', handleKeyUp);
      active = false;
      if (observer.current === observe) observer.current = null;
      if (activator.current === activate) activator.current = null;
      if (rotator.current === rotate) rotator.current = null;
      if (budgetTimer !== null) clearTimeout(budgetTimer);
      window.removeEventListener('blur', cancelActivation);
      window.removeEventListener('focusin', handleUiInteraction);
      window.removeEventListener('pointerdown', handlePointerDown);
      window.removeEventListener('pointerup', handlePointerUp);
      window.removeEventListener('pointermove', handlePointerMove);
      window.removeEventListener('pointercancel', cancelActivation);
      window.removeEventListener('contextmenu', cancelActivation);
      clearPendingSend();
    };
  }, [
    enabled,
    session,
    ownEntityId,
    inputLocked,
    inputGeneration,
    sendCommand,
  ]);

  return { ...view, observeAim, activateAbility, rotateVelocity };
}
