#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_STUN_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_STUN_COMPONENT_HPP

#include "component_kind_name.hpp"
#include "component_lifetime.hpp"
#include "tick_window.hpp"

#include <string_view>

namespace blob_royale::simulation {

// canonical: stun_component -- body-bound input lock, published with both absolute endpoints.
struct Stun final {
  TickWindow window;

  friend bool operator==(const Stun&, const Stun&) = default;
};

template <> struct ComponentKindName<Stun> {
  static constexpr std::string_view value = "stun";
};

template <> struct ComponentLifetime<Stun> {
  static constexpr bool bound_to_body = true;
};

} // namespace blob_royale::simulation

#endif
