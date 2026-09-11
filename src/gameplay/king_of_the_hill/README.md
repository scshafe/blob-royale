<!-- canonical: king_of_the_hill_domain -- capture geometry, motion, scoring, and match rules -->

# King of the hill

This domain depends only on simulation values and shared gameplay mechanics. The mode declares
one `HillMovementSystem` before scoring; both motion policies publish the same `Hill` circle.

- `hill_geometry` owns the unchanged marker-tour arithmetic, with no random draws.
- `hill_roaming` owns rational-direction sampling, scalar speed and retarget selection, and
  per-axis outer-boundary cancellation. It calls canonical simulation motion arithmetic and the
  world's named hill generator; it does not query terrain or create another generator.
- `hill_movement_system` owns the non-participant hill entity and its round lifecycle. Roaming
  stores `HillMotion`; publication removes its optional private schedule. Lobby/countdown reset,
  running advances before scoring, and ended freezes.
- `king_of_the_hill_configuration` validates authored policy/ranges and converts seconds once.
  The application and replay loaders author these fields explicitly; neither chooses a fallback.

Interior cliffs, holes, and spawn connectivity do not constrain the capture zone or become safe
because it passes over them. Closed map bounds constrain the center, allowing circle overhang.
Strict overshoot discards that axis's whole displacement and zeros its velocity; exact arrival
commits position and zeros outward velocity. Tangential motion is preserved without normalization,
reflection, or early retarget. Edge/corner rests may persist across repeated outward selections.

The sampler's written call order is quarter-circle parameter, quadrant, scalar speed, interval.
Angular density is not uniform; vector norms have binary64 rounding. Equal ranges still draw and
canonical bounded-integer rejection remains observable. See ADR 0008 for intake bounds and the
execution plan's Step 12 for verification obligations. Future trajectory policies belong at this
same motion seam; neither scoring nor rendering should gain policy-specific geometry.
