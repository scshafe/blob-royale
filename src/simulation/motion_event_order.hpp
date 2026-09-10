#ifndef BLOB_ROYALE_SIMULATION_MOTION_EVENT_ORDER_HPP
#define BLOB_ROYALE_SIMULATION_MOTION_EVENT_ORDER_HPP

#include "candidate_pair.hpp"
#include "entity_id.hpp"
#include "simulation_validation_error.hpp"

#include <cmath>
#include <compare>
#include <cstdint>

namespace blob_royale::simulation {

// canonical: motion_event_time -- one normalized parameter on a linear motion segment.
//
// Root rounding contract: every operation in swept_geometry.cpp uses binary64 in its written
// order, under round-to-nearest/ties-to-even and -ffp-contract=off. Its exact-sign expansion uses
// explicit std::fma solely to recover product rounding residuals; no implicit contraction occurs.
// Retain the resulting binary64
// root exactly when it is in [0, 1], and canonicalize -0 to +0. There is no quantization, epsilon
// equality, or endpoint snapping. A root one representable value outside the interval is outside.
// Equality means equal stored roots, not an inferred equality of their exact mathematical times.
// Consumers must use those same roots; any revision belongs to this contract, never a comparator.
class MotionTime final {
public:
  [[nodiscard]] static MotionTime create(const double value) {
    if (!std::isfinite(value)) {
      throw SimulationValidationError(SimulationValidationCode::kPhysicalScalarNotFinite,
                                      "motion_time", "motion time must be finite");
    }
    if (value < 0.0 || value > 1.0) {
      throw SimulationValidationError(SimulationValidationCode::kPhysicalScalarOutOfRange,
                                      "motion_time", "motion time must lie in [0, 1]");
    }
    return MotionTime{value == 0.0 ? 0.0 : value};
  }

  [[nodiscard]] static constexpr MotionTime start() noexcept { return MotionTime{0.0}; }
  [[nodiscard]] static constexpr MotionTime end() noexcept { return MotionTime{1.0}; }
  [[nodiscard]] constexpr double value() const noexcept { return value_; }

  friend bool operator==(const MotionTime&, const MotionTime&) = default;
  friend constexpr std::strong_ordering operator<=>(const MotionTime first,
                                                    const MotionTime second) noexcept {
    if (first.value_ < second.value_) {
      return std::strong_ordering::less;
    }
    if (first.value_ > second.value_) {
      return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
  }

private:
  explicit constexpr MotionTime(const double value) noexcept : value_(value) {}
  double value_;
};

// canonical: motion_event_priority -- ADR 0008's chronology, including x-before-y wall ties.
// Contact consequences (including lethal termination) commit before checkpoint credit. Body
// contacts precede walls at equal times, retaining the accepted pair-before-wall precedence.
enum class MotionEventPriority : std::uint8_t {
  kSupportLoss = 0,
  kBodyContact = 1,
  kWallX = 2,
  kWallY = 3,
  kCheckpoint = 4,
};

// canonical: motion_event_order -- lexicographic time, priority, then canonical identity.
//
// Pair identity is (lower EntityId, higher EntityId). A boundary/trigger identity is (body
// EntityId, authored feature index): indices are stable declaration indices, never discovery,
// pointer, or insertion order. Wall feature indices are 0 for lower and 1 for upper. Equal keys
// identify the same event and must not acquire another ordering from storage or discovery order.
class MotionEventKey final {
public:
  [[nodiscard]] static MotionEventKey contact(const MotionTime time,
                                              const CandidatePair pair) noexcept {
    return MotionEventKey{time, MotionEventPriority::kBodyContact, pair.lower_id(),
                          pair.higher_id().value()};
  }

  [[nodiscard]] static MotionEventKey boundary(const MotionTime time,
                                               const MotionEventPriority priority,
                                               const EntityId entity,
                                               const std::uint64_t feature_index) {
    switch (priority) {
    case MotionEventPriority::kSupportLoss:
    case MotionEventPriority::kWallX:
    case MotionEventPriority::kWallY:
    case MotionEventPriority::kCheckpoint:
      return MotionEventKey{time, priority, entity, feature_index};
    case MotionEventPriority::kBodyContact:
      break;
    }
    throw SimulationValidationError(SimulationValidationCode::kPhysicalScalarOutOfRange,
                                    "motion_event.priority",
                                    "boundary requires a declared non-contact priority");
  }

  [[nodiscard]] MotionTime time() const noexcept { return time_; }
  [[nodiscard]] MotionEventPriority priority() const noexcept { return priority_; }
  [[nodiscard]] EntityId first_entity() const noexcept { return first_entity_; }
  [[nodiscard]] std::uint64_t second_identity() const noexcept { return second_identity_; }

  friend bool operator==(const MotionEventKey&, const MotionEventKey&) = default;
  friend std::strong_ordering operator<=>(const MotionEventKey&, const MotionEventKey&) = default;

private:
  MotionEventKey(const MotionTime time, const MotionEventPriority priority,
                 const EntityId first_entity, const std::uint64_t second_identity) noexcept
      : time_(time), priority_(priority), first_entity_(first_entity),
        second_identity_(second_identity) {}

  MotionTime time_;
  MotionEventPriority priority_;
  EntityId first_entity_;
  std::uint64_t second_identity_;
};

} // namespace blob_royale::simulation

#endif
