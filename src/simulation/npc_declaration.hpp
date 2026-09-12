#ifndef BLOB_ROYALE_SIMULATION_NPC_DECLARATION_HPP
#define BLOB_ROYALE_SIMULATION_NPC_DECLARATION_HPP

#include "bot_profile_name.hpp"
#include "seat_kind_name.hpp"

#include <optional>

namespace blob_royale::simulation {

// canonical: npc_declaration -- complete NPC selection identity, independent of its controller.
struct NpcDeclaration final {
  SeatKindName kind;
  std::optional<BotProfileName> profile_name{};

  [[nodiscard]] bool is_valid() const noexcept { return !kind.empty(); }
  friend bool operator==(const NpcDeclaration&, const NpcDeclaration&) = default;
};

} // namespace blob_royale::simulation

#endif
