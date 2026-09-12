#ifndef BLOB_ROYALE_SIMULATION_BOT_PROFILE_NAME_HPP
#define BLOB_ROYALE_SIMULATION_BOT_PROFILE_NAME_HPP

#include "bot_profile_name_policy.hpp"
#include "bounded_name.hpp"

namespace blob_royale::simulation {

// canonical: bot_profile_name -- required fixed-capacity profile identity, with no empty default.
using BotProfileName = BoundedName<BotProfileNamePolicy>;

} // namespace blob_royale::simulation

#endif
