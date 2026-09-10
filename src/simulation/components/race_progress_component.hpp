#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_RACE_PROGRESS_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_RACE_PROGRESS_COMPONENT_HPP

#include "component_kind_name.hpp"

#include <cstdint>
#include <string_view>

namespace blob_royale::simulation {

// canonical: race_progress_component -- the next ordered gate one racer must reach.
//
// `checkpoint_progress` attaches {0} to every alive racer that lacks progress while running and
// advances at most one gate per tick. The gate count means finished. Progress survives the loss
// of a body so `checkpoint_respawn` can return the racer to the last gate; match reset erases it
// with the racer. Its presence also distinguishes a returning racer from a fresh mid-race joiner.
// related: ../../gameplay/race/checkpoint_progress_system.hpp -- the only progress writer.
// related: docs/architecture/0007-king-of-the-hill-and-race-modes.md section "Progress and
// finishing".
struct RaceProgress final {
  // Zero-based next gate index; the course's gate count means all gates have been taken.
  std::uint64_t next_checkpoint{};

  friend bool operator==(const RaceProgress&, const RaceProgress&) = default;
};

template <> struct ComponentKindName<RaceProgress> {
  static constexpr std::string_view value = "race_progress";
};

} // namespace blob_royale::simulation

#endif
