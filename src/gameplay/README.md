<!-- canonical: gameplay_domain -- the games, and where a new one goes -->

# Gameplay domain

`blob_gameplay` is where the **rules** live. `blob_simulation` owns values and mechanism — components,
stores, the fixed tick kernel, the match machine, the closed command and event vocabularies — and
this library owns the declarations that turn that mechanism into a playable game
(`docs/architecture/0002-simulation-architecture.md` § "Decision";
`docs/architecture/0004-gameplay-architecture.md` § "Libraries, and where a new thing goes").

It depends on **`blob_simulation` and nothing else**. No network, no JSON, no Boost, no logging, no
thread, no clock. A mode that wanted any of those has misplaced a rule that belongs above the
simulation boundary.

## What is here

```
src/gameplay/
  game_mode_registry.hpp/.cpp   the closed map from one mode name to its factory
  gameplay_validation_error.hpp the one exception vocabulary of this library
  shared/                       systems more than one mode declares
    thrust_steering_system.*    a validated thrust direction becomes stored acceleration
  sandbox/                      free play: thrust, bump, and nothing ever ends
    sandbox_mode.*              the seven declarations
    free_play_objective.hpp     always startable, never decided, zero durations
    next_free_spawn_point_policy.hpp  the next free point, in every phase
```

**`shared/` is where a system more than one mode declares lives.** ADR 0004 files a mechanic under
`src/gameplay/<mode>/`, which is right for a mechanic one mode owns. `thrust_steering` is declared by
`sandbox` and, from plan Step 21, by `royale`, and filing it under either would make the other reach
into its neighbour. The rule is: one mode declares it, it lives in that mode's directory; two modes
declare it, it moves to `shared/` and takes its scale as a constructor argument.

## Adding a game

```
new  src/gameplay/<mode>/<mode>_mode.{hpp,cpp}  the mode class, its systems, rules, policies
new  src/gameplay/<mode>/*_system.{hpp,cpp}     any mechanic only that mode declares
edit src/gameplay/game_mode_registry.hpp        one include and one row in kGameModeRegistrations
edit match configuration                        `[match] mode=`
do not touch                                    blob_simulation, blob_runtime, blob_server,
                                                blob_protocol, or any other mode
```

`@extension-point game_mode` — `game_mode_registry.hpp`. The table is `constexpr`, so two rows
claiming one name fail to compile rather than resolving to whichever was written first. A factory
takes no argument today because the only balance number a registered mode owns has a declared
default; plan Step 25 hands a factory its validated `[<mode>]` section, which changes this row shape
once and changes no mode.

Two implementations of the seam: `sandbox` here, and `royale` in plan Step 21.

## `sandbox`

Free play. It accepts `spawn`, `despawn`, and `thrust`; seats every joiner at the next free spawn
point in every phase; uses the engine's two built-in contact rows unchanged; declares one
`kPreKernel` system, `thrust_steering`; and never leaves `running` because its objective can always
start and is never decided. It contributes no component kind, no contact rule, no world event, no
mode-state block, and no `kPostKernel` or `kLifecycle` system.

`SandboxMode` is **77 lines** — a 46-line class declaration plus 31 lines of definitions — of which
**53 are code** once comments and blank lines are removed. That is the measurement ADR 0004's claim
that "a mode is a declaration, not machinery" is answerable to, and every one of those lines is a
declaration: the longest function body in the mode is `validate_map`'s five-line rejection. Its two
sub-declarations are 44 and 53 lines including their comment blocks, and 13 and 19 lines of code.

`validate_map` rejects a map with no `spawn` marker at startup, naming the map: free play with
nowhere to seat a joiner would silently defer every spawn command forever.

## Steering

`steered_acceleration` is the one place a thrust direction becomes an acceleration, and **the
magnitude clamp happens exactly once, there**. `ThrustCommand` carries the submitted direction
verbatim and `InputBatch::create` only range-checks each component against `[-1, 1]`, because
clamping at construction and again in the system would scale twice and is not bit-identical to
scaling once. The written operation order is the contract of
`docs/architecture/0005-royale-mode.md` § "Steering":

```
m = sqrt(x * x + y * y)
s = 1        when m <= 1
s = 1 / m    when m > 1
acceleration = ((x * s) * thrust_max, (y * s) * thrust_max)
```

`sqrt(x * x + y * y)` is written out rather than delegated to `std::hypot`, which computes a
different binary64 value for the same inputs.

## Determinism obligations

Everything `docs/architecture/0004-gameplay-architecture.md` § "Determinism obligations for
framework code" binds mode code to holds here: a system's `apply` is `const` and it holds immutable
configuration and nothing else, every value it mutates is world-owned, every iteration is over an
ascending component store or the canonical two-store join (`component_join.hpp`), and no mode reads
a clock, an unordered container, a pointer order, or a global.

Every declaration a mode returns is **independently owned**: the engine reads the seven declarations
once at construction and then destroys the mode, so a system, policy, or objective holding a pointer
back into its mode would dangle on the first tick.

## Verification

Focused tests are registered under the `blob_gameplay_unit_tests` CTest target with the
`unit.gameplay.` prefix, mirroring this directory under `tests/unit/gameplay/`. Run
`./scripts/verify-focused 'unit.gameplay'`; the canonical gate remains `./scripts/verify-linux pr`.
