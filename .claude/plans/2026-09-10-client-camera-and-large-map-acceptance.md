# Plan: Client camera and large-map acceptance

**Goal:** Render a readable local window onto large worlds, with strict player-follow and independent manual panning, using one client-only projection for every world layer.
**Out of scope:** Changing gameplay/protocol, switching or redeploying the live hill server, adding zoom/minimap/server visibility filtering, collecting human impressions by automation, or claiming the later live race playtest is complete.

## Context

- Continues the camera follow-up from completed `.claude/plans/2026-09-09-king-of-the-hill-and-race-modes.md`; baseline `d0f0577` includes the final compact hill verification note and is one local documentation commit ahead of the pushed/deployed `b1e80d4`.
- The accepted world/viewport contract is `docs/architecture/0004-gameplay-architecture.md`, “World space and the client viewport”; ADR 0007 links it. At baseline `d0f0577`, `SimulationCanvas` fit the entire map, entity renderers multiplied independent scales, and the race renderer separately scaled the canvas.
- Follow resolves a body using the current session controller identity. The immutable `SimulationSessionIdentity` object changes for every welcome, even when numeric IDs repeat; preserve the viewer's unrelated debug disclosure across reconnects.
- Chosen first UX: default Follow player; explicit Manual view/Follow player buttons; drag in manual mode and named directional pan buttons. Manual keeps its selected centre through snapshots. WASD/arrows remain steering; camera actions have no gameplay sender. Manual centre stays within map bounds, while the viewport can reveal outside-map background. Strict follow is never edge-clamped.
- One world unit occupies one CSS pixel initially. The responsive viewport is capped at 960×640 CSS pixels with a fixed 3:2 aspect; resizing changes visible world extent. DPR only sizes/transforms the backing buffer. Initial view before the first body is map centre; bodyless follow retains its last centre.
- Alternatives: retaining fit-all as the governing projection fails readable large-map play; a shared uniform translated projection permits follow, manual, and a future spectator target without parallel renderers. Zoom and an overview option are not needed for this slice.
- The baseline 1920×1280 browser fixtures are useful inputs but asserted 0.5-scale overview coordinates. Update their real canvas assertions and add explicit larger-than-view, independent-camera, pan/no-thrust, and follow/recovery evidence. Do not weaken validation or crop input before validation.

## Steps

- [x] **Step 1: Unify translated world projection**
  - Verify: `./scripts/run-linux-toolchain -- npm --prefix frontend-react run test:ci -- src/features/simulation/rendering`
  - Notes: Add one typed uniform projection and point/distance conversion. Move all entity layers and race course/gates onto it. Test translation, distance/roundtrip correctness, all-layer alignment, immutable world input, and invalid projection inputs. No independent per-mode transform survives.
  - Verification (2026-09-10): The pinned Linux wrapper passed all 42 rendering tests, including projection guards, translated entity layers, and race/body alignment; the combined camera-core run passed 65 tests. Log: `/tmp/blob-royale-camera-core-tests.log`. Advisory on this Mac host.

- [x] **Step 2: Implement session-local camera policy**
  - Verify: `./scripts/run-linux-toolchain -- npm --prefix frontend-react run test:ci -- src/features/simulation/useSimulationCamera.test.ts`
  - Notes: Default follow, explicit manual, initial centre, controller/body lookup, absent/bodyless retention, replacement-body tracking, manual limits, new-session/room reset, and independent hook instances. Camera APIs contain no transport or gameplay sender.
  - Verification (2026-09-10): All 23 camera-hook tests passed in the same pinned run, including repeated numeric IDs under a fresh welcome and rejection of a stale previous-room target. No transport or steering implementation was changed.

- [x] **Step 3: Integrate responsive camera controls**
  - Verify: `./scripts/run-linux-toolchain -- npm --prefix frontend-react run test:ci -- src/features/simulation/SimulationCanvas.test.tsx src/features/simulation/SimulationViewer.test.tsx src/features/simulation/useThrustInput.test.ts`
  - Notes: Logical CSS viewport and DPR backing buffer, true projected world boundary/outside background, pointer-capture drag with cancellation, accessible ordinary buttons, screen-space HUD. Unit/integration tests prove centring at edges, resize/DPR independence, manual persistence and no accidental thrust. Preserve unrelated viewer state and existing command behavior.
  - Verification (2026-09-10): Focused integration passed 39 tests; the subsequent full web gate passed all 349 tests after adding fractional-DPR, density-query rearming, and drag-cleanup assertions. Formatting, generated protocol drift/examples, strict typecheck, warning-free lint, and production build passed. Log: `/tmp/blob-royale-camera-verify-web.log`. Advisory on this Mac host.

- [x] **Step 4: Prove large-map browser behavior**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-web && ./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`
  - Notes: Preserve every existing browser scenario with camera-correct geometric expectations. Add production-browser evidence for a world wider/taller than its view, strict follow, manual pan with no gameplay command, resuming follow, independently positioned clients, resize, and body replacement/return where the fixtures support it. No retries, skips, mocked production transport, or deployed map edits.
  - Verification (2026-09-10): Full web passed 349 tests; all 8 production Chromium scenarios passed first attempt with zero retries/skips, including the independent-camera flow and strengthened race/recovery checks. Log: `/tmp/blob-royale-camera-browser.log`. Advisory on this Mac host.
  - Final-repeat correction (2026-09-10): The repeat browser gate caught a stale recorded paint after the four-step drag: its pre-action frame-count threshold could already be satisfied during the action. The failure screenshot showed the final position, while the recorder still exposed the penultimate position. Require several completed paints after an action-time frame baseline, retaining every geometric/command assertion. Step reopened until fresh verification passes.
  - Corrected verification (2026-09-10): An independent reviewer confirmed the observation-boundary diagnosis. Fresh full web passed 349 tests and full browser passed all 8 scenarios with zero retries/skips. Logs: `/tmp/blob-royale-camera-corrected-web.log` and `/tmp/blob-royale-camera-corrected-browser.log`. Step reclosed on that evidence; an additional three-run camera regression check is included in final verification.

- [x] **Step 5: Align implemented camera documentation**
  - Verify: `git diff --check && git diff --exit-code d0f0577 -- src tests/fixtures maps deploy docs/protocol/schema`
  - Notes: Update ADRs, simulation README/user controls, and a camera review/verification note. Record concrete choices and tested scope; preserve historical playtest evidence and pending human/race live play. Check no world/gameplay/protocol/deployment changes were introduced.
  - Verification (2026-09-10): ADRs 0004/0007, root/client/domain READMEs, and `docs/reviews/2026-09-10-client-camera-review.md` agree on implementation and pending deployment/human work. Diff whitespace check passed; the authoritative source/map/deployment/protocol scope comparison against `d0f0577` was empty.

- [x] **Step 6: Review and verify the completed slice**
  - Verify: `./scripts/run-linux-toolchain -- ./scripts/verify-web && ./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e && git diff --check`
  - Notes: Independent review against the accepted camera contract and fresh full client/browser results. Commit verified implementation and docs locally. No push/deployment is included in this new follow-up slice; a later authorized deployment must pass its own CI/native release workflow.
  - Verification (2026-09-10): Independent source/renderer/browser/documentation reviews completed, with the documented test synchronization correction. Fresh corrected full gates passed 349 web tests and all 8 browser scenarios; three additional camera runs passed with retries disabled. Diff checks and authoritative-scope comparison passed. Final logs: `/tmp/blob-royale-camera-corrected-web.log`, `/tmp/blob-royale-camera-corrected-browser.log`, `/tmp/blob-royale-camera-repeat-browser.log`. This plan and verified slice are committed together locally; no push/deployment.

## Done criteria

Every box is checked only after its verify passes. All client tests and all discovered production-browser scenarios pass without skips/retries; one translated uniform projection serves every world layer; readable large-map follow/manual behavior is demonstrated without modifying authoritative coordinates or sending camera gameplay commands. The implementation and aligned docs are committed locally with exact verification counts and limitations. Mac-hosted Linux/amd64 runs are advisory, not native deployment certification. Human play impressions and the later live race session remain separate work.

**Amended 2026-09-10:** The final repeat exposed a camera-test paint-completion race. Step 4 is reopened for the stricter post-action observation boundary; implementation scope and acceptance assertions are unchanged.
