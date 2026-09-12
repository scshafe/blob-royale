#include "npc_catalogue.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <algorithm>
#include <utility>

namespace blob_royale::simulation {

NpcCatalogue NpcCatalogue::create(std::vector<std::string> unprofiled_kinds,
                                  std::vector<NpcDeclaration> profiles) {
  if (unprofiled_kinds.size() > kMaximumUnprofiledNpcKindCount ||
      profiles.size() > kMaximumNpcProfileCount ||
      unprofiled_kinds.size() + profiles.size() > kMaximumNpcCatalogueChoiceCount) {
    throw SimulationValidationError(SimulationValidationCode::kNpcCatalogueLimitExceeded,
                                    "npc_catalogue",
                                    "NPC catalogue exceeds its partition or total bounds");
  }
  for (std::size_t index = 0; index < unprofiled_kinds.size(); ++index) {
    SeatKindNamePolicy::validate(unprofiled_kinds[index]);
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (unprofiled_kinds[previous] == unprofiled_kinds[index]) {
        throw SimulationValidationError(SimulationValidationCode::kNpcCatalogueDuplicate,
                                        "npc_catalogue.unprofiled_kinds",
                                        "duplicate unprofiled kind");
      }
    }
  }
  for (std::size_t index = 0; index < profiles.size(); ++index) {
    const auto& declaration = profiles[index];
    SeatKindNamePolicy::validate(declaration.kind.value());
    if (!declaration.profile_name.has_value()) {
      throw SimulationValidationError(SimulationValidationCode::kNpcCatalogueProfileMissing,
                                      "npc_catalogue.profiles",
                                      "profile partition requires a profile name");
    }
    if (std::ranges::find(unprofiled_kinds, declaration.kind.value()) != unprofiled_kinds.end()) {
      throw SimulationValidationError(SimulationValidationCode::kNpcCatalogueMixedKind,
                                      "npc_catalogue.profiles",
                                      "a kind cannot be both plain and profiled");
    }
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (profiles[previous] == declaration) {
        throw SimulationValidationError(SimulationValidationCode::kNpcCatalogueDuplicate,
                                        "npc_catalogue.profiles", "duplicate profiled declaration");
      }
    }
  }
  return NpcCatalogue(std::move(unprofiled_kinds), std::move(profiles));
}

NpcCatalogue NpcCatalogue::empty() { return NpcCatalogue({}, {}); }

NpcCatalogue::NpcCatalogue(std::vector<std::string> unprofiled_kinds,
                           std::vector<NpcDeclaration> profiles) noexcept
    : unprofiled_kinds_(std::move(unprofiled_kinds)), profiles_(std::move(profiles)) {}

bool NpcCatalogue::contains(const std::string_view kind,
                            const std::optional<std::string_view> profile_name) const noexcept {
  if (!profile_name.has_value()) {
    return std::ranges::find(unprofiled_kinds_, kind) != unprofiled_kinds_.end();
  }
  return std::ranges::any_of(profiles_, [&](const NpcDeclaration& declaration) {
    return declaration.kind.value() == kind && declaration.profile_name->value() == *profile_name;
  });
}

bool NpcCatalogue::contains(const NpcDeclaration& declaration) const noexcept {
  return contains(declaration.kind.value(), declaration.profile_name.has_value()
                                                ? std::optional{declaration.profile_name->value()}
                                                : std::nullopt);
}

} // namespace blob_royale::simulation
