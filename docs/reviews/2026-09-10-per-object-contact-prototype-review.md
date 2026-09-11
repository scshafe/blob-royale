# Per-object contact prototype review — 2026-09-10

Status: **Step 4a source review complete; execution evidence is separate; not Step 5 acceptance.**

Scope: the supplemental, unwired Step 4a prototype after `dd3c7b0`. The
`rigorous-architect` reviewer owns this document and performs source review only. The executing
agent owns builds, test results, benchmark measurements, and the plan/ADR updates. This document
does not accept Step 5, adopt the live solver, or certify native performance.

### Assumptions

- The controlling contract is ADR-0008 (dynamic-arenas-and-combat),
  `docs/architecture/0008-dynamic-arenas-and-combat.md` § "Owner clarification during Step 5
  review, 2026-09-10", and Steps 4a, 5, 16, and 18 of
  `.claude/plans/2026-09-10-dynamic-arenas-and-combat.md`.
- The design applies "separation of policy from mechanism" and the single implementation
  constraint recorded in ADR-0004 (gameplay-architecture),
  `docs/architecture/0004-gameplay-architecture.md` § "Context and Problem Statement".
- An object's policy controls that object's effects on the other participant. It does not
  select the other participant's policy, disable physical collision admission, or grant
  protection against another object's effect.
- Ordinary shield behavior after the perfect opening remains undecided. The existing pure
  ordinary-shield implementation remains prototype evidence, not newly accepted gameplay.
  The short perfect opening requires incoming opponent motion; grazing effect eligibility
  alone does not qualify for a parry (ADR-0008 § "Perfect shield; later behavior deferred").

### Risks

- No remaining concrete source blocker was found in the reviewed Step 4a implementation.
  The reviewer ran no builds, tests, or benchmarks. Their execution and evidence belong to
  the executing agent; source closure alone does not establish those results.
- The broader observation path retains the existing visible numerical limits of certified
  normals and shared roots. It does not claim arbitrary binary64 input acceptance. In
  particular, previously rejected default-policy pairs still return before normal construction;
  a requested touch must be representable within the certified geometry contract.
- Revision-based delivery can repeat an effect during continuous overlap when another event
  changes a participant's trajectory. It is not once-per-encounter delivery. Cross-tick or
  separation/re-entry suppression would require a separately specified lifetime.

### Alternatives

- **Extend the existing pair driver and composition.** A certified touch observation carries
  an optional closing-impact certificate and source-oriented effect eligibility. Physical
  equations consume only the impact certificate. One driver retains pair dependencies,
  event identity, geometry, response ordering, and repeat suppression.
- **Implement moving-body touch effects with `MotionTrigger`.** Rejected for this scope. The
  current trigger binds one entity, tracks its revision, admits support/checkpoint priorities,
  and replaces that body. General pair support would duplicate dependencies, identity, and
  two-body responses already owned by the pair driver
  (`src/simulation/continuous_motion.hpp`, `MotionTrigger` and `solve_continuous_motion`).

### Recommended path

Accept **Step 4a architectural/source-review closure**, with build/test/measurement results
recorded separately by the executing agent. The solver, composition, test additions, and
benchmark adapter were reviewed after their workers declared the source frozen. No live
solver, component registration, authoring parser, wire schema, accepted fixture, root equation,
event comparator, or ordinary-shield decision is adopted by this closure.

#### Review findings and source closure

One concrete finding required correction: the first observation implementation constructed
contact geometry before rejecting an impact-only pair with inadmissible topology. That reordered
the old rejection path and could make tiny stationary geometry fail while normalizing a vector
that previously needed no normal. The corrected `make` branch returns before geometry when
neither any-touch admission nor impact geometry applies. The new tiny-stationary regression
asserts unchanged bodies and no events/effects
(`src/simulation/continuous_motion.cpp`, `ContinuousMotionSolver::State::pair_event`;
`tests/unit/simulation/continuous_motion_tests.cpp`,
"default stationary rejection retains its domain without constructing a tiny normal").

| Reviewed boundary | Source-reviewed result |
| --- | --- |
| Policy ownership and validation | Frozen policies bind entity identity; missing rows select closing-impact; duplicates, unknown entities, and invalid enum values fail before motion (`src/simulation/motion_contact_observation.hpp`, `src/simulation/continuous_motion.cpp`). |
| Touch versus impact | Exact circle topology permits tangent/stationary observation while optional impact retains the existing closing gate. Revised retained certificates use current radial direction without replacing certified geometry (`src/simulation/continuous_motion.cpp`). |
| Repeated observations | The same post-response revision suppression consumes no-op and velocity-changing observations. External changes can re-enable the pair; acceleration-only changes and time advancement cannot (`src/simulation/continuous_motion.cpp`, `finish_event`; `tests/unit/simulation/continuous_motion_tests.cpp`). |
| Source-oriented lethal effects | A victim's eligibility reads the other participant's source flag. Swapped identities and the complete two-policy source/victim combinations are represented in the regression assertions (`src/gameplay/shared/guarded_pair_contact.cpp`, `tests/unit/gameplay/shared/guarded_pair_contact_tests.cpp`). |
| Physical and guard composition | The existing base, quarter-delta, perfect-stop, and separation arithmetic remains behind the optional impact. Touch-only paths preserve motion and cannot emit stun (`src/gameplay/shared/guarded_pair_contact.cpp`). |
| Chronology and terrain | Support/contact/finish ordering, termination, final-tick observations, and exclusion of future reference-line travel retain the existing driver. The real terrain support-loss helper is reused (`tests/unit/simulation/continuous_motion_tests.cpp`, `tests/unit/gameplay/shared/guarded_pair_contact_tests.cpp`). |
| Historical benchmark workload | The callback type changes; the three prototype cases retain an empty policy span and their existing workload and outcome assertions. The documentation identifies that retained scope (`benchmarks/blob_simulation_benchmarks.cpp`, `benchmarks/README.md`). |

The source includes 14 new solver policy cases and six new guarded-composition cases. These
counts describe inspected assertions, not successful execution. Existing guarded-composition
expectations remain unchanged, including the explicitly historical ordinary-shield cases.
The default-policy benchmark workloads do not measure the cost of dense any-touch populations.

#### Shared prototype contract

The agreed interface is the new `src/simulation/motion_contact_observation.hpp`:

- `ContactEffectPolicy::kClosingImpact` or `ContactEffectPolicy::kAnyTouch`.
- `MotionContactEffectPolicy` identifies one entity and its frozen effective policy.
- `PairContactObservation::touch` carries certified contact geometry, including normal,
  center distance, and current relative normal speed. It does not by itself authorize an
  impulse. `impact` is optional and retains the existing impact admission contract.
- `first_effect_eligible` admits effects declared by the first source on the second body;
  `second_effect_eligible` admits effects declared by the second source on the first body.
  An admitted impact satisfies both policies. A touch without impact satisfies only
  `kAnyTouch`.
- The policy span is a final argument to `solve_continuous_motion`, after the existing limits.
  Missing entries mean `kClosingImpact`; duplicate entities, unknown entities, and invalid
  policy values fail visibly. The empty span preserves the original impact-only behavior.

The canonical detailed circle query supplies the geometry and exact classifications. Initial
inside/on-boundary contact is observable even with stationary, separating, or tangent motion.
An outside tangent contributes its single in-range root. An unchanged tangent/stationary
certificate cannot become an impact through rounded normal arithmetic. A velocity-revised
retained certificate keeps its original time, normal, and distance, while the existing exact
current-radial veto and closing-speed check decide whether an impact is now admitted. It must
not re-test the retained geometry using rounded membership or unrelated future roots
(`src/simulation/swept_geometry.hpp`; ADR-0008 § "Continuous-motion prototype boundary").

The same pair composition handles lethality, diagnostics, and optional physical response.
Victim eligibility reads the other participant's source flag. A no-impact observation may
terminate an eligible victim or emit a contact fact, but it cannot run base impulse, guard
projection, or parry/stun arithmetic. Both effects and physical response remain inside the
one pair chronology. Support loss precedes contacts; contacts precede checkpoint credit;
termination removes subsequent events. Center-based falls continue to use the existing
terrain support-loss query (`src/simulation/motion_event_order.hpp`,
`src/simulation/motion_triggers.cpp`).

Every consumed observation records both post-response motion revisions, including a response
that changes no motion. Its own response and elapsed time do not re-enable it. Another event
changing either trajectory can re-enable the pair. Existing event/effect limits bound the
resulting work; there is no minimum time advance, skipped event, or hidden fallback. The
state resets with the solver invocation, so this contract makes no cross-tick encounter
promise (ADR-0008 § "Per-object effect eligibility").

#### Proposed Step 16 authoring and committed projection

The following is a concrete future adoption design, not a claim that these types or schema
members exist today:

1. Add an optional body-bound `ContactEffectAdmission` component containing a validated
   **nondefault** `ContactEffectPolicy`. Canonical absence means `kClosingImpact`; the current
   stored nondefault is `kAnyTouch`. Every body has exactly one effective policy without
   requiring a synthetic component on every existing seed or body replacement. It remains
   separate from `PhysicsBody` and from `LethalOnContact`: the first owns mechanics; the
   latter selects whether a lethal effect exists. The existing typed-component and
   presence-only lethality contracts provide these ownership boundaries
   (`docs/architecture/0004-gameplay-architecture.md` § "Entities, components, and stores";
   `src/simulation/components/lethal_on_contact_component.hpp`).
2. One validated assignment/effective-policy owner resolves creation inputs once: explicit
   instance policy, otherwise an archetype default, otherwise `kClosingImpact`. Resolve an
   explicit closing-impact override before canonical omission so it can override an
   any-touch archetype. Assignment removes the component for the default and stores the
   nondefault otherwise; lookup maps absence to the default. Unknown authored values,
   invalid enum values, and redundantly stored defaults are errors, never an implicit
   fallback. Store no reference back to mutable defaults. `HazardArchetype` owns a configured
   default; body creation accepts an explicit instance policy, allowing two instances of one
   archetype to differ. Keep this a typed creation input, not an arbitrary component-patch command
   (`src/gameplay/shared/hazard_archetype.hpp`,
   `src/gameplay/shared/hazard_spawn_system.cpp`).
3. Extend each authored static-body row with its explicit instance policy and represent the
   row as one body declaration containing geometry and policy. Preserve row-order identity
   and one map-content owner; do not join a second geometry table by coordinates. The current
   row loader and map seating contract are the migration points
   (`src/application/map_loader.cpp`, `src/simulation/map_definition.hpp`).
4. Project the committed nondefault components into sparse `MotionContactEffectPolicy` rows
   in canonical entity order at the solver boundary. Missing rows have the same explicit
   closing-impact meaning as in the pure prototype. Contact responses read the same frozen
   policy facts for that quantum. No live callback infers policy from class names, radius,
   or a global mode.
5. Publish nondefault components with the v3 kind `contact_effect_admission`, carrying the
   validated `policy` field. Its initial schema admits only `any_touch`; documented absence
   means `closing_impact`. Authoring accepts both choices, while committed/wire values have
   one representation of each effective state. Step 16 owns the complete authoring, validation,
   component registration, body-bound lifetime, publication, schema, and deliberate existing
   object migration. There is no protocol or registry change in Step 4a. This follows the
   plan's Step 16 contact-admission amendment and wire rule.

Mandatory resolved components were considered and rejected because they would require changes
to every body producer merely to materialize the established default. A distinct empty marker
for each policy was also rejected: one named policy capability can accommodate later reviewed
values without proliferating component kinds. The sparse nondefault component keeps the
effective lookup total while making malformed data fail explicitly.

The closed two-policy vocabulary covers the approved distinction. Independent sensor filters,
center-entry on blocking body hazards, runtime object editing, and per-effect policy within one
object require explicit follow-up design; none is implied by this projection.

### What to verify before committing

- The executing agent records the required serial verification and default-policy benchmark
  outcome comparison. This source review does not substitute for or predeclare those results.
- If verification changes admission, response, or repeat semantics, return that change for
  focused source review before citing this document as closure of the revised behavior.
- Preserve the completed Step 4 review and benchmark baseline as historical evidence. Label
  any new measurements with their source and advisory platform. Native performance and
  capacity remain deferred, not certified (ADR-0008 § "Capacity and performance deferred").
- Step 5 still needs its explicit human decision. Step 16 owns the proposed committed-policy
  and schema projection above; it must also document that a contact fact can describe a
  non-impulsive touch. Resolve post-opening shield behavior before Step 18.

Confidence: medium — reviewed source closes the identified blocker; execution evidence and future live-policy adoption are separate.

### Execution evidence recorded by the executing agent

On 2026-09-10, **Mac/arm64 hosting Docker Linux/amd64; advisory, not native**:

- `./scripts/verify-focused 'unit.simulation|unit.gameplay'`: **761/761**.
- The same command with `linux-clang-asan-ubsan`: **761/761**.
- `./scripts/run-benchmarks-linux`: clean release build, all **8 cases** and delivery passed.
- `./scripts/verify-focused 'fixtures'`: **42/42**.
- The same command with `linux-clang-asan-ubsan`: **42/42**.

The final source contains 14 new solver cases and six new composition cases. No accepted
fixture/oracle/replay expectation changed. Pinned formatting and `git diff --check` passed.
No verification-driven semantic correction followed the source-review closure.

The sibling `2026-09-10-per-object-contact-prototype-baseline.json` records the three unchanged
default closing-impact workloads, raw samples, platform, and exact comparisons against the
historical Step 4 baseline. Workload definitions, complete correctness records, and deterministic
work counters are identical. Hashes remain `fnv1a64:53a16ad6bc6a44a0` (charge),
`fnv1a64:8f206d66b10f4e84` (32 holes), and `fnv1a64:8d567eb367dea076` (dense shields).
New advisory median solve times are 34.124438 µs, 702.192563 µs, and 280.408563 µs respectively;
they do not establish a native performance regression, capacity, or tuning decision. Any-touch
behavior is covered by the added tests, not by a claimed new performance workload. The prior
review and baseline remain historical evidence. Step 5 still requires the owner's decision.
