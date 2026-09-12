import { useCallback, useLayoutEffect, useRef, useState } from 'react';

import { SimulationApiError } from './SimulationApiError';
import {
  ABILITY_COMMAND_MIN_INTERVAL_MILLISECONDS,
  CHARGE_KEY_CODE,
  SHIELD_KEY_CODE,
  THRUST_COMMAND_MIN_INTERVAL_MILLISECONDS,
  THRUST_GO_KEY_CODE,
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
 * rate mitigation, and the per-ability minimum interval below is only the backstop for the window
 * between a press and the next snapshot -- but the type is deliberately "cannot act now" rather
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
}

interface ThrustInputView {
  readonly direction: ThrustDirection;
  readonly aimDirection: ThrustDirection;
  readonly lastNonzeroAimDirection: ThrustDirection | null;
}

/**
 * What one body's input carries across an effect rebuild. Named for the session's input rather than
 * for thrust alone since Step 21: an ability's rate backstop and the remembered aim a charge reads
 * are body state, not effect state, and re-deriving them from nothing on every rebuild is exactly
 * the bug that made a stun wipe remembered aim.
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
  abilitySentAtMilliseconds: Record<SimulationAbility, number>;
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

/** A fresh mutable record per effect run; a same-body rebuild inherits the attempt timestamps. */
function abilitySendTimes(
  inherited: InputTransmission | null,
): Record<SimulationAbility, number> {
  return {
    charge:
      inherited?.abilitySentAtMilliseconds.charge ?? Number.NEGATIVE_INFINITY,
    shield:
      inherited?.abilitySentAtMilliseconds.shield ?? Number.NEGATIVE_INFINITY,
  };
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
 * A generation change retires held input and every pending command, including zero releases,
 * even if the entire stun was missed. Only a fresh Space press captures the current generation;
 * its aim updates and release retain that token until retirement.
 *
 * The layout effect establishes this owner before Canvas publishes passive-effect geometry;
 * pointer handlers call the stable observer directly. Invalid numeric geometry raises
 * SIMULATION.SESSION_INVARIANT_VIOLATION rather than inventing a usable direction.
 *
 * Step 21 added the two ability pulses to this same owner, and to nothing else: one keyboard, one
 * set of guards, one sender reference. They deliberately do *not* reuse `flush`. A pulse has no
 * level semantics for it to keep, and every part of that timer is wrong for one -- its change-only
 * gate would swallow a second identical press, its 50 ms coalescing would delay a press against an
 * 80 ms perfect opening, its single parked timer could drop one on any effect re-run, and its
 * refusal latch is shared with held thrust, so a refused ability send would drop a player's
 * propulsion. What the two paths share is `sendCommand` and the guards around it.
 *
 * The rate discipline is not optional and is not the thrust throttle: the per-session bucket is
 * capacity 30 refilling 20 per second, its token is charged before parsing, and an empty bucket is
 * a `1008` socket close rather than a refusal. A visible published cooldown suppresses an
 * activation outright, and `ABILITY_COMMAND_MIN_INTERVAL_MILLISECONDS` backstops the window before
 * the snapshot that would publish one. The two payloads are encoded separately because their
 * schemas differ: `shield` requires `input_generation` and spells the never-invalidated case as an
 * explicit `null`, where `charge` omits the member exactly as `set_thrust` does.
 */
export function useThrustInput({
  enabled,
  session,
  ownEntityId,
  inputLocked,
  inputGeneration,
  abilityUnavailable,
  sendCommand,
}: ThrustInputOptions): ThrustInputControls {
  const [view, setView] = useState<ThrustInputView>(EMPTY_INPUT_VIEW);
  const observer = useRef<ThrustInputControls['observeAim'] | null>(null);
  const activator = useRef<ThrustInputControls['activateAbility'] | null>(null);
  const transmission = useRef<InputTransmission | null>(null);
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

  // Availability moves with almost every snapshot, and the input effect must not be rebuilt when it
  // does: a rebuild re-declares `goHeld`, so a cooldown merely starting would drop the thrust a
  // player is holding. This ref is that seam. It is written after every commit and read only from
  // inside an event handler, so an activation always sees the last committed frame while the effect
  // keeps the narrow lifetime dependency list Step 11a gave it.
  useLayoutEffect(() => {
    unavailable.current = abilityUnavailable;
  });

  useLayoutEffect(() => {
    const bodyAvailable = enabled && session !== null && ownEntityId !== null;
    const canSend = bodyAvailable && !inputLocked;
    let active = true;
    let hasObservation = false;
    let cameraGestureActive = false;
    let goHeld = false;
    let awaitingFreshActivation = false;
    let sendRequiredAfterRefusal = false;
    let aimDirection = ZERO_THRUST;
    // A pulse has no held state to send, but it has held state to *observe*: `event.repeat` is the
    // browser's claim and a synthetic or non-conforming event may omit it, so the second half of
    // the go key's `event.repeat || goHeld` rule is per-ability here too.
    const heldAbilityKeys = new Set<SimulationAbility>();
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
      abilitySentAtMilliseconds: abilitySendTimes(inheritedTransmission),
    };
    transmission.current = currentTransmission;
    let lastNonzeroAimDirection = currentTransmission.lastNonzeroAimDirection;
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
          sameDirection(currentTransmission.direction, desired) &&
          currentTransmission.inputGeneration === activationGeneration)
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
      // Reserve the interval before invoking a capability that may synchronously retire this
      // closure. A same-body replacement must inherit the attempt's timestamp, even before return.
      const previousSentAtMilliseconds = currentTransmission.sentAtMilliseconds;
      currentTransmission.sentAtMilliseconds = now;
      const submitted = sendCommand({
        kind: 'set_thrust',
        payload:
          activationGeneration === undefined
            ? desired
            : { ...desired, input_generation: activationGeneration },
      });
      // The capability may synchronously replace this welcome/body while reporting its result.
      // A retired closure cannot publish old aim or mutate the replacement lifetime afterward.
      if (!active) return;
      if (submitted) {
        currentTransmission.direction = desired;
        currentTransmission.inputGeneration = activationGeneration;
        currentTransmission.sentAtMilliseconds = now;
        sendRequiredAfterRefusal = false;
      } else {
        currentTransmission.sentAtMilliseconds = previousSentAtMilliseconds;
        goHeld = false;
        awaitingFreshActivation = true;
        sendRequiredAfterRefusal = true;
        publishView();
      }
    };

    const cancelActivation = (): void => {
      // Every cancellation source releases the ability latches as well as go. Blur is the reason
      // this is not left to keyup: a blur swallows the release, and a latch that outlives the press
      // wedges the key -- the next real press reads as a repeat and the ability never fires again.
      heldAbilityKeys.clear();
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
      if (!sameDirection(nextAim, ZERO_THRUST)) {
        lastNonzeroAimDirection = nextAim;
        currentTransmission.lastNonzeroAimDirection = nextAim;
      }
      if (!hasObservation || cameraGestureActive) goHeld = false;
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

    /**
     * @canonical ability_activation -- the one guard an ability pulse passes, whether a key or an
     * on-screen button asked for it. A button that re-implemented any of this could activate
     * something a key could not, and the accessible control would be the unguarded one.
     *
     * It deliberately does *not* inherit `hasObservation`. That is a held-control rule -- thrust
     * needs live geometry because it steers with the cursor -- and applying it here would refuse
     * exactly the cursor-left-the-canvas case the remembered-aim fallback exists for, and would
     * make shield, which needs no direction at all, unusable for a keyboard-only player.
     *
     * The result of `sendCommand` is not consulted, and that is the point: a refusal must not touch
     * the thrust path's refusal latch, which cancels a player's propulsion and demands a fresh
     * press. The two paths share the sender, never its bookkeeping.
     */
    const activate = (ability: SimulationAbility): void => {
      if (
        !active ||
        !canSend ||
        cameraGestureActive ||
        unavailable.current[ability]
      ) {
        return;
      }
      const now = performance.now();
      const sentAtMilliseconds =
        currentTransmission.abilitySentAtMilliseconds[ability];
      if (
        now - sentAtMilliseconds <
        ABILITY_COMMAND_MIN_INTERVAL_MILLISECONDS
      ) {
        return;
      }
      const command = abilityCommand(ability);
      if (command === null) {
        return;
      }
      // Reserved before the call, exactly as the thrust path reserves its interval, because the
      // capability may synchronously retire this closure while reporting its result; a same-body
      // replacement then inherits the attempt. It is not restored on a local refusal: a pulse has
      // no retry to delay, and nothing a refusal here reports -- an unadvertised kind, a closed
      // socket, an envelope the schema rejects -- clears again within a third of a second.
      currentTransmission.abilitySentAtMilliseconds[ability] = now;
      sendCommand(command);
    };
    activator.current = activate;

    /** One code, one action: the pool ADR 0008 reserved supplies both, and nothing else binds. */
    const abilityForCode = (code: string): SimulationAbility | null =>
      code === SHIELD_KEY_CODE
        ? 'shield'
        : code === CHARGE_KEY_CODE
          ? 'charge'
          : null;

    const isUiInteraction = (event: KeyboardEvent): boolean =>
      blocksGameplayInput(event.target) ||
      blocksGameplayInput(document.activeElement);

    const handleKeyDown = (event: KeyboardEvent): void => {
      if (isUiInteraction(event)) {
        cancelActivation();
        return;
      }
      const ability = abilityForCode(event.code);
      if (ability !== null) {
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
        if (event.repeat || heldAbilityKeys.has(ability)) {
          return;
        }
        // Latched on every accepted press, not only on one that sends: the latch records that the
        // key is physically down, so a suppressed activation still refuses the repeat behind it.
        heldAbilityKeys.add(ability);
        activate(ability);
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
      activationGeneration = inputGeneration;
      goHeld = true;
      flush();
    };

    const handleKeyUp = (event: KeyboardEvent): void => {
      if (isUiInteraction(event)) {
        cancelActivation();
        return;
      }
      const ability = abilityForCode(event.code);
      if (ability !== null) {
        // A release sends nothing -- the server holds no ability state a client could clear -- so
        // the only work here is dropping the latch, and only for a press this owner actually took.
        if (!heldAbilityKeys.delete(ability)) {
          return;
        }
        event.preventDefault();
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
      if (activator.current === activate) activator.current = null;
      window.removeEventListener('blur', cancelActivation);
      window.removeEventListener('focusin', handleUiInteraction);
      window.removeEventListener('pointerdown', handleUiInteraction);
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

  return { ...view, observeAim, activateAbility };
}
