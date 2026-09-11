#ifndef BLOB_ROYALE_SIMULATION_COMPONENT_LIFETIME_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENT_LIFETIME_HPP

namespace blob_royale::simulation {

// canonical: component_lifetime -- whether a kind belongs to the current physical body.
// @extension-point component_lifetime
//
// Persistent entity state is the default: scores, controller links, return timers, and objective
// progress can outlive a body. A body-bound kind specializes this trait beside its value struct,
// so adding it needs no cleanup-system edit. This header deliberately depends on neither the
// component registry nor GameWorld; component headers can include it without an ownership cycle.
// related: game_world.hpp -- the registry-generated bodyless cleanup operation.
// related: components/hill_presence_component.hpp -- partial progress belongs to one body.
template <typename Component> struct ComponentLifetime {
  static constexpr bool bound_to_body = false;
};

} // namespace blob_royale::simulation

#endif
