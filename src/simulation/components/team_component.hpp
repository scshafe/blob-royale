#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_TEAM_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_TEAM_COMPONENT_HPP

#include "component_kind_name.hpp"
#include "team_id.hpp"

#include <string_view>

namespace blob_royale::simulation {

// canonical: team_component -- optional side membership for one entity.
//
// Absence is the unaligned case, so a free-for-all mode simply never attaches this kind.
struct Team final {
  TeamId team_id;

  friend bool operator==(const Team&, const Team&) = default;
};

template <> struct ComponentKindName<Team> {
  static constexpr std::string_view value = "team";
};

} // namespace blob_royale::simulation

#endif
