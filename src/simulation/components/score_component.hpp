#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_SCORE_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_SCORE_COMPONENT_HPP

#include "component_kind_name.hpp"

#include <cstdint>
#include <string_view>

namespace blob_royale::simulation {

// canonical: score_component -- one integer scoreboard cell.
//
// The cell sits on whichever entity owns the score: a player entity in a free-for-all, a team
// entity in a team mode. Points are signed so a penalty is expressible without a second kind.
struct Score final {
  std::int64_t points{};

  friend bool operator==(const Score&, const Score&) = default;
};

template <> struct ComponentKindName<Score> {
  static constexpr std::string_view value = "score";
};

} // namespace blob_royale::simulation

#endif
