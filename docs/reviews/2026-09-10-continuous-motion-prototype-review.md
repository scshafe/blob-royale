# Continuous-motion prototype review — 2026-09-10

Status: **Step 4 source review and advisory verification complete; not Step 5 acceptance**.

Scope: plan Step 4's permanent, pure, unwired motion solver, trigger helpers, guarded-pair
composition, arithmetic promotion, and deterministic benchmark workloads. A `rigorous-architect`
specialist performed the independent read-only review; the executing agent owns test execution
and this evidence record. All local execution is **advisory Mac/arm64 hosting Docker Linux/amd64**,
not native Linux certification. No live-kernel cutover, protocol change, push, or deployment is
part of this step.

## Proposed numerical and composition boundary

`swept_geometry` and `MotionEventKey` remain the only root/time-order owners. Binary64 times are
compared exactly: time, support loss, body contact, x wall, y wall, checkpoint, then canonical
identities. There is no time epsilon, quantization, or minimum time advance. The solver chooses
the minimum currently eligible event; a response can causally enable an earlier-priority event
at the same time, so history is time-monotone rather than globally sorted by the full key.

Each body's velocity epoch has its own fixed anchor. Pair roots use the later of their anchors;
unrelated events do not shorten either trajectory and change its root arithmetic. Geometry
certificates already at the selected time survive velocity-only changes, but termination removes
the body. Pair repeat suppression records both post-response motion revisions. Acceleration-only
changes do not create another current-motion revision. Paths close on the body's own velocity
change, termination, or quantum end, rather than reporting one start-to-end chord.
Velocity-revised retained contacts receive an exact radial-direction veto from the shared circle
owner. This is not a membership recheck: the certified time/normal/distance remain intact despite
newly rounded inside/outside or supporting-line classifications. The diagnostic event trace records
selected certificates, including declined ones; emitted typed consequences are separate.

At exactly `t=1`, a newly closing touching pair still participates in causal ordering before a
tied finish. A reference line formed from relative velocity times one canonical fixed delta is
classified by the same circle query; only initial inside/on-boundary geometry can be admitted at
the existing end time. Reference roots outside that initial contact never authorize next-tick
travel. This distinguishes zero remaining duration from truly stationary relative motion.

Callbacks read the committed world and frozen typed facts. They can return velocity,
acceleration, and per-body disposition changes plus typed consequences, but cannot teleport,
alter collision geometry, or mutate the world. Triggers use bounded stable feature identities and
cursors; a response must advance its cursor, change geometric velocity, or terminate. Support
loss bypasses guards. Ordered gates use canonical closed occupancy and finish termination.

Closing-only contact admission retains the live velocity-tolerance predicate. Initial closing
overlap is a zero-time contact without depenetration; stationary, separating, and tangent touches
do not cause contact/guard/lethal effects. Position tolerance belongs to spatial membership, never
event comparison. Body membership uses the circle polynomial at the sum of effective radii,
without the old initial proximity band. Gate endpoint membership retains the existing written
square-root distance against authored radius plus `kPositionTolerance`, applied exactly once.
Static stored velocity is not geometric motion.
Exact radial approach is also required for initial contact unless centers coincide exactly.
Noncoincident centers retain their geometric normal even when their distance is below the legacy
position tolerance; only exactly coincident centers use the velocity/axis fallback. This is an
unwired prototype distinction, not an alteration to accepted legacy detectors.

Guard composition calls the promoted accepted baseline/general/static equations. It quarters
guarded received velocity deltas, derives both perfect-stop decisions from pre-response incoming
world-frame motion, zeros newly stunned velocity and acceleration, then performs one inverse-mass
normal separation projection. Static and newly stopped bodies have zero correction weight for
that projection, not permanent immobility. A later external impact may move a stopped body.
Residual closing speed outside the existing velocity tolerance is a named failure, not an
iterative nudge. Unguarded lethal contacts preserve both bodies and terminate the victim(s).
Unguarded physical contacts return the base equation unchanged, including a restitution-zero
rounding residue; the defense projection is only applied when a guard modified the response.

The shield's quartered response can add world-frame kinetic energy. The unit-mass `(10,9)`
example becomes `(9.75,10)`, increasing the squared-speed sum from `181` to `195.0625`. Only the
final separation projection is dissipative relative to its own input. Global energy conservation
is not claimed; choosing an energy cap would change the gameplay policy.

## Provisional resource envelope

These are fail-visible engineering ceilings, not measured native capacity or operating advice.
Callers/tests may lower them, never raise them silently.

| Resource per quantum | Ceiling |
| --- | ---: |
| Bodies | 256 |
| Candidate pairs | 32,640 |
| Broad-phase pair examinations | 1,000,000 |
| Root-query charges | 250,000 |
| Selected events | 2,048 |
| Trigger queries | 250,000 |
| Path segments | 4,352 |
| Returned effects | 8,192 |
| Trigger declarations | 512 |
| Trigger cursor value | 4,096 |

Terrain retains Step 3's 8 corridors, 32 total segments, 40 points, 32 holes, 8,192 retained
boundary elements, and 60,000 temporary logical arrangement slots. These bounds do not measure
allocator capacity or certify every maximum-shaped input's performance. Exhaustion throws;
there is no discrete fallback, truncated motion, dropped consequence, or partial result.

Scalar broad-phase endpoints do not reject an early-stopped trajectory solely because its
unhandled endpoint exceeds body-coordinate storage. Terrain/gate helpers retain a narrower
whole-sweep domain: `start + displacement` must fit `Vector2` component limits, even when an
earlier trigger could terminate travel. That visible numerical-admissibility restriction remains
for owner review before live adoption; this step does not clip the query or alter Step 3's domain.

## Review findings and disposition

The `rigorous-architect` recommendation is to accept **Step 4 architectural/source-review closure**,
conditional on the executing agent's final verification, not live adoption. Its final read-only
review found no remaining concrete blocker in the reviewed scope. The specialist ran no tests
or benchmarks and assigned medium confidence to its source-based conclusion; execution evidence
below belongs to the executing agent.

| Finding | Correction and retained proof |
| --- | --- |
| Exact high-speed tangency acquired a false closing normal-speed residual | Shared circle topology; fixed root-bit/adjacent hit-miss tests; mid-sweep and endpoint tangent regression |
| A velocity-revised retained contact bypassed fresh topology admission | Exact radial metadata with certificate-producing revisions; no membership re-gate; no-consequence and charged-query-budget regression |
| Tiny distinct centers were collapsed by the proximity fallback | Exact coincidence only; tiny separating, oblique geometric-normal, and coincident-fallback cases |
| Declining a tied wall lost the future opposite wall | Invalidate the consumed axis even without reflection; exact axial reversal/multiple-bounce regression |
| Zero remaining duration dropped causal tick-end contacts | Reference-line classification only; contact chain before tied finish, terminal tangent, and no-next-tick-travel tests |
| Intermediate vectors rejected representable relative motion or early termination | Scalar relative/AABB intermediates; opposing maximum velocities and near-coordinate-limit termination cases |
| Wall-overlap and gate-rim contracts were inconsistent | Outward immediate reflection/inward departure without relocation; gate spatial expansion once; bent-path, rim, support/body/gate priority cases |
| Defense correction changed unguarded restitution-zero bits | Project only guarded responses; explicit general-equation residue preservation and real-driver perfect-shield tests |
| Optimized GCC diagnosed the setup factory's moved-optional chain | Direct construction of the identical map/mode value; original-chain equivalence over initial state and 50 committed ticks; no warning suppression |
| The royale benchmark read a deployment now configured for king of the hill | Freeze the former configuration with provenance; explicit reference input/JSON labels and ADR 0006 amendment; original measured workload and current live configuration unchanged |

The reviewed design retains fixed epochs, post-response repeat suppression, bounded trigger
progress, actual paths, immutable callback inputs, and one permanent guarded composition. The
three benchmarks call those same owners and assert fixed physical outcomes. No replacement
engine, fast-body bypass, new registry arm, or extra live-kernel declaration is introduced.

The specialist's final benchmark-input addendum also found no remaining blocking source issue.
After its five-line provenance header, `benchmarks/fixtures/royale-roster.cfg` matches the
configuration in `a9e0104ca25724ac4660a4fc9031620b8a852d40` byte for byte. That provenance applies
to the configuration only; the map remains the current repository's `arena-960x640`, whose only
change since that source is Step 3's explicit solid-terrain declaration. The historical
`royale_deployed_roster` identity, seat widening, measured loops, timing/hash algorithms, and
budget are retained. This is not a measurement of today's hill deployment. Provenance metadata
describes the canonical runner's checked-in fixture, not authentication of arbitrary files passed
directly to the executable.

## Verification and fixture impact

Before legacy readers delegated, all seven actual-old/new/frozen-reference arithmetic promotion
cases passed on both advisory lanes. The exact filter was
`unit.simulation.(contact-taking|contact impulse promotion|general impulse promotion|radius wrappers|static velocity extraction)`.
The original radius solvers and static response were still intact during those runs. An earlier
two-case filter is not counted as the promotion gate. The permanent independent frozen reference
remains after delegation.

The detailed circle promotion independently passed **4/4** cases on each advisory lane before
delegation/consumer adoption, including 25 fixed geometry/root-bit rows. The old compiled solver's
exact nonaxis-tangent regression was also run directly through CTest on both lanes before a
rebuild: both failed specifically because a contact effect and event were incorrectly emitted.
That deliberate red test establishes the defect; it is not completion evidence.

Before radial-metadata consumption, the expanded identical command filter passed **5/5** on each
advisory lane, retaining all 25 root-bit rows and adding 22 radial/coincidence rows. Root
expressions/order remained unchanged. The old compiled retained-tangent regression also failed
on both lanes with two effects instead of one; this separately establishes the cached-path defect.

Full post-delegation simulation/gameplay verification passed **741/741 on each advisory lane**:
`./scripts/verify-focused 'unit.simulation|unit.gameplay'` and
`./scripts/verify-focused 'unit.simulation|unit.gameplay' linux-clang-asan-ubsan`.
This includes 62 new named cases: 7 impulse promotion, 5 circle metadata, 25 continuous-motion,
24 guard-composition, and 1 setup-factory equivalence. The accepted-fixture supplements
`./scripts/verify-focused 'fixtures'` and
`./scripts/verify-focused 'fixtures' linux-clang-asan-ubsan` each passed **42/42, advisory**.
Accepted fixture/oracle/replay expectations remain untouched. The live kernel and original
contact detectors remain the execution path until the later approved cutover.

The required release runner initially failed an optimized setup-factory warning, corrected as
recorded above. After that build passed, execution exposed a stale benchmark-input assumption:
the current deployment selects king of the hill, while the old benchmark requires royale.
The plan was amended before replacing that input with a provenance-backed historical royale
reference. Neither failed attempt produced completion evidence or justified changing an expected
physical outcome. After that correction, the final complete serial chain passed both focused
lanes, the canonical benchmark command, and both accepted-fixture lanes. All counts above refer
to that final chain. No accepted expectation was changed.

## Advisory benchmark baselines

The canonical `./scripts/run-benchmarks-linux` passed all eight cases plus its delivery check
after a clean pinned release build. The three new workloads use the same solver and composition
core. Each uses one warm-up and nine timed samples of sixteen solves; independent repeats and
each retained timed output passed fixed outcome and complete-result-hash checks outside timing.

These are **advisory Mac/arm64-hosted Docker Linux/amd64 measurements**, not native evidence.
The container reports VirtualApple at 2.50 GHz, eight logical CPUs, GCC 13.3.0, Release,
glibc 2.39, and pinned toolchain `ubuntu-24.04-amd64-20260804`; the governor is unavailable.
Raw samples, exact workload definitions, all work counters, hashes, and platform metadata are in
[`2026-09-10-continuous-motion-prototype-baseline.json`](2026-09-10-continuous-motion-prototype-baseline.json).

| Workload | Advisory median µs/solve | Advisory p25–p75 µs/solve | Complete result FNV-1a64 |
| --- | ---: | ---: | --- |
| Eight charge-speed/static contact rows | 26.406750 | 26.139750–26.945938 | `53a16ad6bc6a44a0` |
| Thirty-two masked bodies crossing thirty-two holes | 577.843438 | 577.055500–578.895688 | `8f206d66b10f4e84` |
| Eight touching four-body shield chains | 238.037438 | 237.964125–238.545313 | `8d567eb367dea076` |

The deterministic work recorded in the same advisory run is per solve. Root counts charge
canonical public queries, including bounded pre-fast-return charges, not internal polynomial
operations. These observations do not establish native capacity or select production ceilings.

| Workload | Pair examinations | Root charges | Events | Trigger queries | Maximum candidate pairs | Paths | Effects |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Charge speed | 1,080 | 144 | 8 | 16 | 8 | 16 | 8 |
| Dense holes | 2,118 | 1,680 | 32 | 32 | 0 | 32 | 32 |
| Dense shields | 17,834 | 1,520 | 40 | 112 | 48 | 32 | 80 |

The fixed outcomes are eight reflections without falls; thirty-two support-loss terminations;
and forty contacts plus forty stun facts, with each shield chain ending at x velocities
`(-250,-250,0,0)` because later external impacts can move a previously stopped body. The stress
speed is 40,000 world units per second, not approved charge tuning. Terrain compilation,
acceleration/drag intake, ability/status lifecycle, publication, and transport are excluded.
These cases exercise the authored hole limit but not worst-case body/arrangement capacity.

## Step 5 owner gate

Step 5 remains unticked. Native Linux performance evidence and owner selection of practical
ceilings are required. The owner must also settle adoption of closing-only contact admission,
the external-impulse shield policy, and the explicit numerical-failure boundary. Phase C stays
blocked until that human decision. Passing local tests does not certify release performance or
authorize the cutover.
