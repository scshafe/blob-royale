# Native release and deployment receipt

Step 24 is complete. The owner authorized push and deployment, and the canonical
`./scripts/deploy-tailnet` installed commit `8b2e9ac5ce8677567e7d3d6051ebfa511dec598f`
on `cole-ubuntu-pc` on 2026-09-13 UTC (2026-09-12 America/Los_Angeles).
The game is available at <https://cole-ubuntu-pc.colobus-stargazer.ts.net:8444/>.
The later documentation receipt commit does not identify a different deployed runtime.

The [machine-readable receipt](2026-09-12-native-release-baseline.json) records the
source, server, web, verification stamp, release, image content identity, evidence hashes,
benchmark results, and independent live probes. The authoritative release ID is
`release-2e76395af1b5dd409b34d15c1afb66547e018de5637d7cb4af83e7a75bb4bbaf`;
the running image content ID is
`sha256:9cab0f74507de29df2f378a9abe0b0cdbbcc08192507ca9a270ca1e8aaecc1ce`.
The publication/evidence ID suffix is not the image content ID.

## Completed gates

The native host and Docker daemon are Linux/x86_64. The clean checkout used pinned
toolchain `ubuntu-24.04-amd64-20260804`; the existing sanitizer prerequisite
`vm.mmap_rnd_bits=28` was confirmed without changing it. Root ran one C++ build at a time.

| Gate | Result |
|---|---|
| Clean GCC debug/release, Clang ASan/UBSan, Clang TSan builds | All four passed |
| Full CTest suites | 1,896/1,896 in each of four lanes; 7,584 executions |
| Native process smoke | Passed |
| Full web verification | 986/986 |
| Chromium scenarios | 24/24; zero retries or skips |
| Fixed fuzz corpus | 99 inputs across seven harnesses |
| Bounded fuzzing | Seven 30-second campaigns; seed 424242 |
| Source/artifact verification and release publication | Passed, authoritative |
| Runtime/web dependency evidence | Passed; zero high/critical findings |
| Canonical install, container readiness, Tailscale Serve, liveness and site root | Passed |
| `git diff --check` | Passed |

[GitHub quality run 34739448828](https://github.com/scshafe/blob-royale/actions/runs/34739448828)
also passed on the exact deployed commit. Its pull-request profile is separate from the
full native release profile above. All required acceptance tests ran without retries or skips.

The [earlier release review](2026-09-12-release-review.md) retains three prerequisite
findings: GCC's optimized optional-reference fixture diagnostic, the aggregate one-hour
CI timeout, and an earlier completed browser frame preceding participant/gate paint.
Commits `62e6879`, `5ebd974`, and `8b2e9ac` repair fixture storage, job capacity, and
observation synchronization respectively. Exact assertions, warnings, individual test
deadlines, production behavior, and the deployment configuration were preserved.
Failed attempts installed nothing; their evidence remains distinct from this successful run.

## Native benchmark scope

`./scripts/run-benchmarks-linux` passed on the same clean native checkout before the
canonical deployment. Its eight cases comprise one live kernel case, one historical
Royale roster, three spatial-grid diagnostics, and three pure motion-solver cases.
All correctness objects, pure-solver workloads/work counts, and delivery policy/results
match the preceding native `62e6879` benchmark. Independent statistical review confirmed
that comparison and the category boundaries.

Historical Royale median mean tick cost was 7.473 microseconds; median p99 was 13.061
microseconds, across nine samples of 6,400 ticks. Its advisory budget indicators passed.
The powersave-governor measurements do not certify current four-room hill capacity or
512/2,048-player live capacity, and timing variation is not a regression/improvement claim.
The delivery workload published 4,096 snapshots, delivered 257, coalesced 3,839, retained
at most two, and finished with zero tick lag (maximum delivered lag 15).

## Independent live verification

The publication and verification stamp agree on the clean source commit and authority.
The running container's commit/release labels, image reference, and image content ID match
that publication. Installed configuration, maps, and web bytes match the certified inputs;
the configuration mount is read-only. The container was running with zero restarts.

Both local and tailnet liveness/readiness probes passed. The v3 directory reported all
four rooms healthy, each advancing from tick 10,504 to 10,908 between observations.
The complete Tailscale Serve configuration matches the pre-deployment backup, including
unrelated handlers; Funnel remains disabled.

Safari's live room page reported “Connected to the match session.” and a completed
snapshot at tick 99,737 with six entities and four players in a running match. The next
UI read was deferred by the computer-use tool because the owner was actively interacting;
no second browser tick is claimed. API observations above establish all-room progress.
No test roster, start command, or movement-tuning update was submitted during verification.

The accepted [arrival-brake contract](2026-09-12-arrival-brake-contract.md) remains
conditional; this release does not restore a universal no-overshoot claim. Numerical
and browser JSON-token limitations remain documented in the earlier review. Automated
verification does not settle fun or fairness; the separate
[human balance-playtest agenda](2026-09-12-combat-balance-playtest-agenda.md) remains available.
