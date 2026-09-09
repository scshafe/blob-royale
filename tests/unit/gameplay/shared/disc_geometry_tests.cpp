#include "shared/disc_geometry.hpp"

#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;

TEST_CASE("a centre on the rim is inside and one past the tolerance is outside",
          "[unit][gameplay][shared][geometry]") {
  const simulation::Vector2 center = simulation::Vector2::create(480.0, 320.0);

  CHECK_FALSE(gameplay::center_is_outside(center, center, 100.0));
  CHECK_FALSE(
      gameplay::center_is_outside(simulation::Vector2::create(580.0, 320.0), center, 100.0));
  CHECK_FALSE(
      gameplay::center_is_outside(simulation::Vector2::create(480.0, 220.0), center, 100.0));
  CHECK(gameplay::center_is_outside(simulation::Vector2::create(580.001, 320.0), center, 100.0));
  CHECK(gameplay::center_is_outside(simulation::Vector2::create(480.0, 419.999), center, 99.99));
}

TEST_CASE("a zero radius keeps only the centre itself inside",
          "[unit][gameplay][shared][geometry]") {
  const simulation::Vector2 center = simulation::Vector2::create(10.0, 10.0);
  CHECK_FALSE(gameplay::center_is_outside(center, center, 0.0));
  CHECK(gameplay::center_is_outside(simulation::Vector2::create(10.0, 10.001), center, 0.0));
}
