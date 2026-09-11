#ifndef BLOB_ROYALE_TESTING_DETERMINISTIC_RANDOM_FROZEN_REFERENCE_HPP
#define BLOB_ROYALE_TESTING_DETERMINISTIC_RANDOM_FROZEN_REFERENCE_HPP

#include "simulation_validation_error.hpp"

#include <cstdint>
#include <limits>

namespace blob_royale::testing::deterministic_random_reference {

// Independent frozen reference for 42be52c's generator. Do not import production RNG constants,
// finalizer, seed derivation, or distribution helpers. The optional finalizer parameter exists
// only to exercise the proposed mixing extraction with these unchanged reference distributions;
// the default reference and the production-hazard oracle always use the frozen finalizer below.
struct FrozenFinalizer final {
  [[nodiscard]] static constexpr std::uint64_t mix_bits(std::uint64_t mixed) noexcept {
    mixed = (mixed ^ (mixed >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
    mixed = (mixed ^ (mixed >> 27U)) * 0x94D0'49BB'1331'11EBULL;
    return mixed ^ (mixed >> 31U);
  }
};

template <typename Finalizer = FrozenFinalizer> class Random final {
public:
  [[nodiscard]] static Random create(const std::uint64_t seed) noexcept { return Random(seed); }

  [[nodiscard]] std::uint64_t next_bits() noexcept {
    ++draw_count_;
    state_ += 0x9E37'79B9'7F4A'7C15ULL;
    return Finalizer::mix_bits(state_);
  }

  [[nodiscard]] double next_unit_interval() noexcept {
    return static_cast<double>(next_bits() >> 11U) * 0x1.0p-53;
  }

  [[nodiscard]] std::uint64_t next_below(const std::uint64_t bound) {
    if (bound == 0) {
      throw simulation::SimulationValidationError(
          simulation::SimulationValidationCode::kDeterministicRandomBoundEmpty,
          "deterministic_random.next_below.bound", "a draw below zero names no value");
    }
    const std::uint64_t rejection_threshold =
        (std::numeric_limits<std::uint64_t>::max() - bound + 1ULL) % bound;
    std::uint64_t drawn = next_bits();
    while (drawn < rejection_threshold) {
      drawn = next_bits();
    }
    return drawn % bound;
  }

  [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }
  [[nodiscard]] std::uint64_t draw_count() const noexcept { return draw_count_; }

  friend bool operator==(const Random&, const Random&) = default;

private:
  explicit Random(const std::uint64_t seed) noexcept : seed_(seed), state_(seed) {}

  std::uint64_t seed_;
  std::uint64_t state_;
  std::uint64_t draw_count_{};
};

using DeterministicRandom = Random<>;

} // namespace blob_royale::testing::deterministic_random_reference

#endif
