#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_LIFETIME_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_LIFETIME_COMPONENT_HPP

#include "component_kind_name.hpp"

#include <cstdint>
#include <string_view>

namespace blob_royale::simulation {

// canonical: lifetime_component -- the remaining tick count of a self-expiring entity.
//
// Duration is an integer tick count rather than a time value, because blob_simulation names no
// clock type; a wall-clock duration is converted once at configuration load.
struct Lifetime final {
  std::uint64_t ticks_remaining{};

  friend bool operator==(const Lifetime&, const Lifetime&) = default;
};

template <> struct ComponentKindName<Lifetime> {
  static constexpr std::string_view value = "lifetime";
};

} // namespace blob_royale::simulation

#endif
