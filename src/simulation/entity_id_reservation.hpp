#ifndef BLOB_ROYALE_SIMULATION_ENTITY_ID_RESERVATION_HPP
#define BLOB_ROYALE_SIMULATION_ENTITY_ID_RESERVATION_HPP

#include "entity_id.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <cstdint>
#include <string>

namespace blob_royale::simulation {

// canonical: entity_id_reservation -- the contiguous ids one tick may bring into existence.
//
// One monotonic allocator outside the simulation issues EntityId values and places a contiguous
// half-open block `[first, first + count)` in each tick's InputBatch. Every id the tick creates --
// a session join and any entity a system creates -- is drawn from that block and from nowhere
// else, which is what makes replaying a recorded command log reproduce simulation-created ids
// exactly (`docs/architecture/0003-deterministic-simulation-contract.md`
// § "Accepted simulation input").
//
// **Exhaustion is a hard failure.** `draw_next` on an empty reservation throws rather than
// wrapping, reusing a live id, or returning a sentinel: a reused id would silently graft one
// entity's components onto another, which is exactly the class of corruption a hard failure
// exists to prevent. It is an engine-internal invariant violation rather than a client
// disagreement, so it is not one of the world-state disagreements a tick ignores.
//
// The block is stored as `first` plus `count` rather than as two ids because the exclusive end of
// a block ending at kMaximumEntityId is not itself a representable EntityId. Every exhausted
// reservation compares equal to `none()`, so "no ids left" is one value rather than a family.
//
// `draw_next` advances the reservation it is called on, and a tick's InputBatch hands out its
// reservation **by value**: the world draws from its own copy, so the batch keeps describing the
// unchanging input the tick was given.
// related: input_batch.hpp -- the value that carries one tick's reservation.
class EntityIdReservation final {
public:
  // Creates the block `[first, first + count)`. Throws SimulationValidationError when the block
  // would run past kMaximumEntityId or would exceed the world's seat count, so an impossible
  // reservation never reaches a tick.
  [[nodiscard]] static EntityIdReservation create(const EntityId first, const std::uint64_t count) {
    if (count > kMaximumEntityIdReservationCount) {
      throw SimulationValidationError(SimulationValidationCode::kEntityIdReservationLimitExceeded,
                                      "entity_id_reservation.count",
                                      "reservation count " + std::to_string(count) +
                                          " exceeds the accepted limit " +
                                          std::to_string(kMaximumEntityIdReservationCount));
    }
    if (count == 0) {
      return none();
    }
    if (first.value() > kMaximumEntityId - (count - 1)) {
      throw SimulationValidationError(
          SimulationValidationCode::kEntityIdReservationOutOfRange, "entity_id_reservation.first",
          "reservation of " + std::to_string(count) + " ids from EntityId " +
              std::to_string(first.value()) + " runs past the maximum EntityId");
    }
    return EntityIdReservation(first, count);
  }

  // The empty reservation: the value a tick that may create no entity carries. A draw from it is
  // the hard failure above, which is what makes "this tick creates nothing" a stated input rather
  // than an assumption.
  [[nodiscard]] static EntityIdReservation none() {
    return EntityIdReservation(EntityId::create(kMinimumEntityId), 0);
  }

  EntityIdReservation(const EntityIdReservation&) = default;
  EntityIdReservation(EntityIdReservation&&) noexcept = default;
  EntityIdReservation& operator=(const EntityIdReservation&) = default;
  EntityIdReservation& operator=(EntityIdReservation&&) noexcept = default;
  ~EntityIdReservation() = default;

  // The lowest id the block would still issue. Meaningful only while count() is nonzero.
  [[nodiscard]] EntityId first_entity_id() const noexcept { return first_; }

  [[nodiscard]] std::uint64_t count() const noexcept { return count_; }

  // Exhaustion, tested without drawing.
  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }

  // Whether this block would issue the id, which is exactly "this id names no committed entity
  // and this tick may create it".
  [[nodiscard]] bool contains(const EntityId entity) const noexcept {
    return count_ != 0 && entity.value() >= first_.value() &&
           entity.value() - first_.value() < count_;
  }

  // Draws the lowest remaining id and advances past it. Throws SimulationValidationError when the
  // block is exhausted; it never wraps and never reissues a drawn id.
  [[nodiscard]] EntityId draw_next() {
    if (count_ == 0) {
      throw SimulationValidationError(
          SimulationValidationCode::kEntityIdReservationExhausted,
          "entity_id_reservation.draw_next",
          "the tick's EntityId reservation is exhausted; no id may be reused or wrapped");
    }
    const EntityId drawn = first_;
    if (count_ == 1) {
      *this = none();
      return drawn;
    }
    first_ = EntityId::create(drawn.value() + 1);
    --count_;
    return drawn;
  }

  friend bool operator==(const EntityIdReservation&, const EntityIdReservation&) = default;

private:
  EntityIdReservation(const EntityId first, const std::uint64_t count) noexcept
      : first_(first), count_(count) {}

  EntityId first_;
  std::uint64_t count_;
};

} // namespace blob_royale::simulation

#endif
