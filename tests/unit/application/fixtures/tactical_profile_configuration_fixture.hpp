#ifndef BLOB_ROYALE_TESTS_UNIT_APPLICATION_FIXTURES_TACTICAL_PROFILE_CONFIGURATION_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_APPLICATION_FIXTURES_TACTICAL_PROFILE_CONFIGURATION_FIXTURE_HPP

#include "../application_input_test_fixture.hpp"

#include "controllers_validation_error.hpp"
#include "match_configuration.hpp"
#include "tactical_profile_catalogue.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace blob_royale::application::tactical_profile_configuration_fixture {

inline constexpr std::string_view kFirstProfileName = "porcelain_otter";
inline constexpr std::string_view kSecondProfileName = "velvet_ibis";
inline constexpr std::string_view kUnknownProfileName = "unconfigured_profile";
inline constexpr std::string_view kProfileKind = "tactical";
inline constexpr std::string_view kPlainKind = "wanderer";
inline constexpr std::string_view kDefaultMode = "royale";
inline constexpr std::string_view kBaseRosterLine = "bots=wanderer:2, chaser:1";
inline constexpr std::string_view kProfileRoster = "tactical@porcelain_otter:1";
inline constexpr std::string_view kMixedRoster =
    "tactical@porcelain_otter:1, wanderer:1, tactical@velvet_ibis:2";
inline constexpr std::string_view kSpacedRoster =
    " tactical @ porcelain_otter : 1 , wanderer : 1 , tactical @ velvet_ibis : 2 ";
inline constexpr std::string_view kDuplicateProfileRoster =
    "tactical@porcelain_otter:1,tactical@porcelain_otter:2";
inline constexpr std::string_view kFirstProfileHeader = "[bot_profile.porcelain_otter]";
inline constexpr std::string_view kFirstProfileSection = "\n[bot_profile.porcelain_otter]\n"
                                                         "objective_seek_probability=1\n"
                                                         "reaction_delay_ticks=80\n"
                                                         "aim_error=0.05\n"
                                                         "target_persistence_ticks=400\n";
inline constexpr std::string_view kSecondProfileSection = "\n[bot_profile.velvet_ibis]\n"
                                                          "objective_seek_probability=0.25\n"
                                                          "reaction_delay_ticks=23\n"
                                                          "aim_error=0.125\n"
                                                          "target_persistence_ticks=71\n";
inline constexpr std::string_view kHazardSection = "\n[hazard.porcelain_otter]\n"
                                                   "radius_world_units=10\n"
                                                   "mass=1\n"
                                                   "restitution=1\n"
                                                   "speed_world_units_per_second=260\n"
                                                   "spawn_interval_seconds=6\n"
                                                   "lethal_on_contact=true\n";
inline const controllers::TacticalProfile::Section kFirstProfileValues{
    .profile_name = "porcelain_otter",
    .objective_seek_probability = 1,
    .reaction_delay_ticks = 80,
    .aim_error = 0.05,
    .target_persistence_ticks = 400};
inline const controllers::TacticalProfile::Section kSecondProfileValues{
    .profile_name = "velvet_ibis",
    .objective_seek_probability = 0.25,
    .reaction_delay_ticks = 23,
    .aim_error = 0.125,
    .target_persistence_ticks = 71};
inline constexpr std::string_view kMinimumProfileSection =
    "\n[bot_profile.porcelain_otter]\nobjective_seek_probability=0\nreaction_delay_ticks=0\n"
    "aim_error=0\ntarget_persistence_ticks=0\n";
inline constexpr std::string_view kMaximumProfileSection =
    "\n[bot_profile.porcelain_otter]\nobjective_seek_probability=1\nreaction_delay_ticks=4000\n"
    "aim_error=0.25\ntarget_persistence_ticks=4000\n";
inline const controllers::TacticalProfile::Section kMinimumProfileValues{
    .profile_name = "porcelain_otter",
    .objective_seek_probability = 0,
    .reaction_delay_ticks = 0,
    .aim_error = 0,
    .target_persistence_ticks = 0};
inline const controllers::TacticalProfile::Section kMaximumProfileValues{
    .profile_name = "porcelain_otter",
    .objective_seek_probability = 1,
    .reaction_delay_ticks = 4000,
    .aim_error = 0.25,
    .target_persistence_ticks = 4000};

struct RequiredField final {
  std::string_view line;
  std::string_view context;
};
inline constexpr std::array kRequiredFields{
    RequiredField{"objective_seek_probability=1\n",
                  "bot_profile.porcelain_otter.objective_seek_probability"},
    RequiredField{"reaction_delay_ticks=80\n", "bot_profile.porcelain_otter.reaction_delay_ticks"},
    RequiredField{"aim_error=0.05\n", "bot_profile.porcelain_otter.aim_error"},
    RequiredField{"target_persistence_ticks=400\n",
                  "bot_profile.porcelain_otter.target_persistence_ticks"}};

struct ParserFailure final {
  std::string_view original;
  std::string_view replacement;
  ApplicationInputErrorCode code;
};
inline constexpr std::array kParserFailures{
    ParserFailure{kFirstProfileHeader, "[bot_profile]",
                  ApplicationInputErrorCode::kConfigurationSectionUnknown},
    ParserFailure{kFirstProfileHeader, "[bot_profile.]",
                  ApplicationInputErrorCode::kConfigurationSectionUnknown},
    ParserFailure{kFirstProfileHeader, "[bot_profiles.porcelain_otter]",
                  ApplicationInputErrorCode::kConfigurationSectionUnknown},
    ParserFailure{kFirstProfileHeader, "[bot_profile.porcelain_otter",
                  ApplicationInputErrorCode::kConfigurationSyntaxInvalid},
    ParserFailure{"aim_error=0.05", "aggression=0.05",
                  ApplicationInputErrorCode::kConfigurationKeyUnknown},
    ParserFailure{"aim_error=0.05", "aim_error=0.05\naim_error=0.1",
                  ApplicationInputErrorCode::kConfigurationKeyDuplicate},
    ParserFailure{"aim_error=0.05",
                  "aim_error=", ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"aim_error=0.05", "aim_error=0.05oops",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"objective_seek_probability=1", "objective_seek_probability=true",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"reaction_delay_ticks=80", "reaction_delay_ticks=-1",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"reaction_delay_ticks=80", "reaction_delay_ticks=+1",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"reaction_delay_ticks=80", "reaction_delay_ticks=1.5",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"reaction_delay_ticks=80", "reaction_delay_ticks=1e2",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"reaction_delay_ticks=80", "reaction_delay_ticks=18446744073709551616",
                  ApplicationInputErrorCode::kConfigurationValueOutOfRange},
    ParserFailure{"target_persistence_ticks=400", "target_persistence_ticks=-1",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"target_persistence_ticks=400", "target_persistence_ticks=2.5",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"target_persistence_ticks=400", "target_persistence_ticks=4e2",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"target_persistence_ticks=400", "target_persistence_ticks=nan",
                  ApplicationInputErrorCode::kConfigurationValueInvalid},
    ParserFailure{"target_persistence_ticks=400", "target_persistence_ticks=18446744073709551616",
                  ApplicationInputErrorCode::kConfigurationValueOutOfRange}};

struct DomainFailure final {
  std::string_view original;
  std::string_view replacement;
  controllers::ControllersValidationCode code;
  std::string_view context;
};
inline constexpr std::array kDomainFailures{
    DomainFailure{"objective_seek_probability=1", "objective_seek_probability=-0.01",
                  controllers::ControllersValidationCode::kTacticalProfileProbabilityInvalid,
                  "bot_profile.porcelain_otter.objective_seek_probability"},
    DomainFailure{"objective_seek_probability=1", "objective_seek_probability=1.01",
                  controllers::ControllersValidationCode::kTacticalProfileProbabilityInvalid,
                  "bot_profile.porcelain_otter.objective_seek_probability"},
    DomainFailure{"objective_seek_probability=1", "objective_seek_probability=nan",
                  controllers::ControllersValidationCode::kTacticalProfileProbabilityInvalid,
                  "bot_profile.porcelain_otter.objective_seek_probability"},
    DomainFailure{"objective_seek_probability=1", "objective_seek_probability=inf",
                  controllers::ControllersValidationCode::kTacticalProfileProbabilityInvalid,
                  "bot_profile.porcelain_otter.objective_seek_probability"},
    DomainFailure{"reaction_delay_ticks=80", "reaction_delay_ticks=4001",
                  controllers::ControllersValidationCode::kTacticalProfileReactionDelayInvalid,
                  "bot_profile.porcelain_otter.reaction_delay_ticks"},
    DomainFailure{"aim_error=0.05", "aim_error=-0.01",
                  controllers::ControllersValidationCode::kTacticalProfileAimErrorInvalid,
                  "bot_profile.porcelain_otter.aim_error"},
    DomainFailure{"aim_error=0.05", "aim_error=0.251",
                  controllers::ControllersValidationCode::kTacticalProfileAimErrorInvalid,
                  "bot_profile.porcelain_otter.aim_error"},
    DomainFailure{"aim_error=0.05", "aim_error=nan",
                  controllers::ControllersValidationCode::kTacticalProfileAimErrorInvalid,
                  "bot_profile.porcelain_otter.aim_error"},
    DomainFailure{"aim_error=0.05", "aim_error=inf",
                  controllers::ControllersValidationCode::kTacticalProfileAimErrorInvalid,
                  "bot_profile.porcelain_otter.aim_error"},
    DomainFailure{"target_persistence_ticks=400", "target_persistence_ticks=4001",
                  controllers::ControllersValidationCode::kTacticalProfilePersistenceInvalid,
                  "bot_profile.porcelain_otter.target_persistence_ticks"}};

struct SelectionFailure final {
  std::string_view roster;
  ApplicationInputErrorCode code;
};
inline constexpr std::array kSelectionFailures{
    SelectionFailure{"tactical:1", ApplicationInputErrorCode::kMatchBotProfileRequired},
    SelectionFailure{"wanderer@porcelain_otter:1",
                     ApplicationInputErrorCode::kMatchBotProfileUnexpected},
    SelectionFailure{"chaser@porcelain_otter:1",
                     ApplicationInputErrorCode::kMatchBotProfileUnexpected},
    SelectionFailure{"tactical@unconfigured_profile:1",
                     ApplicationInputErrorCode::kMatchBotProfileUnknown},
    SelectionFailure{"unregistered@porcelain_otter:1",
                     ApplicationInputErrorCode::kMatchBotKindUnknown}};

inline constexpr std::array kMalformedRosterTerms{"tactical@:1",
                                                  "@porcelain_otter:1",
                                                  "tactical@@porcelain_otter:1",
                                                  "tactical@porcelain_otter",
                                                  "tactical@porcelain_otter:",
                                                  "tactical@porcelain_otter:0",
                                                  "tactical@porcelain_otter:-1",
                                                  "tactical@porcelain_otter:1.5",
                                                  "tactical@porcelain_otter:1:2"};
inline constexpr std::array kMalformedNames{"Porcelain_otter", "porcelain-otter", "porcelain.otter",
                                            "7otter"};
inline const std::string kOversizedName(65, 'a');
inline const std::string kMaximumName(64, 'a');

struct ModeAdmission final {
  std::string_view mode;
  bool admits_profiled_roster;
};
inline constexpr std::array kModeAdmissions{
    ModeAdmission{"sandbox", false}, ModeAdmission{"royale", true},
    ModeAdmission{"king_of_the_hill", true}, ModeAdmission{"race", true}};

[[nodiscard]] inline std::string
configuration(const std::string_view roster = kProfileRoster,
              const std::string_view mode = kDefaultMode,
              const std::string_view sections = kFirstProfileSection) {
  std::string text = test_fixture::replace_once(std::string{test_fixture::kValidConfiguration},
                                                kBaseRosterLine, "bots=" + std::string{roster});
  text = test_fixture::replace_once(std::move(text), "mode=royale", "mode=" + std::string{mode});
  text.append(sections);
  return text;
}

[[nodiscard]] inline std::string two_profile_configuration() {
  std::string sections{kFirstProfileSection};
  sections.append(kHazardSection);
  sections.append(kSecondProfileSection);
  return configuration(kMixedRoster, kDefaultMode, sections);
}

[[nodiscard]] inline std::string profile_sections(const std::size_t count) {
  std::string sections;
  for (std::size_t index = 0; index < count; ++index) {
    sections.append(
        test_fixture::replace_once(std::string{kFirstProfileSection}, kFirstProfileHeader,
                                   "[bot_profile.authored_" + std::to_string(index) + "]"));
  }
  return sections;
}

[[nodiscard]] inline std::vector<MatchConfiguration::BotRosterEntry>
profiled_roster(const std::size_t count) {
  std::vector<MatchConfiguration::BotRosterEntry> entries;
  for (std::size_t index = 0; index < count; ++index) {
    entries.push_back({std::string{kProfileKind}, 1,
                       simulation::BotProfileName::create("authored_" + std::to_string(index))});
  }
  return entries;
}

[[nodiscard]] inline std::string
roster_text(const std::vector<MatchConfiguration::BotRosterEntry>& entries) {
  std::string text;
  for (const auto& entry : entries) {
    if (!text.empty()) {
      text.append(",");
    }
    text.append(entry.controller_kind);
    if (entry.profile_name.has_value()) {
      text.append("@");
      text.append(entry.profile_name->value());
    }
    text.append(":" + std::to_string(entry.count));
  }
  return text;
}

[[nodiscard]] inline ApplicationConfig
load(const test_fixture::TemporaryApplicationInputWorkspace& workspace,
     const std::string_view text) {
  const auto result = test_fixture::load_application_config(
      workspace.write_file("tactical-profile-configuration.cfg", text));
  return std::get<ApplicationConfigLoader::RunRequest>(result).application_config();
}

[[nodiscard]] inline MatchConfiguration
match_with_roster(std::vector<MatchConfiguration::BotRosterEntry> entries,
                  const std::string_view mode = kDefaultMode) {
  return MatchConfiguration::create(std::string{mode}, "arena-960x640", "maps", 1, 4,
                                    std::move(entries));
}

[[nodiscard]] inline ApplicationConfig
application_with_match(const ApplicationConfig& baseline, MatchConfiguration match,
                       controllers::TacticalProfileCatalogue profiles) {
  return ApplicationConfig::create(baseline.server_config(), baseline.simulation_config(),
                                   std::move(match), baseline.game_mode_configuration(),
                                   baseline.lobbies_configuration(), std::move(profiles));
}

} // namespace blob_royale::application::tactical_profile_configuration_fixture

#endif
