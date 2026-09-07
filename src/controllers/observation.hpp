#ifndef BLOB_ROYALE_CONTROLLERS_OBSERVATION_HPP
#define BLOB_ROYALE_CONTROLLERS_OBSERVATION_HPP

#include "controller_id.hpp"
#include "entity_id.hpp"
#include "tick_sequence.hpp"
#include "world_snapshot.hpp"

#include <memory>
#include <optional>

namespace blob_royale::controllers {

// canonical: observation -- everything a controller may see, and the only thing it may see.
//
// One published `WorldSnapshot`, the controller's own durable identity, and the body that identity
// is currently driving. That is the whole surface, and the bound is the point: it is exactly what a
// networked player's session can read (`const SnapshotPublication&` plus its own `welcome`), so a
// bot cannot observe anything a human could not
// (`docs/architecture/0004-gameplay-architecture.md` § "Controllers";
// `tests/unit/controllers/human_bot_symmetry_tests.cpp` asserts it rather than assuming it).
//
// **It is built from a `ControllerId` and resolves the `EntityId` itself, which is where this
// deviates from the ADR.** ADR 0004 § "Controllers" writes
// `Observation::create(snapshot, EntityId entity)`, and it was written before plan Step 22 settled
// that a command source's durable handle is a `ControllerId`: the engine chooses an `EntityId`
// inside `step` from the tick's reservation, royale destroys a player's body on elimination and
// again on the lobby wipe, and a respawn is a **new** `EntityId` under the same `ControllerId`
// (`src/runtime/command_sink.hpp`; `src/simulation/controller_id.hpp`). An observation keyed on an
// `EntityId` would therefore have to be re-keyed by some caller on every respawn, and that caller
// would need the world lookup this class already performs. Keying on the durable identity and
// resolving the body per snapshot is how a controller **finds itself across a respawn** without a
// lookup table anywhere.
//
// **`entity()` is a `std::optional` and its absence is a defined answer, never a precondition
// violation.** A controller whose spawn has not been seated yet, and one whose body has just been
// eliminated, both observe a world in which they carry no `Controllable`. ADR 0004 states that case
// explicitly: `decide` is still called and may return an empty vector or a spawn.
//
// **The resolution is this file's job and no one else's.** "Which body is this controller driving?"
// is one question with one implementation, here: it reads the published `Controllable` store, which
// is the only place the `EntityId` and `ControllerId` identity spaces meet and which the snapshot
// publishes for exactly this reason (`src/simulation/components/controllable_component.hpp`).
//
// **The snapshot is retained, not referenced.** `Observation` holds the `shared_ptr` the
// publication handed out, so an asynchronous controller may keep the observation across threads and
// across ticks and the world it decided against stays alive and unchanged underneath it. ADR 0004
// requires that: an LLM-driven or learned-policy bot computes off-thread and returns the latest
// decision it has.
// related: controller.hpp -- the role that consumes this.
// related: controller_host.hpp -- the one production builder of these values.
// related: ../simulation/components/controllable_component.hpp -- the link this resolves through.
class Observation final {
public:
  // Builds one controller's view of one published world.
  //
  // Throws ControllersValidationError with `CONTROLLERS.OBSERVATION_SNAPSHOT_ABSENT` for a null
  // snapshot: `SnapshotPublication::latest()` is non-null from construction onward, so a null here
  // is a caller defect and a controller handed an empty world would decide against a world that
  // does not exist.
  [[nodiscard]] static Observation create(std::shared_ptr<const simulation::WorldSnapshot> snapshot,
                                          simulation::ControllerId controller);

  Observation(const Observation&) = default;
  Observation(Observation&&) noexcept = default;
  Observation& operator=(const Observation&) = default;
  Observation& operator=(Observation&&) noexcept = default;
  ~Observation() = default;

  // The complete committed world this decision is made against. Deleted on an rvalue because the
  // snapshot is owned by this value and a reference into a temporary observation would dangle.
  [[nodiscard]] const simulation::WorldSnapshot& snapshot() const& noexcept { return *snapshot_; }
  [[nodiscard]] const simulation::WorldSnapshot& snapshot() const&& = delete;

  // The retained handle, for a controller that carries the world to another thread and decides
  // later. Sharing the handle is what keeps that world alive and unchanged for it.
  [[nodiscard]] const std::shared_ptr<const simulation::WorldSnapshot>&
  retained_snapshot() const& noexcept {
    return snapshot_;
  }
  [[nodiscard]] const std::shared_ptr<const simulation::WorldSnapshot>&
  retained_snapshot() const&& = delete;

  // The observing controller's durable identity: the value `CommandSink::open_session` issued and
  // the value every command this controller submits is stamped with.
  [[nodiscard]] simulation::ControllerId controller() const noexcept { return controller_; }

  // The body this controller drives in this snapshot, or `std::nullopt` when it drives none because
  // its spawn is still pending or its body was eliminated. Both are ordinary, defined states.
  [[nodiscard]] std::optional<simulation::EntityId> entity() const noexcept { return entity_; }

  // Whether this controller carries a live body in this snapshot. The named form of `entity()`
  // having a value, because "am I alive right now?" is the question every behavior actually asks.
  [[nodiscard]] bool has_live_entity() const noexcept { return entity_.has_value(); }

  [[nodiscard]] simulation::TickSequence tick_sequence() const noexcept {
    return snapshot_->tick_sequence();
  }

private:
  Observation(std::shared_ptr<const simulation::WorldSnapshot> snapshot,
              simulation::ControllerId controller,
              std::optional<simulation::EntityId> entity) noexcept;

  std::shared_ptr<const simulation::WorldSnapshot> snapshot_;
  simulation::ControllerId controller_;
  std::optional<simulation::EntityId> entity_;
};

} // namespace blob_royale::controllers

#endif
