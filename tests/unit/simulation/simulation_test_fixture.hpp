#ifndef BLOB_ROYALE_TESTS_UNIT_SIMULATION_SIMULATION_TEST_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_SIMULATION_SIMULATION_TEST_FIXTURE_HPP

#include "command_kind_mask.hpp"
#include "command_registry.hpp"
#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "components/lifetime_component.hpp"
#include "components/score_component.hpp"
#include "contact_rule_table.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "events/contact_event.hpp"
#include "game_mode.hpp"
#include "game_world.hpp"
#include "map_definition.hpp"
#include "match_lifecycle_durations.hpp"
#include "match_objective.hpp"
#include "match_outcome.hpp"
#include "match_phase.hpp"
#include "physics_body.hpp"
#include "simulation_system.hpp"
#include "simulation_validation_error.hpp"
#include "spawn_policy.hpp"
#include "system_pipeline.hpp"
#include "tick_context.hpp"
#include "vector2.hpp"
#include "world_event_registry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
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

// Writes 1 into an entity's Score when the single command phase 0 recorded for it compares equal
// to the declared command, and 0 otherwise. This is how the *content* of a recorded command stays
// observable from a committed snapshot now that a publication strips the recorded list.
class RecordedCommandProbeSystem final : public simulation::SimulationSystem {
public:
  RecordedCommandProbeSystem(const std::string_view system_name, simulation::Command expected)
      : system_name_(system_name), expected_(std::move(expected)) {}

  [[nodiscard]] std::string_view name() const noexcept override { return system_name_; }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    for (const simulation::ComponentStore<simulation::Controllable>::Entry& entry :
         world.store<simulation::Controllable>().entries()) {
      const bool matched = entry.value.commands_this_tick.size() == 1 &&
                           entry.value.commands_this_tick.front() == expected_;
      world.mutable_store<simulation::Score>().insert_or_assign(entry.entity,
                                                                simulation::Score{matched ? 1 : 0});
    }
  }

private:
  std::string_view system_name_;
  simulation::Command expected_;
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

// Creates one entity carrying one body, once, at a declared stage, at an id the test chose. This
// is what the spatial index has to notice: the index is a function of the body store. Writing the
// component *is* the creation, because the roster is derived from the stores.
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
    world.mutable_store<simulation::PhysicsBody>().insert_or_assign(created_, body_);
  }

private:
  std::string_view system_name_;
  simulation::EntityId created_;
  simulation::PhysicsBody body_;
};

// Creates one entity from **this tick's EntityIdReservation** and gives it a body and a controller
// link, which is the in-contract way a mode brings a projectile, a pickup, or a zone entity into
// the world: the id is drawn from the tick's input value, so a replay reproduces it exactly. It
// records the id it drew into that entity's Lifetime cell, because a system may not write to
// test-owned storage.
class ReservedEntityCreatingSystem final : public simulation::SimulationSystem {
public:
  ReservedEntityCreatingSystem(const std::string_view system_name,
                               const simulation::PhysicsBody body,
                               const std::size_t creation_count = 1) noexcept
      : system_name_(system_name), body_(body), creation_count_(creation_count) {}

  [[nodiscard]] std::string_view name() const noexcept override { return system_name_; }

  void apply(simulation::GameWorld& world, const simulation::TickContext&) const override {
    for (std::size_t created = 0; created < creation_count_; ++created) {
      const simulation::EntityId entity = world.create_entity();
      world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity, body_);
      world.mutable_store<simulation::Controllable>().insert_or_assign(
          entity, simulation::Controllable{simulation::ControllerId::create(entity.value())});
      world.mutable_store<simulation::Lifetime>().insert_or_assign(
          entity, simulation::Lifetime{entity.value()});
    }
  }

private:
  std::string_view system_name_;
  simulation::PhysicsBody body_;
  std::size_t creation_count_;
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

// canonical: test_static_body_seating -- how a kernel test places a wall at an id it chose.
//
// Production static content is seated by `GameWorld::create(configuration, map, seed)`, which owns
// the id policy and numbers a map's bodies in declared order. A kernel test needs a *chosen* id --
// "the dynamic body is 1 and the wall is 2" is what an orientation assertion is about -- which a
// map's declared order cannot express. Writing the one component a wall carries is exactly what
// brings the entity into existence now that the roster is derived from the stores, and a wall
// carries no Controllable because nothing drives it.
inline void seat_static_body(simulation::GameWorld& world, const simulation::EntityId entity,
                             const simulation::PhysicsBody body) {
  world.mutable_store<simulation::PhysicsBody>().insert_or_assign(entity, body);
}

// The number of entities carrying both a PhysicsBody and a Controllable, which is what "alive"
// means to every objective and what a client counts from the entities it renders.
[[nodiscard]] inline std::size_t alive_count(const simulation::GameWorld& world) {
  std::size_t alive = 0;
  for (const simulation::ComponentStore<simulation::Controllable>::Entry& entry :
       world.store<simulation::Controllable>().entries()) {
    if (world.store<simulation::PhysicsBody>().find(entry.entity) != nullptr) {
      ++alive;
    }
  }
  return alive;
}

// canonical: test_match_objective -- the in-test objective, an alive-count match.
//
// A match starts when at least `minimum_players` entities are alive and is decided when at most one
// is: `won_by_entity` naming the survivor at one, `drawn` at zero. That is the smallest objective
// that reaches all five transitions of the engine's machine, and it is deliberately the same shape
// royale declares in Step 21 so the machine is exercised the way a real mode will use it.
class TestMatchObjective final : public simulation::MatchObjective {
public:
  TestMatchObjective(const std::size_t minimum_players,
                     const simulation::MatchLifecycleDurations durations) noexcept
      : minimum_players_(minimum_players), durations_(durations) {}

  [[nodiscard]] bool can_start(const simulation::GameWorld& world) const override {
    return alive_count(world) >= minimum_players_;
  }

  [[nodiscard]] simulation::MatchOutcome outcome(const simulation::GameWorld& world,
                                                 const simulation::TickContext&) const override {
    const std::size_t alive = alive_count(world);
    if (alive > 1) {
      return simulation::MatchOutcome::undecided();
    }
    if (alive == 0) {
      return simulation::MatchOutcome::drawn();
    }
    for (const simulation::ComponentStore<simulation::Controllable>::Entry& entry :
         world.store<simulation::Controllable>().entries()) {
      if (world.store<simulation::PhysicsBody>().find(entry.entity) != nullptr) {
        return simulation::MatchOutcome::won_by_entity(entry.entity);
      }
    }
    return simulation::MatchOutcome::undecided();
  }

  [[nodiscard]] simulation::MatchLifecycleDurations durations() const noexcept override {
    return durations_;
  }

private:
  std::size_t minimum_players_;
  simulation::MatchLifecycleDurations durations_;
};

// canonical: test_spawn_policy -- the in-test policy, the first free point in declared order.
//
// It probes forward from the rotation counter the SpawnSystem offers, so the counter's advance is
// observable, and it defers when every point is taken. Optionally it defers in every phase but the
// two the test names, which is how "royale holds joiners pending during a match" is exercised
// without royale existing.
class TestSpawnPolicy final : public simulation::SpawnPolicy {
public:
  TestSpawnPolicy() = default;
  explicit TestSpawnPolicy(std::vector<simulation::MatchPhase> seating_phases) noexcept
      : seating_phases_(std::move(seating_phases)) {}

  [[nodiscard]] std::optional<std::size_t>
  choose_spawn_point(const simulation::GameWorld& world, const simulation::TickContext&,
                     simulation::EntityId, const std::size_t rotation_counter,
                     const std::span<const bool> spawn_point_is_free) const override {
    if (!seating_phases_.empty() && std::find(seating_phases_.cbegin(), seating_phases_.cend(),
                                              world.match().phase) == seating_phases_.cend()) {
      return std::nullopt;
    }
    const std::size_t point_count = spawn_point_is_free.size();
    for (std::size_t probe = 0; probe < point_count; ++probe) {
      const std::size_t index = (rotation_counter + probe) % point_count;
      if (spawn_point_is_free[index]) {
        return index;
      }
    }
    return std::nullopt;
  }

private:
  std::vector<simulation::MatchPhase> seating_phases_;
};

// canonical: test_game_mode -- the minimal GameMode every lifecycle and seating test declares.
//
// It is the second implementation of the `game_mode` seam alongside the engine's own idle
// declarations, and it is what proves a mode needs nothing from the kernel but the seven
// declarations. `systems()` is single-use, which matches the contract exactly: the engine reads
// each declaration once, at construction.
class TestGameMode final : public simulation::GameMode {
public:
  struct Declaration final {
    std::string name{"test_mode"};
    std::vector<simulation::SystemPipeline::StagedSystem> systems;
    std::size_t minimum_players{1};
    simulation::MatchLifecycleDurations durations{};
    std::size_t required_spawn_point_count{0};
    std::vector<simulation::MatchPhase> seating_phases;
  };

  explicit TestGameMode(Declaration declaration) : declaration_(std::move(declaration)) {}

  [[nodiscard]] static std::unique_ptr<const simulation::GameMode> create(Declaration declaration) {
    return std::make_unique<const TestGameMode>(std::move(declaration));
  }

  [[nodiscard]] std::string_view name() const noexcept override { return declaration_.name; }

  [[nodiscard]] simulation::SystemPipeline systems() const override {
    return simulation::SystemPipeline::create(std::move(declaration_.systems));
  }

  [[nodiscard]] simulation::ContactRuleTable contact_rules() const override {
    return simulation::ContactRuleTable::built_in();
  }

  [[nodiscard]] simulation::CommandKindMask accepted_command_kinds() const noexcept override {
    return simulation::CommandKindMask::all();
  }

  [[nodiscard]] std::unique_ptr<const simulation::SpawnPolicy> spawn_policy() const override {
    return std::make_unique<const TestSpawnPolicy>(declaration_.seating_phases);
  }

  [[nodiscard]] std::unique_ptr<const simulation::MatchObjective> objective() const override {
    return std::make_unique<const TestMatchObjective>(declaration_.minimum_players,
                                                      declaration_.durations);
  }

  void validate_map(const simulation::MapDefinition& map) const override {
    if (map.spawn_points().size() >= declaration_.required_spawn_point_count) {
      return;
    }
    throw simulation::SimulationValidationError(
        simulation::SimulationValidationCode::kMapMarkerLimitExceeded,
        "test_game_mode.validate_map.spawn_points",
        "this mode requires at least " + std::to_string(declaration_.required_spawn_point_count) +
            " spawn points");
  }

private:
  // Mutable because `systems()` is a `const` declaration that hands over ownership of the declared
  // list, and the engine calls it exactly once.
  mutable Declaration declaration_;
};

// A map with `point_count` unaligned spawn points spaced far enough apart that every one of them
// is simultaneously seatable at the given radius, which is what a seating test needs to observe
// rotation rather than occupancy.
[[nodiscard]] inline simulation::MapDefinition spawn_point_map(const std::size_t point_count,
                                                               const double width = 500.0,
                                                               const double height = 500.0,
                                                               const double spacing = 50.0) {
  std::vector<simulation::MapDefinition::Marker> markers;
  markers.reserve(point_count);
  for (std::size_t index = 0; index < point_count; ++index) {
    markers.push_back(simulation::MapDefinition::Marker::spawn(
        simulation::Vector2::create(spacing * static_cast<double>(index + 1), spacing)));
  }
  return simulation::MapDefinition::create("spawn_point_map",
                                           simulation::ArenaBounds::create(width, height), {},
                                           std::move(markers), simulation::MapMetadata::none());
}

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
