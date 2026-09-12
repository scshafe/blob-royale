#include "tactical_seed_identity.hpp"

#include "controllers_limits.hpp"
#include "controllers_validation_error.hpp"
#include "deterministic_random.hpp"

namespace blob_royale::controllers {

void validate_tactical_seed_identity(const TacticalSeedIdentity& identity) {
  if (identity.lobby_id == 0 || identity.lobby_id > simulation::kMaximumProtocolSafeInteger ||
      identity.seat_index >= simulation::kMaximumLobbySeatCount) {
    throw ControllersValidationError(
        ControllersValidationCode::kTacticalSeedIdentityInvalid,
        "tactical_controller.seed_identity",
        "identity requires a positive safe lobby and an authored seat index");
  }
}

std::uint64_t tactical_seed_for(const TacticalSeedIdentity& identity,
                                const simulation::BotProfileName& profile_name,
                                const simulation::TickSequence running_started_tick) noexcept {
  using Random = simulation::DeterministicRandom;
  auto mixed = Random::mix_bits(kTacticalSeedDomain ^ identity.match_seed);
  mixed = Random::mix_bits(mixed ^ identity.lobby_id);
  mixed = Random::mix_bits(mixed ^ identity.seat_index);
  mixed = Random::mix_bits(mixed ^ static_cast<std::uint64_t>(profile_name.value().size()));
  for (const char character : profile_name.value()) {
    mixed = Random::mix_bits(mixed ^ static_cast<unsigned char>(character));
  }
  return Random::mix_bits(mixed ^ running_started_tick.value());
}

} // namespace blob_royale::controllers
