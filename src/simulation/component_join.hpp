#ifndef BLOB_ROYALE_SIMULATION_COMPONENT_JOIN_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENT_JOIN_HPP

#include "component_store.hpp"
#include "entity_id.hpp"

#include <cstddef>
#include <span>
#include <utility>

namespace blob_royale::simulation {

// canonical: component_join -- the one ordered merge of two ascending component stores.
//
// **This is the answer to "what walks two stores together?"** A player is an entity carrying both
// a PhysicsBody and a Controllable; a steering system reads a Controllable and writes the
// PhysicsBody beside it; royale's elimination reads a Zone and a body; the protocol v1 player
// projection reads a body and a controller link. ADR 0004 § "Entities, components, and stores"
// names the pattern -- "a system that needs two components performs an ordered merge of two
// ascending spans, not a hash lookup" -- and until this file every reader wrote its own copy of
// that merge (engine review finding 9).
//
// Both stores are strict ascending EntityId order by construction, so the merge is one forward
// pass over each, allocates nothing, and visits matches in ascending EntityId order. That order is
// the one `docs/architecture/0003-deterministic-simulation-contract.md` requires of every phase,
// so a system built on this join is ordered correctly without saying so.
//
// It is **read-only on purpose**. A store's values are mutable through `mutable_values()` and
// `mutable_find`, and which ids a store holds changes only through `insert_or_assign` and `erase`;
// a join that handed out mutable entries would be a second way to reach a component and would
// reintroduce exactly the key-rewriting hazard `mutable_values()` closes (engine review finding 8).
// A system that writes what it joined calls `insert_or_assign` with the id the visitor was given:
// the key already exists, so the assignment neither inserts nor moves an entry and the walk stays
// valid.
//
// **A published snapshot joins through the same one implementation.** `WorldSnapshot` owns copies
// of the stores and hands them out as `std::span<const ComponentStore<C>::Entry>` rather than as
// stores, so the store-taking form below was not callable on the value every snapshot reader
// actually holds -- the protocol encoder, an in-process bot's `Observation`, a fixture assertion --
// and each of them wrote the merge again. The span form is therefore the primitive and the store
// form delegates to it, so "ordered merge of two ascending spans" has exactly one loop no matter
// which of the two shapes a reader is holding.
// related: component_store.hpp -- the ascending storage this merges.
// related: world_snapshot.hpp -- the published spans the span form is for.
// related: entity_roster.hpp -- the successive ordered union of *every* store, which is a
//          different question: that one asks which entities exist, this one asks which carry both.

// Visits every entity carrying both kinds, in ascending EntityId order, as
// `visitor(EntityId, const First&, const Second&)`, over two ascending entry spans.
//
// This is the one merge. Both spans are strict ascending EntityId order by the construction of the
// store they came from -- a `ComponentStore::entries()` directly, or the copy of one a
// `WorldSnapshot` publishes -- so the pass allocates nothing and needs no comparator.
//
// Total: an entity carrying only one of the two kinds is skipped rather than visited with a
// missing half, and either span being empty visits nothing.
template <typename FirstEntry, typename SecondEntry, typename Visitor>
void for_each_entity_with_both(const std::span<const FirstEntry> first_entries,
                               const std::span<const SecondEntry> second_entries, Visitor visitor) {
  std::size_t first_index = 0;
  std::size_t second_index = 0;
  while (first_index < first_entries.size() && second_index < second_entries.size()) {
    const EntityId first_entity = first_entries[first_index].entity;
    const EntityId second_entity = second_entries[second_index].entity;
    if (first_entity < second_entity) {
      ++first_index;
      continue;
    }
    if (second_entity < first_entity) {
      ++second_index;
      continue;
    }
    visitor(first_entity, first_entries[first_index].value, second_entries[second_index].value);
    ++first_index;
    ++second_index;
  }
}

// The store-taking form, for a caller holding the world's own stores. It is the span form applied
// to `entries()`, so a change to the merge is a change in one place.
template <typename First, typename Second, typename Visitor>
void for_each_entity_with_both(const ComponentStore<First>& first_store,
                               const ComponentStore<Second>& second_store, Visitor visitor) {
  for_each_entity_with_both<typename ComponentStore<First>::Entry,
                            typename ComponentStore<Second>::Entry>(
      first_store.entries(), second_store.entries(), std::move(visitor));
}

// How many entities carry both kinds. It is the join's counting form because "how many players are
// alive" is asked by every objective and by the lifecycle tests, and counting through a visitor
// that increments a captured integer is the same loop written again at each call site.
template <typename FirstEntry, typename SecondEntry>
[[nodiscard]] std::size_t
count_entities_with_both(const std::span<const FirstEntry> first_entries,
                         const std::span<const SecondEntry> second_entries) {
  std::size_t matched = 0;
  for_each_entity_with_both<FirstEntry, SecondEntry>(
      first_entries, second_entries, [&matched](EntityId, const auto&, const auto&) { ++matched; });
  return matched;
}

template <typename First, typename Second>
[[nodiscard]] std::size_t count_entities_with_both(const ComponentStore<First>& first_store,
                                                   const ComponentStore<Second>& second_store) {
  return count_entities_with_both<typename ComponentStore<First>::Entry,
                                  typename ComponentStore<Second>::Entry>(first_store.entries(),
                                                                          second_store.entries());
}

} // namespace blob_royale::simulation

#endif
