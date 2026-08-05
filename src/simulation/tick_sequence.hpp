#ifndef BLOB_ROYALE_SIMULATION_TICK_SEQUENCE_HPP
#define BLOB_ROYALE_SIMULATION_TICK_SEQUENCE_HPP

#include "simulation_limits.hpp"

#include <compare>
#include <cstdint>

namespace blob_royale::simulation {

// canonical: tick_sequence -- exact committed simulation-state sequence value.
class TickSequence final {
public:
  using Value = std::uint64_t;

  static constexpr Value kMinimumValue = 0;
  static constexpr Value kMaximumValue = kMaximumProtocolSafeInteger;

  // Returns the sequence assigned to the loaded initial state.
  [[nodiscard]] static TickSequence zero() noexcept;

  // Creates an exact sequence in the inclusive protocol-safe integer range [0, 2^53 - 1].
  // Throws SimulationValidationError when value exceeds kMaximumValue.
  [[nodiscard]] static TickSequence create(Value value);

  TickSequence(const TickSequence&) = default;
  TickSequence(TickSequence&&) noexcept = default;
  TickSequence& operator=(const TickSequence&) = default;
  TickSequence& operator=(TickSequence&&) noexcept = default;
  ~TickSequence() = default;

  [[nodiscard]] Value value() const noexcept { return value_; }

  // Returns the next exact sequence. Throws SimulationValidationError instead of wrapping at max.
  [[nodiscard]] TickSequence next() const;

  friend bool operator==(const TickSequence&, const TickSequence&) = default;
  friend std::strong_ordering operator<=>(const TickSequence&, const TickSequence&) = default;

private:
  explicit TickSequence(Value value) noexcept;

  Value value_;
};

} // namespace blob_royale::simulation

#endif
