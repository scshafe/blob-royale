#ifndef BLOB_ROYALE_TESTS_UNIT_SIMULATION_SIMULATION_TEST_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_SIMULATION_SIMULATION_TEST_FIXTURE_HPP

#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/lifetime_component.hpp"
#include "components/score_component.hpp"
#include "entity_id.hpp"
#include "events/contact_event.hpp"
#include "game_world.hpp"
#include "physics_body.hpp"
#include "simulation_system.hpp"
#include "system_pipeline.hpp"
#include "tick_context.hpp"
#include "world_event_registry.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::testing {

namespace simulation = blob_royale::simulation;

// canonical: simulation_system_test_double -- the named systems every staged-kernel test declares.
//
// Each one obeys the interface's own rule: `apply` is const, the system holds nothing but the
// immutable configuration it was constructed with, and every value it changes is world-owned. That
// is why what a system observed is read back out of the committed snapshot rather than out of a
// captured buffer -- a probe that wrote to test-owned storage would be exactly the impurity the
// interface exists to prevent.

// Writes the position it observed into each entity's Score cell, rounded to whole world units. No
// accepted phase reads Score, so the probe records a stage's view without changing the tick.
class PositionProbeSystem final : public simulation::SimulationSystem {
public:
  explicit PositionProbeSystem(const std::string_view system_name) noexcept
      : system_name_(system_name) {}

  [[nodiscard]] std::string_view name() const noexcept override { return system_name_; }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    for (const simulation::ComponentStore<simulation::PhysicsBody>::Entry& entry :
         world.store<simulation::PhysicsBody>().entries()) {
      world.mutable_store<simulation::Score>().insert_or_assign(
          entry.entity,
          simulation::Score{static_cast<std::int64_t>(std::llround(entry.value.position().x()))});
    }
  }

private:
  std::string_view system_name_;
};

// Appends one decimal digit to a world-owned trail, so the exact sequence in which stages and
// systems ran is a number in the committed snapshot. Lifetime is the trail because it is a
// registered kind no accepted phase reads.
class OrderTrailSystem final : public simulation::SimulationSystem {
public:
  OrderTrailSystem(const std::string_view system_name, const std::uint64_t marker) noexcept
      : system_name_(system_name), marker_(marker) {}

  [[nodiscard]] std::string_view name() const noexcept override { return system_name_; }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    for (const simulation::EntityId entity : world.entities()) {
      const simulation::Lifetime* recorded = world.store<simulation::Lifetime>().find(entity);
      const std::uint64_t trail = recorded == nullptr ? 0 : recorded->ticks_remaining;
      world.mutable_store<simulation::Lifetime>().insert_or_assign(
          entity, simulation::Lifetime{(trail * 10) + marker_});
    }
  }

private:
  std::string_view system_name_;
  std::uint64_t marker_;
};

// Emits one declared event per tick, in ascending EntityId order over the entities it names.
class EventEmittingSystem final : public simulation::SimulationSystem {
public:
  EventEmittingSystem(const std::string_view system_name,
                      std::vector<simulation::WorldEvent> events)
      : system_name_(system_name), events_(std::move(events)) {}

  [[nodiscard]] std::string_view name() const noexcept override { return system_name_; }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    for (const simulation::WorldEvent& event : events_) {
      world.emit(event);
    }
  }

private:
  std::string_view system_name_;
  std::vector<simulation::WorldEvent> events_;
};

// Writes the number of events it observed into every entity's Score cell, which is how a later
// stage's view of an earlier stage's events becomes readable from the committed snapshot.
class EventCountProbeSystem final : public simulation::SimulationSystem {
public:
  explicit EventCountProbeSystem(const std::string_view system_name) noexcept
      : system_name_(system_name) {}

  [[nodiscard]] std::string_view name() const noexcept override { return system_name_; }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    const auto observed_event_count = static_cast<std::int64_t>(world.events().size());
    for (const simulation::EntityId entity : world.entities()) {
      world.mutable_store<simulation::Score>().insert_or_assign(
          entity, simulation::Score{observed_event_count});
    }
  }

private:
  std::string_view system_name_;
};

// Writes the number of commands phase 0 recorded for each entity into that entity's Score cell.
class RecordedCommandCountProbeSystem final : public simulation::SimulationSystem {
public:
  explicit RecordedCommandCountProbeSystem(const std::string_view system_name) noexcept
      : system_name_(system_name) {}

  [[nodiscard]] std::string_view name() const noexcept override { return system_name_; }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    for (const simulation::ComponentStore<simulation::Controllable>::Entry& entry :
         world.store<simulation::Controllable>().entries()) {
      world.mutable_store<simulation::Score>().insert_or_assign(
          entry.entity,
          simulation::Score{static_cast<std::int64_t>(entry.value.commands_this_tick.size())});
    }
  }

private:
  std::string_view system_name_;
};

// Records the tick sequence the context published, so "the sequence this tick commits" is
// observable from the committed snapshot rather than only from the context's own accessor.
class TickSequenceProbeSystem final : public simulation::SimulationSystem {
public:
  explicit TickSequenceProbeSystem(const std::string_view system_name) noexcept
      : system_name_(system_name) {}

  [[nodiscard]] std::string_view name() const noexcept override { return system_name_; }

  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override {
    for (const simulation::EntityId entity : world.entities()) {
      world.mutable_store<simulation::Lifetime>().insert_or_assign(
          entity, simulation::Lifetime{context.tick_sequence().value()});
    }
  }

private:
  std::string_view system_name_;
};

// Writes 1 into every entity's Score when this tick produced a ContactEvent naming the declared
// rule and 0 otherwise, which is how a tick-local contact event becomes observable from the
// committed snapshot after the commit has cleared the list.
class ContactRuleProbeSystem final : public simulation::SimulationSystem {
public:
  ContactRuleProbeSystem(const std::string_view system_name,
                         const std::string_view probed_rule_name) noexcept
      : system_name_(system_name), probed_rule_name_(probed_rule_name) {}

  [[nodiscard]] std::string_view name() const noexcept override { return system_name_; }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    std::int64_t matched = 0;
    for (const simulation::WorldEvent& event : world.events()) {
      const auto* contact = std::get_if<simulation::ContactEvent>(&event);
      if (contact != nullptr && contact->rule_name == probed_rule_name_) {
        ++matched;
      }
    }
    for (const simulation::EntityId entity : world.entities()) {
      world.mutable_store<simulation::Score>().insert_or_assign(entity, simulation::Score{matched});
    }
  }

private:
  std::string_view system_name_;
  std::string_view probed_rule_name_;
};

// Creates one entity carrying one body, once, at a declared stage. This is the in-contract way a
// mode brings a projectile, a zone, or a dropped flag into the world, and it is what the spatial
// index has to notice: the index is a function of the body store, not of the entity roster.
class BodyCreatingSystem final : public simulation::SimulationSystem {
public:
  BodyCreatingSystem(const std::string_view system_name, const simulation::EntityId created,
                     const simulation::PhysicsBody body) noexcept
      : system_name_(system_name), created_(created), body_(body) {}

  [[nodiscard]] std::string_view name() const noexcept override { return system_name_; }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    if (world.contains(created_)) {
      return;
    }
    world.create_entity(created_);
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(created_, body_);
  }

private:
  std::string_view system_name_;
  simulation::EntityId created_;
  simulation::PhysicsBody body_;
};

// Destroys one entity directly rather than by emitting a DespawnEvent. `destroy_entity` is public
// and is the obvious call a system author reaches for, so the committed index has to survive it.
class EntityDestroyingSystem final : public simulation::SimulationSystem {
public:
  EntityDestroyingSystem(const std::string_view system_name,
                         const simulation::EntityId destroyed) noexcept
      : system_name_(system_name), destroyed_(destroyed) {}

  [[nodiscard]] std::string_view name() const noexcept override { return system_name_; }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    world.destroy_entity(destroyed_);
  }

private:
  std::string_view system_name_;
  simulation::EntityId destroyed_;
};

// A system with a name and no effect, for the pipeline's own ordering and rejection tests.
class NamedNoOpSystem final : public simulation::SimulationSystem {
public:
  explicit NamedNoOpSystem(const std::string_view system_name) noexcept
      : system_name_(system_name) {}

  [[nodiscard]] std::string_view name() const noexcept override { return system_name_; }

  void apply(simulation::GameWorld&, const simulation::TickContext&) const override {}

private:
  std::string_view system_name_;
};

[[nodiscard]] inline simulation::SystemPipeline::StagedSystem
staged(const simulation::SystemStage stage,
       std::unique_ptr<const simulation::SimulationSystem> system) {
  return simulation::SystemPipeline::StagedSystem{stage, std::move(system)};
}

[[nodiscard]] inline simulation::SystemPipeline::StagedSystem
staged_no_op(const simulation::SystemStage stage, const std::string_view system_name) {
  return staged(stage, std::make_unique<const NamedNoOpSystem>(system_name));
}

} // namespace blob_royale::testing

#endif
