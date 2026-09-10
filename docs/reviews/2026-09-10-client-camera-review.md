<!-- canonical: client_camera_review -- camera follow-up scope, independent review, and verification -->

# Client camera review, 2026-09-10

Implements `.claude/plans/2026-09-10-client-camera-and-large-map-acceptance.md`
against baseline `d0f0577`, following ADR 0004's world/viewport contract.
This is a local client-only follow-up, not a new deployment or a human playtest.

## Implemented choices

- One uniform translated world projection serves bodies, labels, hill, zone, hazard/exposure
  rings, race course, and gates. HUD and controls remain in screen space. Complete validated
  snapshots, authoritative coordinates, gameplay, and transport are unchanged.
- Follow player is the default. It centres the current controller's body without edge clamping,
  retains the last centre while bodyless, and follows the replacement body. Before the first
  body it uses map centre. A new room or immutable welcome resets camera state, even when numeric
  IDs repeat; unrelated debug disclosure survives.
- Manual view retains its centre across snapshots. Dragging and named pan buttons move only
  that client's camera, clamped to the inclusive world rectangle. Follow player resumes tracking.
  Ordinary buttons support Tab/Enter/Space; WASD/arrows remain steering. Pointer release,
  cancellation, lost capture, released buttons, blur, mode change, and unmount terminate drags.
- Scale is one world unit per CSS pixel. A responsive 3:2 viewport is capped at 960×640 CSS pixels
  with integer rounding. DPR only sizes the backing buffer, capped at 4. Zero-area views do not
  draw. The actual map boundary is projected against a distinct outside-map background.

## Independent review

Separate read-only passes reviewed the integration and camera policy, the renderer changes, and
the shared browser recorder plus all seven existing scenarios. No concrete correctness findings
remained. The integration reviewer identified coverage gaps for odd viewport dimensions with
fractional DPR, media-query-only density changes/rearming, and additional pointer cleanup paths;
those assertions were added before the final full web gate.

The legacy browser geometry checks subtract the painted map boundary and actual backing/CSS
ratios, not private React camera state. They also require the real boundary to occupy 1920×1280
CSS pixels, preventing inverse normalization from hiding the old fit-all scale. Existing movement,
deflection, and contraction thresholds preserve their world distances; stationary-peer equality
permits only 1e-9 inverse-projection roundoff. Camera selection itself is checked by the new raw
paint assertions and added raw race/recovery assertions, not by normalized legacy geometry.

## Verification

The pinned Linux/amd64 wrapper on the Mac host passed:

- Rendering and camera core: 65 tests (42 rendering, 23 camera policy).
- Full web gate: 349 tests, zero failures/skips/todos; formatting, generated protocol drift,
  schema examples, strict TypeScript, warning-free lint, and production build also passed.
- Production Chromium: all 8 discovered scenarios passed on the first attempt, zero retries,
  flakes, or skips. The original seven scenarios remain; the eighth uses two real sessions over
  a 1920×1280 world and independently observed canvas paints and WebSocket sends.
- Large-map evidence includes strict edge centring, independent player views, DPR 1 and 2,
  keyboard-activated manual pan, drag/release, persistence through fresh snapshots, responsive
  resize with unchanged body size, resuming follow, and zero outgoing commands from camera actions.
  Race proves bodyless centre retention and return tracking; recovery proves fresh-welcome reset
  while preserving debug disclosure. Fractional DPR and additional lifecycle cases have unit coverage.
- `git diff --check` passed. `git diff --exit-code d0f0577 -- src tests/fixtures maps deploy
  docs/protocol/schema` was empty. Existing browser fixture configuration/map edits are comments
  only; no gameplay inputs, dependencies, or generated protocol files changed.

Reproduce from the repository root:

```sh
./scripts/run-linux-toolchain -- ./scripts/verify-web
./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e
git diff --check
```

Initial complete-gate logs are `/tmp/blob-royale-camera-verify-web.log` and
`/tmp/blob-royale-camera-browser.log`; these local temporary paths are not durable CI artifacts.
The subsequent full repeat passed web but exposed a timing defect in the new camera test: the
four-step drag could satisfy its pre-action frame threshold before the recorder published the
final drag paint. The recorded boundary was 20 pixels (one drag step) behind, while the failure
screenshot showed the correct final boundary. The test now captures a post-action frame baseline
and requires several newer completed paints. Geometry tolerances, real transport, command checks,
and retry policy are unchanged. An independent reviewer confirmed that diagnosis. Fresh corrected
full gates passed all 349 web tests and all 8 browser scenarios with zero skips/retries, recorded
in `/tmp/blob-royale-camera-corrected-web.log` and `/tmp/blob-royale-camera-corrected-browser.log`.
The corrected camera scenario also passed three independent consecutive runs with retries still
disabled (`--repeat-each=3 --workers=1`), logged in `/tmp/blob-royale-camera-repeat-browser.log`.
These Mac-hosted emulated results are advisory, not native release certification. No new C++ lane
or live service verification is claimed for this frontend-only change. The dependency install
reported four existing audit advisories (two moderate, two high); dependency remediation was not
part of this slice and no automatic audit fix was applied.

## Still separate

The recorded tailnet hill deployment at `b1e80d4` predates this camera. This slice does not push,
redeploy, or switch that server to race. Human large-map impressions, an authorized camera
deployment through the native release workflow, and the later live race playtest remain pending.
Zoom, minimap, spectator controls, server-side visibility filtering, and protocol changes are not
implemented or required by this slice. Historical mode review/playtest evidence remains unchanged.
