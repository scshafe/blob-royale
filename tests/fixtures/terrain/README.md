# Shared terrain geometry goldens

`geometry-goldens.json` is test data shared by the C++ terrain factory/query tests and the
browser's session validation and terrain rendering tests. Each case contains the exact v3
welcome terrain shape and named support probes. It is not another geometry implementation.

The production `TerrainDefinition` factory validates every C++ case before the canonical
support query checks its probes. Browser tests validate that same authored shape and inspect
rendering operations through the existing camera projection. Overlapping holes must subtract
their union, including where corridors overlap; an even-odd path containing all holes would
incorrectly paint their overlap as ground. Separate outer-envelope probes pin clipping.

Exact rims and corridor edges are supported by the simulation's established boundary policy;
these probes deliberately avoid subpixel tolerance-band comparisons with raster antialiasing.

The clipped bent-road case authors width `10 - 1e-9` and hole radius `15 + 1e-9` so the
simulation's established position tolerance produces exact effective radii 10 and 15. This
matches the effective-radius convention in `terrain_queries_tests.cpp` and isolates the
envelope/cap/hole topology from independently rounded boundary incidence. The initial draft's
unadjusted 10/15 input was rejected by the existing geometry-precision guard; no production
geometry arithmetic or admission guard is changed for this fixture.
