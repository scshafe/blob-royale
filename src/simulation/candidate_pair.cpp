#include "candidate_pair.hpp"

#include "simulation_validation_error.hpp"

#include <string>
#include <utility>

namespace blob_royale::simulation {

CandidatePair CandidatePair::create(EntityId first, EntityId second) {
  if (first == second) {
    throw SimulationValidationError(
        SimulationValidationCode::kCandidatePairDuplicateEntityId, "candidate_pair.entity_ids",
        "candidate pair requires two distinct EntityId values; received " +
            std::to_string(first.value()) + " twice");
  }
  if (second < first) {
    std::swap(first, second);
  }
  return CandidatePair(first, second);
}

CandidatePair::CandidatePair(const EntityId lower_id, const EntityId higher_id) noexcept
    : lower_id_(lower_id), higher_id_(higher_id) {}

} // namespace blob_royale::simulation
