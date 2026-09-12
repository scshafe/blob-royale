#ifndef BLOB_ROYALE_CONTROLLERS_CREATION_CONTEXT_HPP
#define BLOB_ROYALE_CONTROLLERS_CREATION_CONTEXT_HPP

#include "tactical_seed_identity.hpp"

#include <optional>

namespace blob_royale::controllers {

class TacticalProfile;

// Borrowed only during Registry::create. Profiled factories copy both values; plain rows reject
// either field. Absence means a plain factory call, never a request for an invented profile.
struct CreationContext final {
  const TacticalProfile* profile{nullptr};
  std::optional<TacticalSeedIdentity> tactical_identity{};
};

} // namespace blob_royale::controllers

#endif
