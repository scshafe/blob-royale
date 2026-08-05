#ifndef BLOB_ROYALE_SIMULATION_CANDIDATE_PAIR_HPP
#define BLOB_ROYALE_SIMULATION_CANDIDATE_PAIR_HPP

#include "entity_id.hpp"

#include <compare>

namespace blob_royale::simulation {

// canonical: simulation_candidate_pair -- one distinct pair in ascending EntityId order.
class CandidatePair final {
public:
  // Canonicalizes two distinct IDs or throws SimulationValidationError when they are equal.
  [[nodiscard]] static CandidatePair create(EntityId first, EntityId second);

  CandidatePair(const CandidatePair&) = default;
  CandidatePair(CandidatePair&&) noexcept = default;
  CandidatePair& operator=(const CandidatePair&) = default;
  CandidatePair& operator=(CandidatePair&&) noexcept = default;
  ~CandidatePair() = default;

  [[nodiscard]] EntityId lower_id() const noexcept { return lower_id_; }
  [[nodiscard]] EntityId higher_id() const noexcept { return higher_id_; }

  friend bool operator==(const CandidatePair&, const CandidatePair&) = default;
  friend std::strong_ordering operator<=>(const CandidatePair&, const CandidatePair&) = default;

private:
  CandidatePair(EntityId lower_id, EntityId higher_id) noexcept;

  EntityId lower_id_;
  EntityId higher_id_;
};

} // namespace blob_royale::simulation

#endif
