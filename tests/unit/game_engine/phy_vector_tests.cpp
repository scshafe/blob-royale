#include "phy_vector.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("phy vector addition preserves component sums", "[unit][game_engine][phy_vector]") {
  PhyVector left{1.25F, -2.5F};
  const PhyVector right{3.75F, 6.5F};

  const PhyVector result = left + right;

  CHECK(result.x == Catch::Approx(5.0F));
  CHECK(result.y == Catch::Approx(4.0F));
}

TEST_CASE("phy vector magnitude uses Euclidean distance", "[unit][game_engine][phy_vector]") {
  PhyVector vector{3.0F, 4.0F};

  CHECK(vector.get_magnitude() == Catch::Approx(5.0F));
}
