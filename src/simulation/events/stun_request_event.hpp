#ifndef BLOB_ROYALE_SIMULATION_EVENTS_STUN_REQUEST_EVENT_HPP
#define BLOB_ROYALE_SIMULATION_EVENTS_STUN_REQUEST_EVENT_HPP

#include "entity_id.hpp"

#include <cstdint>

namespace blob_royale::simulation {

// canonical: stun_request -- in-tick request consumed by shared PostKernel status processing.
// Zero duration and targets without dynamic bodies are explicit no-ops.
struct StunRequest final {
  EntityId entity;
  std::uint64_t duration_ticks;

  friend bool operator==(const StunRequest&, const StunRequest&) = default;
};

} // namespace blob_royale::simulation

#endif
