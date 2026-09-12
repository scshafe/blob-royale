#ifndef BLOB_ROYALE_SIMULATION_NPC_CATALOGUE_HPP
#define BLOB_ROYALE_SIMULATION_NPC_CATALOGUE_HPP

#include "npc_declaration.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace blob_royale::simulation {

// canonical: npc_catalogue -- one immutable admitted selection set for runtime and publication.
// It owns names and declaration order, but knows no controller factory or tactical policy.
class NpcCatalogue final {
public:
  // Rejects malformed names, limits, duplicate choices, missing profiles, and mixed partitions
  // with SIMULATION.NPC_CATALOGUE_* or SIMULATION.SEAT_KIND_NAME_INVALID.
  [[nodiscard]] static NpcCatalogue create(std::vector<std::string> unprofiled_kinds,
                                           std::vector<NpcDeclaration> profiles = {});
  // An empty catalogue admits no NPC selection.
  [[nodiscard]] static NpcCatalogue empty();

  [[nodiscard]] std::span<const std::string> unprofiled_kinds() const& noexcept {
    return unprofiled_kinds_;
  }
  [[nodiscard]] std::span<const std::string> unprofiled_kinds() const&& = delete;
  [[nodiscard]] std::span<const NpcDeclaration> profiles() const& noexcept { return profiles_; }
  [[nodiscard]] std::span<const NpcDeclaration> profiles() const&& = delete;

  // Absence denotes an unprofiled choice, never a wildcard for this kind's profiles.
  [[nodiscard]] bool
  contains(std::string_view kind,
           std::optional<std::string_view> profile_name = std::nullopt) const noexcept;
  [[nodiscard]] bool contains(const NpcDeclaration& declaration) const noexcept;

  friend bool operator==(const NpcCatalogue&, const NpcCatalogue&) = default;

private:
  NpcCatalogue(std::vector<std::string> unprofiled_kinds,
               std::vector<NpcDeclaration> profiles) noexcept;

  std::vector<std::string> unprofiled_kinds_;
  std::vector<NpcDeclaration> profiles_;
};

} // namespace blob_royale::simulation

#endif
