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

[[nodiscard]] const GameModeRegistry::Registration&
require_registration(const std::string_view mode_name) {
  const auto* registration = find_registration(mode_name);
  if (registration == nullptr) {
    throw GameplayValidationError(
        GameplayValidationCode::kGameModeNameUnknown, "game_mode_registry.mode",
        "mode " + std::string(mode_name) + " is registered by no row; the registered modes are " +
            GameModeRegistry::registered_names());
  }
  return *registration;
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
  return require_registration(mode_name).factory(configuration);
}

std::unique_ptr<const simulation::GameMode>
GameModeRegistry::create(const std::string_view mode_name) {
  return create(mode_name, GameModeConfiguration::defaults());
}

std::span<const HazardArchetype>
GameModeRegistry::active_hazards(const std::string_view mode_name,
                                 const GameModeConfiguration& configuration) {
  return require_registration(mode_name).crossing_hazards
             ? std::span<const HazardArchetype>{configuration.hazards}
             : std::span<const HazardArchetype>{};
}

simulation::MovementTuningState
GameModeRegistry::initial_room_tuning(const std::string_view mode_name,
                                      const GameModeConfiguration& configuration) {
  const auto hazards = active_hazards(mode_name, configuration);
  bool lethal = false;
  bool nonlethal = false;
  for (const auto& archetype : hazards) {
    if (archetype.lethal_on_contact())
      lethal = true;
    else
      nonlethal = true;
  }
  const auto& authored = configuration.movement;
  const auto effective = simulation::MovementTuning::create(
      authored.acceleration(), authored.normal_top_speed(), authored.charge_speed_fraction(),
      lethal ? authored.lethal_spawn_rate_per_second() : 0.0,
      nonlethal ? authored.nonlethal_spawn_rate_per_second() : 0.0);
  return {.current = effective,
          .defaults = effective,
          .charge_speed_fraction_maximum = configuration.abilities.charge_safety_envelope_speed() /
                                           simulation::kMaximumNormalTopSpeed,
          .lethal_spawn_rate_per_second_maximum =
              lethal ? simulation::kMaximumCrossingSpawnRatePerSecond : 0.0,
          .nonlethal_spawn_rate_per_second_maximum =
              nonlethal ? simulation::kMaximumCrossingSpawnRatePerSecond : 0.0};
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
