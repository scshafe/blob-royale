#include "shared/spawn_point_probe.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <span>

namespace gameplay = blob_royale::gameplay;

TEST_CASE("the probe walks forward from the counter and wraps", "[unit][gameplay][shared][spawn]") {
  constexpr std::array<bool, 4> all_free{true, true, true, true};
  CHECK(gameplay::next_free_spawn_point(0, all_free) == 0);
  CHECK(gameplay::next_free_spawn_point(2, all_free) == 2);
  CHECK(gameplay::next_free_spawn_point(3, all_free) == 3);
  // A counter past the end wraps rather than failing: the SpawnSystem stores `(chosen + 1) mod n`,
  // but a world seeded by hand may carry any value.
  CHECK(gameplay::next_free_spawn_point(5, all_free) == 1);

  constexpr std::array<bool, 4> only_first_free{true, false, false, false};
  CHECK(gameplay::next_free_spawn_point(2, only_first_free) == 0);
}

TEST_CASE("a full ring and an empty ring both defer", "[unit][gameplay][shared][spawn]") {
  constexpr std::array<bool, 3> all_taken{false, false, false};
  CHECK(gameplay::next_free_spawn_point(1, all_taken) == std::nullopt);
  CHECK(gameplay::next_free_spawn_point(0, std::span<const bool>{}) == std::nullopt);
}
