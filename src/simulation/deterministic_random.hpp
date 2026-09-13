#ifndef BLOB_ROYALE_SIMULATION_DETERMINISTIC_RANDOM_HPP
#define BLOB_ROYALE_SIMULATION_DETERMINISTIC_RANDOM_HPP

#include "simulation_validation_error.hpp"

#include <cstdint>
#include <limits>
#include <string>

namespace blob_royale::simulation {

// canonical: deterministic_random -- the only randomness source inside a tick.
//
// The generator is SplitMix64, written out here rather than taken from `<random>`, because the
// standard engines are bit-specified but the standard *distributions* are not: two conforming
// libraries may return different values from the same engine state, which would make a match's
// outcome a property of the toolchain (`docs/architecture/0004-gameplay-architecture.md`
// § "Determinism obligations for framework code").
//
// Each named generator is owned by GameWorld through RandomStreams, so a draw is part of the
// committed world and a replay of `(map, mode configuration, seed, command log)` reproduces it
// exactly. Each stream's draw_count is committed in every snapshot, so two runs that diverge in
// **how many** draws they took diverge visibly at the first differing tick instead of silently
// later.
//
// Every operation is a pure function of the generator's own state: no clock, no global, and no
// per-worker storage of any kind, so the only way two runs differ is by drawing a different number
// of times.
// related: random_streams.hpp -- the owner and seed derivation of each named generator.
// related: world_snapshot.hpp -- the publication that carries per-stream draw counts.
class DeterministicRandom final {
public:
  // The SplitMix64 constants, named so a reader can check them against the published algorithm
  // rather than against another copy of this file.
  static constexpr std::uint64_t kGoldenGammaIncrement = 0x9E37'79B9'7F4A'7C15ULL;
  static constexpr std::uint64_t kFirstMixMultiplier = 0xBF58'476D'1CE4'E5B9ULL;
  static constexpr std::uint64_t kSecondMixMultiplier = 0x94D0'49BB'1331'11EBULL;

  [[nodiscard]] static DeterministicRandom create(const std::uint64_t seed) noexcept {
    return DeterministicRandom(seed);
  }

  // canonical: splitmix64_finalizer -- pure unsigned mixing for deterministic stream seeds.
  // No generator is advanced. Both next_bits and stream seed derivation use this finalizer; its
  // independent frozen proof retains the pre-extraction arithmetic and distribution behavior.
  [[nodiscard]] static constexpr std::uint64_t mix_bits(std::uint64_t mixed) noexcept {
    mixed = (mixed ^ (mixed >> 30U)) * kFirstMixMultiplier;
    mixed = (mixed ^ (mixed >> 27U)) * kSecondMixMultiplier;
    return mixed ^ (mixed >> 31U);
  }

  DeterministicRandom(const DeterministicRandom&) = default;
  DeterministicRandom(DeterministicRandom&&) noexcept = default;
  DeterministicRandom& operator=(const DeterministicRandom&) = default;
  DeterministicRandom& operator=(DeterministicRandom&&) noexcept = default;
  ~DeterministicRandom() = default;

  // One 64-bit draw. Advances the state and the draw count exactly once.
  [[nodiscard]] std::uint64_t next_bits() noexcept {
    ++draw_count_;
    state_ += kGoldenGammaIncrement;
    return mix_bits(state_);
  }

  // A value in [0, 1) built from the top 53 bits, which is exactly the number of bits a binary64
  // mantissa can hold, so every representable output is reachable and none is reachable twice.
  [[nodiscard]] double next_unit_interval() noexcept {
    return static_cast<double>(next_bits() >> 11U) * 0x1.0p-53;
  }

  // A value in [0, bound) with no modulo bias, by rejection sampling. Throws
  // SimulationValidationError for a zero bound, because "a value below zero" names no value and a
  // sentinel would be a fallback that hides a caller defect.
  //
  // The rejected range is the first `2^64 mod bound` values: what remains is a contiguous range
  // whose length is a multiple of `bound`, and any such range covers every residue class exactly
  // equally. Each rejection is a visible draw, so `draw_count` records the real cost and two runs
  // that rejected differently are already unequal.
  [[nodiscard]] std::uint64_t next_below(const std::uint64_t bound) {
    if (bound == 0) {
      throw SimulationValidationError(SimulationValidationCode::kDeterministicRandomBoundEmpty,
                                      "deterministic_random.next_below.bound",
                                      "a draw below zero names no value");
    }
    const std::uint64_t rejection_threshold =
        (std::numeric_limits<std::uint64_t>::max() - bound + 1ULL) % bound;
    std::uint64_t drawn = next_bits();
    while (drawn < rejection_threshold) {
      drawn = next_bits();
    }
    return drawn % bound;
  }

  // The seed this generator was created with, unchanged by any draw.
  [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }

  // How many 64-bit draws this generator has produced, rejections included.
  [[nodiscard]] std::uint64_t draw_count() const noexcept { return draw_count_; }

  friend bool operator==(const DeterministicRandom&, const DeterministicRandom&) = default;

private:
  explicit DeterministicRandom(const std::uint64_t seed) noexcept : seed_(seed), state_(seed) {}

  std::uint64_t seed_;
  std::uint64_t state_;
  std::uint64_t draw_count_{};
};

} // namespace blob_royale::simulation

#endif
