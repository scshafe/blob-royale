#include "tick_sequence.hpp"

#include "simulation_validation_error.hpp"

#include <string>

namespace blob_royale::simulation {

TickSequence TickSequence::zero() noexcept { return TickSequence(kMinimumValue); }

TickSequence TickSequence::create(const Value value) {
  if (value > kMaximumValue) {
    throw SimulationValidationError(SimulationValidationCode::kTickSequenceOutOfRange,
                                    "tick_sequence.value",
                                    "tick sequence " + std::to_string(value) +
                                        " exceeds the inclusive protocol-safe integer range");
  }
  return TickSequence(value);
}

TickSequence TickSequence::next() const {
  if (value_ == kMaximumValue) {
    throw SimulationValidationError(SimulationValidationCode::kTickSequenceOutOfRange,
                                    "tick_sequence.next",
                                    "maximum tick sequence cannot be incremented");
  }
  return TickSequence(value_ + 1);
}

TickSequence::TickSequence(const Value value) noexcept : value_(value) {}

} // namespace blob_royale::simulation
