#ifndef BLOB_ROYALE_TESTS_UNIT_SIMULATION_FIXTURES_NPC_DECLARATION_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_SIMULATION_FIXTURES_NPC_DECLARATION_FIXTURE_HPP

#include "command_registry.hpp"
#include "npc_catalogue.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace blob_royale::testing::npc_declaration_fixture {

namespace simulation = blob_royale::simulation;

inline constexpr std::string_view kPlainKind = "wanderer";
inline constexpr std::string_view kProfileKind = "tactical";
inline constexpr std::string_view kProfileName = "steady";
inline constexpr std::string_view kOtherProfileName = "quick";
inline constexpr std::uint64_t kController = 1;
inline constexpr std::uint64_t kBotController = 2;
inline constexpr std::uint64_t kSeatIndex = 1;
inline constexpr std::size_t kSeatCount = 4;
inline constexpr std::array<std::string_view, 6> kInvalidNames{"",         "Steady",  "has space",
                                                               "has-dash", "1steady", "_steady"};

[[nodiscard]] inline simulation::NpcDeclaration
profiled(const std::string_view name = kProfileName) {
  return {simulation::SeatKindName::create(kProfileKind), simulation::BotProfileName::create(name)};
}

[[nodiscard]] inline simulation::NpcDeclaration plain() {
  return {simulation::SeatKindName::create(kPlainKind), std::nullopt};
}

[[nodiscard]] inline simulation::NpcCatalogue catalogue() {
  return simulation::NpcCatalogue::create({std::string{kPlainKind}, "chaser"},
                                          {profiled(), profiled(kOtherProfileName)});
}

[[nodiscard]] inline simulation::Command seat(const simulation::NpcDeclaration& declaration,
                                              const std::uint64_t controller = kController) {
  return simulation::SeatNpcCommand{simulation::ControllerId::create(controller), kSeatIndex,
                                    declaration.kind, declaration.profile_name};
}

[[nodiscard]] inline simulation::Command
join(const std::optional<simulation::NpcDeclaration> expected,
     const std::optional<std::uint64_t> seat_index = kSeatIndex) {
  return simulation::JoinCommand{simulation::ControllerId::create(kBotController), seat_index,
                                 expected};
}

[[nodiscard]] inline std::vector<std::string> plain_names(const std::size_t count) {
  std::vector<std::string> names;
  for (std::size_t index = 0; index < count; ++index) {
    names.push_back("plain_" + std::to_string(index));
  }
  return names;
}

[[nodiscard]] inline std::vector<simulation::NpcDeclaration>
profile_names(const std::size_t count) {
  std::vector<simulation::NpcDeclaration> declarations;
  for (std::size_t index = 0; index < count; ++index) {
    declarations.push_back(profiled("profile_" + std::to_string(index)));
  }
  return declarations;
}

} // namespace blob_royale::testing::npc_declaration_fixture

#endif
