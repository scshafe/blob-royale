#ifndef BLOB_ROYALE_SIMULATION_RANDOM_STREAM_REGISTRY_HPP
#define BLOB_ROYALE_SIMULATION_RANDOM_STREAM_REGISTRY_HPP

#include "kind_registry.hpp"
#include "simulation_validation_error.hpp"
#include "snake_case_identity.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace blob_royale::simulation {

// canonical: random_stream_registry -- the closed order and identities of world-owned randomness.
// @extension-point random_stream
// Ordinals are deterministic seed inputs, not incidental array positions: append a new explicit
// ordinal and registry row without renumbering an existing stream. Hazards retain the legacy seed;
// all later ordinals use the written seed derivation in RandomStreams. Names are the wire keys.
enum class RandomStreamKind : std::uint8_t {
  kHazards = 0,
  kHill = 1,
};

struct RandomStreamDefinition final {
  RandomStreamKind kind;
  std::string_view name;
};

inline constexpr std::array kRandomStreamRegistry{
    RandomStreamDefinition{RandomStreamKind::kHazards, "hazards"},
    RandomStreamDefinition{RandomStreamKind::kHill, "hill"}};

inline constexpr std::size_t kRandomStreamCount = kRandomStreamRegistry.size();

static_assert(values_are_distinct(projected_values(
    kRandomStreamRegistry, [](const RandomStreamDefinition stream) { return stream.kind; })));
static_assert(values_are_distinct(projected_values(
    kRandomStreamRegistry, [](const RandomStreamDefinition stream) { return stream.name; })));
static_assert(
    [] {
      for (std::size_t index = 0; index < kRandomStreamCount; ++index) {
        if (static_cast<std::size_t>(kRandomStreamRegistry[index].kind) != index ||
            !is_wire_kind_name(kRandomStreamRegistry[index].name)) {
          return false;
        }
      }
      return true;
    }(),
    "random streams have valid names and contiguous stable ordinal order");

// Full uint64 counts belong to simulation. The protocol alone rejects counts outside its safe
// integer range; this value neither rounds nor narrows them and carries no seed or hidden state.
using RandomDrawCounts = std::array<std::uint64_t, kRandomStreamCount>;

// The single checked runtime lookup used by stream access and name/count readers. Unknown enum
// values are caller defects, never an array index or a fallback to the hazards stream.
[[nodiscard]] inline std::size_t random_stream_index(const RandomStreamKind kind) {
  const auto index = static_cast<std::size_t>(kind);
  if (index >= kRandomStreamCount || kRandomStreamRegistry[index].kind != kind) {
    throw SimulationValidationError(
        SimulationValidationCode::kRandomStreamKindInvalid, "random_stream.kind",
        "random stream ordinal " + std::to_string(index) + " is not registered");
  }
  return index;
}

[[nodiscard]] inline std::string_view random_stream_name(const RandomStreamKind kind) {
  return kRandomStreamRegistry[random_stream_index(kind)].name;
}

} // namespace blob_royale::simulation

#endif
