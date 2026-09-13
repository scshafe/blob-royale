# Arrival braking: accepted velocity reversal

On 2026-09-12 the owner chose: **keep current controls; define and test acceptable
overshoot**. This supersedes Step 22c's universal no-overshoot and all-drag stability
claims. The controller algorithm, published observation, command vocabulary, and
held-thrust application remain unchanged. This is a corrected behavioral contract,
not a new authoritative stopping mechanism.

## Accepted envelope

Arrival braking permits velocity reversal. For a stationary selected hill, an
established regular observation cadence, immediate next-tick command application,
constant movement tuning, and an inactive normal-propulsion limiter, each velocity
component stays within its magnitude at the start of that brake hold, allowing the
simulation's numerical tolerance. The bot must remain in the arrived branch, with
no contacts, ability impulses, stun, walls, or body replacement intervening.

This bounds **reversed speed**, not distance past a stopping point, retention inside
the hill, or finite-time exact rest. In particular, a reversal almost as fast as the
incoming velocity is allowed when reaction and observation intervals do not align.

Let observation spacing be S ticks and reaction delay R ticks. The brake estimates
H=max(R,S), but its command is held for N=ceil(R/S)S ticks when R>0, or N=S when R=0.
Regular cadence gives N<2H. Past observation spacing does not constrain future
runtime scheduling: missed observations, extra delivery latency, and irregular
publication are outside this envelope.

The first zero-delay decision has no established spacing and uses H=1. It is
explicitly outside the bound: with S=20, vx=0.25, acceleration=400, drag=0, and brake
fraction=1, the held command produces vx=-4.75 after twenty ticks. This retained
limitation is covered by a production-loop regression, not described as stability.

Moving hills, live tuning changes, and external impulses are also outside the
bound. Drag acts on absolute velocity; a relative-velocity proof for a moving hill
would require additional terms. Live acceleration rescales retained intent each
tick. No live-network stopping guarantee is inferred from controlled cadence tests.

## Why the conditional bound holds

Production integration adds acceleration, then applies q=max(0,1-D*dt), with
dt=1/400 second. For one component with initial velocity v0, the normalized brake
has effective coefficient 0<=beta<=f/H, where 0<=f<=1 is the authored brake fraction.
Component clamping and magnitude normalization only reduce this coefficient.
While the speed limiter is inactive, exact arithmetic gives:

```
v[n] = v0 * (q^n - beta * sum(k=1..n, q^k))
```

For q in [0,1], convexity gives sum(q^k)<=n(1+q^n)/2. With
c=beta*n/2<=1, the parenthesized term is at least (1-c)q^n-c>=-1 and at
most 1. Thus abs(v[n])<=abs(v0) through N. This argument also covers zero drag
and drag strong enough to clamp q to zero.

A sufficient limiter condition is initial speed norm strictly below V/2, where V
is the normal ceiling. The component bound keeps speed at most its initial norm;
the requested velocity increment per tick is at most that norm/H. Every pre-drag
endpoint therefore remains below 2*initial speed<V. Tests use a generous margin.

Under repeated qualifying holds with positive acceleration and brake fraction,
the ideal model contracts toward zero. Production evidence instead requires a
specific settling tolerance and finite observation horizon. It does not promise
exact zero, universal binary64 convergence, or convergence for a zero brake and
zero drag.

## Verification contract

Use actual TacticalController commands, retained shared thrust steering, and
production physics. Feed resulting body state into subsequent observations; check
command replacement and application ticks. Do not use a second implementation of
the brake equation as the oracle.

- Aligned R40/S20, vx=10, acceleration=400, V=600, fraction=1: drag 0 stops;
  drag 2 reverses to approximately -0.8553727688952049 after forty ticks.
- Nondivisible R21/S20 with drag 0: the actual forty-tick hold ends at approximately
  -9.04761904761905, within the accepted incoming magnitude.
- Established zero-delay cadence and the separate first-decision limitation.
- Diagonal saturated braking through shared normalization, with both component
  bounds and an inactive propulsion limiter.
- Repeated qualifying holds at fractions 1 and 0.5 and drag 0, 2, and 40, with
  measured settling; zero fraction/acceleration retain their coast/drag semantics.

Root runs pinned formatting, both full C++ unit/integration/fixture lanes, fixed
corpus, full web and browser gates after final source edits. These remain advisory
on this Mac/arm64 host. Native Step 24 release and benchmark gates remain required.

Source owners: `src/controllers/tactical_controller.cpp`,
`src/gameplay/shared/thrust_steering_system.cpp`,
`src/gameplay/shared/locomotion.cpp`, and `src/simulation/physics.cpp`.
Independent mathematical/source preflight: release-review proofreader, 2026-09-12.
Verification results belong in the release review, not in this preflight contract.
