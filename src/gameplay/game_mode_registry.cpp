#include "game_mode_registry.hpp"

#include "gameplay_validation_error.hpp"

#include <memory>
#include <string>

namespace blob_royale::gameplay {
namespace {

[[nodiscard]] const GameModeRegistry::Registration*
find_registration(const std::string_view mode_name) noexcept {
  for (const GameModeRegistry::Registration& registration : kGameModeRegistrations) {
    if (registration.name == mode_name) {
      return &registration;
    }
  }
  return nullptr;
}

} // namespace

std::span<const GameModeRegistry::Registration> GameModeRegistry::registrations() noexcept {
  return kGameModeRegistrations;
}

bool GameModeRegistry::contains(const std::string_view mode_name) noexcept {
  return find_registration(mode_name) != nullptr;
}

std::unique_ptr<const simulation::GameMode>
GameModeRegistry::create(const std::string_view mode_name,
                         const GameModeConfiguration& configuration) {
  const Registration* registration = find_registration(mode_name);
  if (registration == nullptr) {
    throw GameplayValidationError(
        GameplayValidationCode::kGameModeNameUnknown, "game_mode_registry.mode",
        "mode " + std::string(mode_name) + " is registered by no row; the registered modes are " +
            registered_names());
  }
  return registration->factory(configuration);
}

std::unique_ptr<const simulation::GameMode>
GameModeRegistry::create(const std::string_view mode_name) {
  return create(mode_name, GameModeConfiguration::defaults());
}

std::string GameModeRegistry::registered_names() {
  std::string names;
  for (const Registration& registration : kGameModeRegistrations) {
    if (!names.empty()) {
      names.append(", ");
    }
    names.append(registration.name);
  }
  return names;
}

} // namespace blob_royale::gameplay
