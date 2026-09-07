#ifndef BLOB_ROYALE_SIMULATION_KIND_REGISTRY_HPP
#define BLOB_ROYALE_SIMULATION_KIND_REGISTRY_HPP

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

namespace blob_royale::simulation {

// canonical: kind_registry -- the closed list of kinds one closed variant declares.
//
// A kind registry is a `std::variant` of value structs plus a `KindOf` trait specialized beside
// each alternative. **The list of kinds and every check over it are derived from those two, never
// hand-typed beside them.** A hand-typed parallel array guarded only by a size check accepts a
// duplicated entry, which silently drops a kind from `CommandKindMask::all()` and from every
// diagnostic that enumerates kinds; and a hand-written pairwise injectivity assertion needs ten
// comparisons at five kinds and is wrong the first time one is forgotten (engine review
// finding 7).
//
// Two implementations of this: `kCommandKinds` in command_registry.hpp and `kWorldEventKinds` in
// world_event_registry.hpp.
// related: component_list.hpp -- the same "generate, never maintain" rule for component kinds.

// The kinds one closed variant declares, in alternative order, read through `KindOf`.
//
// An alternative that forgot its `KindOf` specialization fails to compile here rather than
// dropping out of the list, because the primary template of every such trait is declared and never
// defined. An alternative that copied a neighbour's enumerator is caught by `values_are_distinct`.
template <typename Variant, template <typename> class KindOf>
[[nodiscard]] constexpr auto kinds_of_variant() noexcept {
  using Kind = std::remove_cv_t<decltype(KindOf<std::variant_alternative_t<0, Variant>>::value)>;
  return []<std::size_t... Indices>(std::index_sequence<Indices...>) {
    return std::array<Kind, sizeof...(Indices)>{
        KindOf<std::variant_alternative_t<Indices, Variant>>::value...};
  }(std::make_index_sequence<std::variant_size_v<Variant>>{});
}

// Whether every element of a derived list is distinct, which is the injectivity every kind
// registry needs: one enumerator per alternative, and one application rank per kind. Quadratic and
// evaluated at compile time over a handful of kinds, so the cost is a compile-time constant and
// the count of comparisons is generated rather than written out.
template <typename Value, std::size_t Count>
[[nodiscard]] constexpr bool values_are_distinct(const std::array<Value, Count>& values) noexcept {
  for (std::size_t left = 0; left + 1 < Count; ++left) {
    for (std::size_t right = left + 1; right < Count; ++right) {
      if (values[left] == values[right]) {
        return false;
      }
    }
  }
  return true;
}

// One per-kind function evaluated over a derived kind list, so an injectivity check over that
// function is `values_are_distinct(projected_values(kinds, function))` rather than a hand-written
// pairwise chain that a new kind silently outgrows.
template <typename Value, std::size_t Count, typename Projection>
[[nodiscard]] constexpr auto projected_values(const std::array<Value, Count>& values,
                                              Projection projection) noexcept {
  using Projected = std::remove_cv_t<decltype(projection(values[0]))>;
  std::array<Projected, Count> projected{};
  for (std::size_t index = 0; index < Count; ++index) {
    projected[index] = projection(values[index]);
  }
  return projected;
}

} // namespace blob_royale::simulation

#endif
