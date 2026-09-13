# Playtest controls — native release and deployment receipt

The playtest iteration is complete. Canonical `./scripts/deploy-tailnet` installed
`4eadbc8023437bee6a030950e3341d2792ebc3cd` on `cole-ubuntu-pc` on 2026-09-13
(UTC and America/Los_Angeles). The site is available at
<https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444/>. The later documentation
commit records this deployment and does not identify a different running release.

The [machine-readable receipt](2026-09-13-playtest-controls-release-baseline.json)
contains the source, server, web, publication, image, evidence and observation identities.
The release ID is
`release-d60d5e3527512187949830d2c8dbf665d172e86a01d0a60a79a4ad98537faae5`;
the running image content ID is
`sha256:ee95dd3a0196caed2223202cccea105747de11ab9188d2eff2a3e0f1ed391573`.
The release/evidence identity is distinct from the image content identity.

## Shipped behavior

The document stays inside the viewport while the information panel scrolls. Held left-click
propels toward the cursor; Space slows current velocity without a reversing brake impulse and
cancels the active charge attempt. Right-drag pans in Manual view. Q/E and the two rotation
buttons turn velocity exactly 90 degrees, including during charge.

A qualifying unshielded charge hit refunds charge immediately and applies a moving stun.
The default active hit window is 0.5 seconds and victim stun is 0.6 seconds. Shielded hits
consume the attempt without refund; the existing perfect-shield response is preserved.
The charge slider tunes an additive boost: the deployed 0.75 fraction at normal speed
600 wu/s adds 450 wu/s. The resultant-speed safety envelope remains 20,000 wu/s.

One Room tuning exchange owns acceleration, normal top speed, charge boost and both crossing
rates, with atomic Apply/Reset and revision handling. Protocol 3.1 retains the v3 routes.
Random crossings share a cap of 64 active objects. Deployed comets sample 175–525 wu/s at
approximately 0.75 births/s; boulders sample 60–180 wu/s at approximately 0.35 births/s.
Rates describe expected births while capacity is available; zero prevents future births.
Existing objects retain their sampled speeds and lifetimes after tuning changes.

## Completed verification

The native host and Docker daemon are Linux/x86_64, using pinned toolchain
`ubuntu-24.04-amd64-20260804`. The source stamp certifies the exact clean commit above.
Root ran one C++ build at a time. Local Docker results on Darwin/arm64 remain advisory.

| Gate | Result |
|---|---|
| Clean GCC debug/release, Clang ASan/UBSan and Clang TSan builds | All four passed |
| Full CTest suites | 1,957/1,957 in each lane; 7,828 passed executions |
| Native process smoke | Passed |
| Full web verification | 1,033/1,033; zero failures, skips or todos |
| Generation, schemas/examples, formatting, types, lint and production build | Passed; 85 schemas and 35 examples across v1–v3 |
| Native Chromium scenarios | 27/27; zero retries or skips |
| Fixed fuzz corpus | 111 executions across seven harnesses |
| Bounded fuzzing | Seven 30-second campaigns; seed 424242, five-second per-input timeout |
| Source/artifact checks and release publication | Passed; authoritative |
| Shipped runtime/production-web dependency evidence | Passed; zero high/critical findings |
| Installation, readiness, Serve and site root | Passed |
| Independent installed-state and browser checks | Passed |
| `git diff --check` | Passed |

The two protocol harnesses each execute the same 39-member corpus, so 111 is a count of
harness/input executions, not distinct files. Shipped-dependency findings are separate from
npm's complete development graph, whose installation reported two moderate and two high findings.

[GitHub quality run 34750250655](https://github.com/scshafe/blob-royale/actions/runs/34750250655)
passed on the exact deployed commit, completing at 11:29:42 UTC. Its pull-request profile
runs GCC debug, ASan/UBSan and TSan tests plus the fixed fuzz corpus; the native release
profile above also runs GCC release tests and bounded fuzzing. Installation was held until
CI succeeded, then the existing verified installer resumed and completed.

## Independent installed-site observations

Publication, source stamp, container labels and image content agree. Installed configuration,
maps and web bytes match the certified inputs; the configuration mount is read-only. The
container was running with zero restarts. Local and tailnet liveness/readiness passed.
All four healthy rooms advanced from ticks 14,214 to 14,618 through the tailnet probe
(local observations were 14,215 to 14,619), publishing protocol 3.1.
The complete Tailscale Serve configuration matches the backup, including unrelated handlers;
Funnel remains disabled.

A separate Chromium 151.0.7922.34 session loaded the installed HTTPS site, joined Room 4,
and received 25 snapshots, advancing from tick 23,302 to 23,782. It observed all five sliders,
both rotation buttons, the control hints and Manual/Follow camera selection. At 1440×900,
900×600 and 390×844, document dimensions matched the viewport, the information panel scrolled
450 pixels, and the canvas position stayed fixed under both panel and arena wheel gestures.
Root visually inspected all three saved screenshots and confirmed a painted, contained arena.

The browser left the room and reported no page errors. No gameplay, match-start, roster or
tuning command was submitted. Live checks establish installed UI and delivery; the native
Chromium fixtures exercise the actual Go/brake/turn/charge/tuning interactions. Desktop
computer-use surfaces had no accessible window, so the separate native headless browser used
the host network for tailnet DNS. This did not alter production routing.

## Benchmark scope and prerequisite corrections

The native benchmark passed on the same clean checkout before deployment. Its artifact SHA-256
is `b1b5723c6c8ec1377011876d6e573298e331fc90042627e0c4cac0e159e7358a`.
Independent statistical review found all eight case correctness objects, solver work and delivery
results identical to `9578dfa`. Against the previous live `8b2e9ac` result, random crossings
change historical Royale to hash `fnv1a64:462b989acd4dfd45`, seven players and nine entities;
the other seven cases' correctness and all solver work/delivery results remain unchanged.
Across nine samples of 6,400 ticks, Royale median mean was 8.892 microseconds and median p99
was 11.246 microseconds. Delivery remained 4,096 published, 257 delivered, 3,839 coalesced,
at most two retained, maximum lag 15 slots and final lag zero.
These powersave-governor observations repeat one seeded trajectory. They do not certify
four-room hill capacity or establish a speed regression/improvement across changed workloads.

Source review corrected Sandbox advertising crossing rates without an installed spawner and
off-canvas aim loss releasing held Space. The previous candidate `9578dfa` then failed
[CI run 34747467055](https://github.com/scshafe/blob-royale/actions/runs/34747467055) on a
browser predicate requiring the victim to remain faster than the attacker at publication.
Immediate recharge permits another burst between impact and publication, so that comparison
is not invariant. The failure log establishes timeout, not the exact intervening motion.
The reviewed test-only repair requires moving victim, active hit stun and spatial order while
retaining charge/refund timing, input generation, hole elimination, geometry and deadlines.
The old native attempt passed its four C++ suites but was canceled before installation.
Its partial evidence remains distinct from this fresh, complete release.

The accepted [bot arrival-brake contract](2026-09-12-arrival-brake-contract.md) remains
conditional; this release makes no universal bot zero-overshoot claim. Automated verification
does not settle balance; the [human playtest agenda](2026-09-12-combat-balance-playtest-agenda.md)
remains available.
