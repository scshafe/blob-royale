#include "deterministic_random.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] std::vector<std::uint64_t> first_draws(const std::uint64_t seed,
                                                     const std::size_t count) {
  simulation::DeterministicRandom generator = simulation::DeterministicRandom::create(seed);
  std::vector<std::uint64_t> drawn;
  drawn.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    drawn.push_back(generator.next_bits());
  }
  return drawn;
}

} // namespace

TEST_CASE("DeterministicRandom reproduces the published SplitMix64 sequence",
          "[unit][simulation][deterministic_random][determinism]") {
  // The generator is written out rather than taken from <random> because standard engines are
  // bit-specified but standard distributions are not. These are the reference outputs of
  // SplitMix64 seeded at zero, so a toolchain that computed anything else fails here rather than
  // silently producing a different match.
  simulation::DeterministicRandom generator = simulation::DeterministicRandom::create(0);

  CHECK(generator.next_bits() == 0xE220'A839'7B1D'CDAFULL);
  CHECK(generator.next_bits() == 0x6E78'9E6A'A1B9'65F4ULL);
  CHECK(generator.next_bits() == 0x06C4'5D18'8009'454FULL);
  CHECK(generator.seed() == 0);
  CHECK(generator.draw_count() == 3);
}

TEST_CASE("two DeterministicRandom values with the same seed produce the same sequence",
          "[unit][simulation][deterministic_random][determinism]") {
  CHECK(first_draws(1'234'567, 16) == first_draws(1'234'567, 16));
  CHECK_FALSE(first_draws(1'234'567, 16) == first_draws(1'234'568, 16));
}

TEST_CASE("DeterministicRandom draw_count counts every draw, so divergence is visible",
          "[unit][simulation][deterministic_random][determinism]") {
  // The draw count is committed in every snapshot, so two runs that diverge in how many draws they
  // took diverge visibly at the first differing tick instead of silently later.
  simulation::DeterministicRandom generator = simulation::DeterministicRandom::create(7);
  CHECK(generator.draw_count() == 0);

  static_cast<void>(generator.next_bits());
  static_cast<void>(generator.next_unit_interval());
  static_cast<void>(generator.next_below(10));

  CHECK(generator.draw_count() >= 3);
  CHECK(generator.seed() == 7);
  CHECK_FALSE(generator == simulation::DeterministicRandom::create(7));
}

TEST_CASE("DeterministicRandom next_unit_interval stays in the half-open unit interval",
          "[unit][simulation][deterministic_random]") {
  simulation::DeterministicRandom generator = simulation::DeterministicRandom::create(42);

  for (std::size_t draw = 0; draw < 2'000; ++draw) {
    const double value = generator.next_unit_interval();
    CHECK(value >= 0.0);
    CHECK(value < 1.0);
  }
}

TEST_CASE("DeterministicRandom next_below stays below its bound and covers every residue",
          "[unit][simulation][deterministic_random]") {
  // Rejection sampling rather than a modulo, so no residue class is over-represented. With a small
  // bound and a few thousand draws every class must appear.
  constexpr std::uint64_t bound = 7;
  simulation::DeterministicRandom generator = simulation::DeterministicRandom::create(99);
  std::vector<std::size_t> observed(bound, 0);

  for (std::size_t draw = 0; draw < 7'000; ++draw) {
    const std::uint64_t value = generator.next_below(bound);
    REQUIRE(value < bound);
    ++observed[static_cast<std::size_t>(value)];
  }

  for (const std::size_t count : observed) {
    CHECK(count > 0);
  }
}

TEST_CASE("DeterministicRandom next_below rejects a zero bound",
          "[unit][simulation][deterministic_random][validation]") {
  // "A value below zero" names no value, and a sentinel would be a fallback that hid a caller
  // defect.
  simulation::DeterministicRandom generator = simulation::DeterministicRandom::create(3);

  CHECK_THROWS_AS(static_cast<void>(generator.next_below(0)),
                  simulation::SimulationValidationError);
}

TEST_CASE("DeterministicRandom next_below is total at a bound of one",
          "[unit][simulation][deterministic_random]") {
  simulation::DeterministicRandom generator = simulation::DeterministicRandom::create(5);

  CHECK(generator.next_below(1) == 0);
}
