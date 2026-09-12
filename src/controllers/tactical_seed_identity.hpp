#ifndef BLOB_ROYALE_CONTROLLERS_TACTICAL_SEED_IDENTITY_HPP
#define BLOB_ROYALE_CONTROLLERS_TACTICAL_SEED_IDENTITY_HPP

#include "bot_profile_name.hpp"
#include "tick_sequence.hpp"

#include <cstdint>

namespace blob_royale::controllers {

// Authored domain identity, independent of controller allocation and body replacement.
struct TacticalSeedIdentity final {
  std::uint64_t match_seed;
  std::uint64_t lobby_id;
  std::uint64_t seat_index;
  friend bool operator==(const TacticalSeedIdentity&, const TacticalSeedIdentity&) = default;
};

// Throws CONTROLLERS.TACTICAL_SEED_IDENTITY_INVALID for a nonpositive/unsafe lobby or invalid seat.
void validate_tactical_seed_identity(const TacticalSeedIdentity& identity);

// canonical: tactical_seed_derivation -- domain, match, lobby, seat, name length/bytes, running
// tick. Pure unsigned ordered mixing; advances no generator and promises no universal collision
// freedom.
[[nodiscard]] std::uint64_t
tactical_seed_for(const TacticalSeedIdentity& identity,
                  const simulation::BotProfileName& profile_name,
                  simulation::TickSequence running_started_tick) noexcept;

} // namespace blob_royale::controllers

#endif
