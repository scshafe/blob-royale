#ifndef BLOB_ROYALE_SIMULATION_MOTION_RESPONSE_HPP
#define BLOB_ROYALE_SIMULATION_MOTION_RESPONSE_HPP

#include "physics_body.hpp"

#include <cstdint>
#include <vector>

namespace blob_royale::simulation {

// canonical: motion_disposition -- a pure solver result, adopted by ContactResponse only at
// ADR 0008 plan Step 16. Termination removes this body from the remainder of the quantum.
enum class MotionDisposition : std::uint8_t { kContinue, kTerminate };

struct MotionBodyResult final {
  PhysicsBody body;
  MotionDisposition disposition = MotionDisposition::kContinue;
  friend bool operator==(const MotionBodyResult&, const MotionBodyResult&) = default;
};

// canonical: pair_motion_response -- typed consequences leave the pure solver as values.
// The solver validates replacement geometry and bounds cumulative effects before retaining them.
// Step 4 gameplay facts and Step 16 WorldEvents use this same driver/response shape.
template <class Effect> struct PairMotionResponse final {
  MotionBodyResult first;
  MotionBodyResult second;
  std::vector<Effect> effects;
  friend bool operator==(const PairMotionResponse&, const PairMotionResponse&) = default;
};

} // namespace blob_royale::simulation

#endif
