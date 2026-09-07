#ifndef BLOB_ROYALE_SIMULATION_COMPONENT_LIST_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENT_LIST_HPP

#include "component_store.hpp"

#include <cstddef>
#include <tuple>

namespace blob_royale::simulation {

// canonical: component_list -- the type-list vocabulary one component registry is written in.
//
// Because a registry is a type list rather than a hand-maintained table, every behavior that must
// visit all kinds is generated from it: structural world equality, entity destruction across every
// store, and snapshot construction. Adding a kind therefore cannot forget to participate in any of
// the three places a hand-written registry would rot.
// related: component_registry.hpp -- the closed list this template is instantiated with.
template <typename... Components> struct ComponentList final {
  static constexpr std::size_t kKindCount = sizeof...(Components);

  // One store per kind, in declared order.
  using Stores = std::tuple<ComponentStore<Components>...>;

  // Invokes `visitor.template operator()<Component>()` once per registered kind, in declared
  // order. This is the only way generic code enumerates the registry, so declared order is the
  // single source of visitation order and no caller can reorder or omit a kind. The visitor is
  // bound once and invoked repeatedly rather than forwarded per kind, so a stateful visitor is
  // never moved from between kinds.
  template <typename Visitor> static constexpr void for_each_kind(Visitor&& visitor) {
    Visitor& bound_visitor = visitor;
    (bound_visitor.template operator()<Components>(), ...);
  }
};

// The complete component storage of one world: `std::tuple<ComponentStore<C>...>` over a list.
template <typename List> using ComponentStores = typename List::Stores;

} // namespace blob_royale::simulation

#endif
