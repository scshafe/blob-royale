#ifndef BLOB_ROYALE_SIMULATION_COMPONENT_STORE_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENT_STORE_HPP

#include "entity_id.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace blob_royale::simulation {

// canonical: component_store -- the only component storage implementation.
//
// Components are stored per kind, never per entity. `entries()` is strict ascending EntityId order
// by construction, so every loop that walks a store is ascending-EntityId for free, which is the
// ordering `docs/architecture/0003-deterministic-simulation-contract.md` requires of every phase.
// A system that needs two kinds performs an ordered merge of two ascending spans, never a hash
// lookup, so no iteration order can depend on a container's hashing. That merge has one named
// implementation and is not re-written per reader (`component_join.hpp`).
// related: component_registry.hpp -- the closed list of kinds stored this way.
// related: component_join.hpp -- the one ordered merge of two of these.
template <typename Component> class ComponentStore final {
public:
  struct Entry final {
    EntityId entity;
    Component value;

    friend bool operator==(const Entry&, const Entry&) = default;
  };

  // Canonicalizes to strict ascending EntityId order; a duplicate id is a validation failure.
  [[nodiscard]] static ComponentStore create(std::vector<Entry> entries) {
    if (entries.size() > kMaximumEntityCount) {
      throw SimulationValidationError(SimulationValidationCode::kComponentStoreLimitExceeded,
                                      "component_store.entries",
                                      "component entry count " + std::to_string(entries.size()) +
                                          " exceeds the accepted limit");
    }

    std::sort(entries.begin(), entries.end(),
              [](const Entry& left, const Entry& right) { return left.entity < right.entity; });

    const auto duplicate = std::adjacent_find(
        entries.cbegin(), entries.cend(),
        [](const Entry& left, const Entry& right) { return left.entity == right.entity; });
    if (duplicate != entries.cend()) {
      throw SimulationValidationError(SimulationValidationCode::kComponentStoreDuplicateEntityId,
                                      "component_store.entries.entity_id",
                                      "duplicate EntityId " +
                                          std::to_string(duplicate->entity.value()));
    }

    return ComponentStore(std::move(entries));
  }

  // An empty store: the value every registered kind holds before any entity carries it.
  ComponentStore() = default;
  ComponentStore(const ComponentStore&) = default;
  ComponentStore(ComponentStore&&) noexcept = default;
  ComponentStore& operator=(const ComponentStore&) = default;
  ComponentStore& operator=(ComponentStore&&) noexcept = default;
  ~ComponentStore() = default;

  [[nodiscard]] std::span<const Entry> entries() const& noexcept { return entries_; }
  [[nodiscard]] std::span<const Entry> entries() const&& = delete;

  // Binary search over the ascending entries; nullptr when this entity does not carry the kind.
  [[nodiscard]] const Component* find(const EntityId entity) const& noexcept {
    const auto match = lower_bound(entries_, entity);
    if (match == entries_.cend() || match->entity != entity) {
      return nullptr;
    }
    return &match->value;
  }
  [[nodiscard]] const Component* find(EntityId entity) const&& = delete;

  // The mutable twins of `entries()` and `find`, so a phase or a system can rewrite a component in
  // place instead of copying it out and assigning it back.
  //
  // **Only the value half is mutable, and that is structural rather than a rule to remember.**
  // `mutable_values()` hands out `Component&` and no `EntityId` at all, so a caller cannot rewrite
  // the key that strict ascending order -- and therefore every phase's iteration order and every
  // binary search -- depends on (engine review finding 8). `insert_or_assign` and `erase` remain
  // the only operations that change which ids the store holds. The tick has only ever needed the
  // values: phase 0 clears each recorded command list and the snapshot rewrites each published
  // value, and both walk the whole store in the ascending order the keys already impose.
  [[nodiscard]] auto mutable_values() {
    return std::views::transform(entries_, [](Entry& entry) -> Component& { return entry.value; });
  }

  [[nodiscard]] Component* mutable_find(const EntityId entity) noexcept {
    const auto match = lower_bound(entries_, entity);
    if (match == entries_.cend() || match->entity != entity) {
      return nullptr;
    }
    return &entries_[static_cast<std::size_t>(match - entries_.cbegin())].value;
  }

  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }

  // Replaces this entity's component or inserts it at its ascending position.
  void insert_or_assign(const EntityId entity, Component value) {
    const auto match = lower_bound(entries_, entity);
    if (match != entries_.cend() && match->entity == entity) {
      entries_[static_cast<std::size_t>(match - entries_.cbegin())].value = std::move(value);
      return;
    }
    if (entries_.size() >= kMaximumEntityCount) {
      throw SimulationValidationError(SimulationValidationCode::kComponentStoreLimitExceeded,
                                      "component_store.entries",
                                      "component entry count exceeds the accepted limit");
    }
    entries_.insert(match, Entry{entity, std::move(value)});
  }

  // Removes this entity's component. An entity that does not carry the kind is unchanged, which
  // is what makes generated destruction across every registered store a total operation.
  void erase(const EntityId entity) noexcept {
    const auto match = lower_bound(entries_, entity);
    if (match == entries_.cend() || match->entity != entity) {
      return;
    }
    entries_.erase(match);
  }

  friend bool operator==(const ComponentStore&, const ComponentStore&) = default;

private:
  explicit ComponentStore(std::vector<Entry> entries) noexcept : entries_(std::move(entries)) {}

  [[nodiscard]] static typename std::vector<Entry>::const_iterator
  lower_bound(const std::vector<Entry>& entries, const EntityId entity) noexcept {
    return std::lower_bound(
        entries.cbegin(), entries.cend(), entity,
        [](const Entry& entry, const EntityId searched) { return entry.entity < searched; });
  }

  std::vector<Entry> entries_;
};

} // namespace blob_royale::simulation

#endif
