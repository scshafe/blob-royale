#ifndef BLOB_ROYALE_SIMULATION_RANDOM_STREAMS_HPP
#define BLOB_ROYALE_SIMULATION_RANDOM_STREAMS_HPP

#include "deterministic_random.hpp"
#include "random_stream_registry.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace blob_royale::simulation {

// canonical: random_streams -- owned deterministic generators for every registered world stream.
// Fixed value storage makes copying and non-throwing commit cover every stream, with no borrowed
// state or per-stream transaction mechanism. Initialization consumes no draws; GameWorld owns
// this complete value and snapshots project only its per-stream counts.
// related: random_stream_registry.hpp -- identities, ordinal order, and published count shape.
class RandomStreams final {
public:
  [[nodiscard]] static RandomStreams create(const std::uint64_t match_seed) noexcept {
    return [&]<std::size_t... Indices>(std::index_sequence<Indices...>) {
      return RandomStreams(std::array{DeterministicRandom::create(
          initial_seed(match_seed, kRandomStreamRegistry[Indices]))...});
    }(std::make_index_sequence<kRandomStreamCount>{});
  }

  RandomStreams(const RandomStreams&) = default;
  RandomStreams(RandomStreams&&) noexcept = default;
  RandomStreams& operator=(const RandomStreams&) = default;
  RandomStreams& operator=(RandomStreams&&) noexcept = default;
  ~RandomStreams() = default;

  // Throws SIMULATION.RANDOM_STREAM_KIND_INVALID for an unregistered runtime enum value.
  [[nodiscard]] DeterministicRandom& get(const RandomStreamKind kind) & {
    return streams_[random_stream_index(kind)];
  }
  [[nodiscard]] const DeterministicRandom& get(const RandomStreamKind kind) const& {
    return streams_[random_stream_index(kind)];
  }
  [[nodiscard]] const DeterministicRandom& get(RandomStreamKind kind) const&& = delete;

  [[nodiscard]] RandomDrawCounts draw_counts() const noexcept {
    RandomDrawCounts counts{};
    for (std::size_t index = 0; index < kRandomStreamCount; ++index) {
      counts[index] = streams_[index].draw_count();
    }
    return counts;
  }

  friend bool operator==(const RandomStreams&, const RandomStreams&) = default;

private:
  // Hazards preserve the old seed, state, and first draw exactly. For ordinal n > 0 the seed is
  // mix64(match_seed + golden_gamma * n), with every operation unsigned modulo 2^64. This is seed
  // derivation, not a draw from hazards or an extra advancement of the destination generator.
  [[nodiscard]] static std::uint64_t initial_seed(const std::uint64_t match_seed,
                                                  const RandomStreamDefinition stream) noexcept {
    if (stream.kind == RandomStreamKind::kHazards) {
      return match_seed;
    }
    return DeterministicRandom::mix_bits(match_seed + DeterministicRandom::kGoldenGammaIncrement *
                                                          static_cast<std::uint64_t>(stream.kind));
  }

  explicit RandomStreams(std::array<DeterministicRandom, kRandomStreamCount> streams) noexcept
      : streams_(std::move(streams)) {}

  std::array<DeterministicRandom, kRandomStreamCount> streams_;
};

} // namespace blob_royale::simulation

#endif
