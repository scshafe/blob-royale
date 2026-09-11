#ifndef BLOB_ROYALE_TESTING_TERRAIN_PROVENANCE_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_TERRAIN_PROVENANCE_FIXTURE_HPP

#include "simulation_limits.hpp"
#include "terrain_definition.hpp"
#include "vector2.hpp"

#include <string_view>
#include <vector>

namespace blob_royale::testing::terrain_provenance_fixture {

// Original Step 8 controller-proof geometry: do not adjust coordinates or authored width to make
// terrain admission succeed. Unlike effective-radius topology fixtures, width 80 is literal here.
inline constexpr double kRaceWidth = 960.0;
inline constexpr double kRaceHeight = 640.0;
inline constexpr double kAuthoredRoadHalfWidth = 80.0;

struct ObliqueCourse final {
  std::string_view name;
  std::vector<simulation::Vector2> points;
};

[[nodiscard]] inline std::vector<ObliqueCourse> oblique_courses() {
  return {{"sloped",
           {simulation::Vector2::create(100.0, 100.0), simulation::Vector2::create(500.0, 400.0)}},
          {"fractional",
           {simulation::Vector2::create(100.125, 100.375),
            simulation::Vector2::create(700.625, 500.875)}}};
}

// Original Step 7 failure, copied from the durable clipped-corner admissibility review. These are
// authored width 10 and hole radius 15, NOT the calibrated shared publication fixture's values.
[[nodiscard]] inline simulation::TerrainDefinition original_clipped_corner() {
  return simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(100.0, 100.0), simulation::TerrainGround::kCorridors,
      {simulation::TerrainCorridor::create("edge_road", 10.0,
                                           {simulation::Vector2::create(0.0, 0.0),
                                            simulation::Vector2::create(100.0, 0.0),
                                            simulation::Vector2::create(100.0, 100.0)})},
      {simulation::TerrainHole::create("edge_pit", simulation::Vector2::create(100.0, 50.0),
                                       15.0)});
}

struct SupportProbe final {
  simulation::Vector2 point;
  bool supported;
};

[[nodiscard]] inline std::vector<SupportProbe> original_clipped_corner_probes() {
  return {{simulation::Vector2::create(0.0, 0.0), true},
          {simulation::Vector2::create(50.0, 10.0), true},
          {simulation::Vector2::create(50.0, 11.0), false},
          {simulation::Vector2::create(90.0, 30.0), true},
          {simulation::Vector2::create(100.0, 50.0), false},
          {simulation::Vector2::create(100.0, 65.0), true},
          {simulation::Vector2::create(100.0, 100.0), true},
          {simulation::Vector2::create(110.0, 30.0), false}};
}

// New exact-topology controls, following terrain_queries_tests.cpp's effective-radius convention.
// They do not replace or adjust any of the original failing authored geometries above.
inline constexpr double kContactRoadRadius = 20.0;
inline constexpr double kContactHoleRadius = 10.0;

[[nodiscard]] inline simulation::TerrainDefinition
contact_control(const simulation::Vector2& hole_center) {
  return simulation::TerrainDefinition::create(
      simulation::ArenaBounds::create(1000.0, 1000.0), simulation::TerrainGround::kCorridors,
      {simulation::TerrainCorridor::create(
          "contact_road", kContactRoadRadius - simulation::kPositionTolerance,
          {simulation::Vector2::create(100.0, 100.0), simulation::Vector2::create(200.0, 100.0)})},
      {simulation::TerrainHole::create("contact_hole", hole_center,
                                       kContactHoleRadius + simulation::kPositionTolerance)});
}

[[nodiscard]] inline simulation::TerrainDefinition unrelated_crossing_at_join() {
  return contact_control(simulation::Vector2::create(110.0, 120.0));
}

[[nodiscard]] inline simulation::Vector2 crossing_contact() {
  return simulation::Vector2::create(100.0, 120.0);
}

[[nodiscard]] inline simulation::Vector2 crossing_hole_center() {
  return simulation::Vector2::create(110.0, 120.0);
}

[[nodiscard]] inline std::vector<SupportProbe> crossing_probes() {
  return {{crossing_contact(), true},
          {simulation::Vector2::create(99.0, 119.0), true},
          {simulation::Vector2::create(101.0, 119.0), false},
          {simulation::Vector2::create(99.0, 121.0), false},
          {crossing_hole_center(), false}};
}

[[nodiscard]] inline simulation::TerrainDefinition different_curvature_tangent() {
  return contact_control(simulation::Vector2::create(90.0, 100.0));
}

[[nodiscard]] inline simulation::Vector2 curvature_contact() {
  return simulation::Vector2::create(80.0, 100.0);
}

[[nodiscard]] inline simulation::Vector2 curvature_outside() {
  return simulation::Vector2::create(79.0, 100.0);
}

[[nodiscard]] inline simulation::Vector2 curvature_supported_cusp() {
  return simulation::Vector2::create(80.15, 102.0);
}

[[nodiscard]] inline std::vector<SupportProbe> curvature_probes() {
  return {{curvature_contact(), true},
          {curvature_supported_cusp(), true},
          {simulation::Vector2::create(80.05, 102.0), false},
          {simulation::Vector2::create(80.3, 102.0), false},
          {simulation::Vector2::create(90.0, 100.0), false}};
}

} // namespace blob_royale::testing::terrain_provenance_fixture

#endif
