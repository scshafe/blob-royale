# Plan: Playtest controls and crossing objects

**Goal:** Keep the arena visible, make movement and charge interactions match the owner's playtest feedback, and expose shared controls for charge strength and random crossing-object density.
**Out of scope:** Automatic propulsion without input; changes to perfect-shield physics or the accepted bot arrival-brake envelope.

## Context

The prior release is complete at deployed `8b2e9ac`; main includes its receipt at `f878ec0`.
The owner now requests held left-click Go, held Space brakes, two instantaneous quarter-turns,
charge-hit moving stun and recharge, a charge slider, random dangerous/survivable crossings with
broader speeds and independent average-rate sliders, and scrolling confined to the information panel.
Existing stun already preserves velocity. Existing charge is an instantaneous additive burst with
only cooldown; attributing hits requires an explicit active window. The current room-tuning exchange
already owns correlated Apply/Reset/revision behavior and must remain its sole implementation.
Released protocol 3.0 requires a 3.1 additive revision; retain v3 routes and existing wire names.
Root owns generation, builds, tests, formatting, commits, and deployment; one C++ build at a time
per host. Workers own named files and must preserve concurrent edits.

## Implementation contract

- Bound the application to the viewport. The arena fits both available dimensions; the right
  information panel scrolls independently. The room directory has its own bounded content region.
- Left-click held over the arena propels toward the cursor. Space brakes against authoritative
  current velocity each tick, with a stopping impulse capped against reversal; braking wins over Go.
  The owner confirmed quick gradual braking/cancel charge and right-drag Manual camera pan.
- Left/right buttons (Q/E shortcuts) rotate current velocity exactly 90 degrees by sign/swap,
  preserve speed, and work during charge. Rotation follows charge admission in the same tick;
  conflicting turns use the existing last-submission rule. Stun and stale input generations refuse
  propulsion, braking, abilities, and rotation. Fresh presses are required after invalidation.
- Charge keeps its additive boost and resultant-speed safety envelope. Its default hit window is
  0.5 seconds, with a separately authored 0.6-second victim stun. The first qualifying closing
  player impact consumes the attempt. An unshielded hit refunds cooldown and stuns the victim while
  retaining collision momentum; a shielded hit consumes the window without a refund. Frozen
  contact candidates are resolved once per activation before the existing status system. A stun
  cancels an active charge window while retaining cooldown. Active and cooldown expiry are independent.
- Extend the one atomic room-tuning value with charge boost fraction and aggregate dangerous and
  survivable spawn rates. Preserve existing wire names; label the panel Room tuning. The charge
  control shows its derived additive speed in world units/second. Apply remains one correlated
  update, with current values, authored Reset, revisions, and dirty-draft review preserved.
- Random births and sampled speeds use the existing named hazards random stream. The class rates
  are expected births/second while capacity is available; zero prevents new births. One aggregate
  Bernoulli trial per running tick selects a class; configured archetypes in that class retain their
  authored relative frequencies. The existing one-entity reservation remains unchanged. Random
  bursts require an explicit bounded active crossing population and matching startup admission;
  no expected-value calculation may stand in for a worst-case bound. Existing objects keep their
  sampled speeds/lifetimes after tuning changes. Defaults increase both deployed populations and
  broaden their speeds; exact proposed values are recorded with the implementation.
- Input transmission shares a bounded budget. Remove the artificial charge-only delay after a
  confirmed hit; rapid turns/abilities must not exhaust the server bucket or delay a release behind
  stale steering. No acceptance retries, skips, or weakened deadlines.

## Steps

- [x] **Step 1: Keep the arena inside the viewport**
  - Verify: full web verification and browser viewport/sidebar-scroll scenarios at wide, short, and narrow dimensions.
  - Notes: Complete. Full web checks passed (1,033 tests); Chromium passed all 27 scenarios with zero retries/skips, including wide, short, and narrow viewport/sidebar checks.
- [ ] **Step 2: Resolve charge hits through shared status**
  - Verify: GCC debug and ASan/UBSan charge/status/contact tests plus cross-mode replay fixtures.
  - Notes: Preserve frozen contact decisions, motion momentum, shields, and explicit effect-budget failure.
- [ ] **Step 3: Add authoritative braking and quarter-turns**
  - Verify: both C++ lanes for command admission, generation invalidation, exact rotations, brake stopping, and charge ordering.
- [ ] **Step 4: Randomize bounded crossing births and speeds**
  - Verify: both C++ lanes for deterministic replay/draw order, class rates, speed bounds, zero rate, capacity, lifetime, and startup guards.
- [ ] **Step 5: Extend the single room-tuning exchange**
  - Verify: both C++ lanes for authoring, validation, atomic revision/results, live charge/rate application and reset; full protocol schema/example checks.
- [ ] **Step 6: Publish protocol and browser controls**
  - Verify: generate/check protocol artifacts, full web tests, browser Go/brake/turn/charge/slider flows, and fixed fuzz corpus.
  - Notes: Additive protocol 3.1; migrate exact fixtures for deliberately changed behavior with explicit reasons.
- [ ] **Step 7: Review and verify the complete implementation**
  - Verify: full GCC debug and ASan/UBSan CTest, full web and Chromium, fixed and bounded fuzz, pinned formatting, native benchmarks, and `git diff --check`.
  - Specialist: proofreader
- [ ] **Step 8: Publish and verify the native release**
  - Verify: push final source, exact-source GitHub quality, canonical `./scripts/deploy-tailnet` native release, independent installed-image/files/all-room health, and browser verification.
  - Notes: The owner's ongoing push/deploy authorization covers this requested playtest iteration. Preserve unrelated Tailscale handlers and publish the exact deployed identity.

## Done criteria

All requested interactions and sliders work in the live room, the document remains fixed while
the information panel scrolls, all applicable gates pass, and the exact deployed revision and
verification evidence are recorded. Human balance assessment remains distinct from automated tests.

## Verification checkpoint

Implementation and source review are complete. Web type, lint, generation/example, format, unit,
production build, and Chromium checks passed on the local pinned Linux toolchain. The full GCC
development run passed 1,952 of 1,957 cases; its five obsolete fixtures were repaired, and all seven
selected cases (including integration setup/cleanup) then passed.
The native clean release profile remains the owner of complete GCC debug/release, ASan/UBSan, TSan,
fixed and bounded fuzz, and publication evidence. Steps 2–8 remain open until those checks and
deployment finish. Local Docker on this Darwin/arm64 workstation is advisory.

Source review corrected Sandbox advertising a crossing rate without an installed spawner and
off-canvas aim loss incorrectly releasing held brakes. Both have regression coverage. Final
acceptance audit found no remaining production mismatch. Historical hazard fixture geometry and
charge-hit expectations were migrated for the deliberately changed behavior; deadlines, zero-retry
policy, and the accepted conditional bot arrival-brake contract remain intact.
