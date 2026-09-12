#include "application_config.hpp"
#include "application_input_error.hpp"
#include "controller_registry.hpp"
#include "controllers_validation_error.hpp"
#include "game_mode_registry.hpp"
#include "match_configuration.hpp"
#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"
#include "tactical_profile.hpp"
#include "tactical_profile_catalogue.hpp"

#include "application_input_test_fixture.hpp"
#include "fixtures/tactical_profile_configuration_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace blob_royale::application {
namespace {

namespace fixture = tactical_profile_configuration_fixture;
using controllers::ControllersValidationCode;
using controllers::ControllersValidationError;
using controllers::TacticalProfile;
using controllers::TacticalProfileCatalogue;
using test_fixture::TemporaryApplicationInputWorkspace;

template <typename Configuration>
concept HasTemporaryProfileAccessor =
    requires(Configuration&& value) { std::move(value).tactical_profiles(); };
static_assert(!HasTemporaryProfileAccessor<ApplicationConfig>);
static_assert(std::is_same_v<decltype(std::declval<const ApplicationConfig&>().tactical_profiles()),
                             const TacticalProfileCatalogue&>);

TEST_CASE("legacy startup configuration owns an empty tactical catalogue by default",
          "[unit][application][config][tactical]") {
  TemporaryApplicationInputWorkspace workspace;
  const ApplicationConfig loaded = fixture::load(workspace, test_fixture::kValidConfiguration);
  CHECK(loaded.tactical_profiles().profiles().empty());
  const ApplicationConfig copied_without_profiles = ApplicationConfig::create(
      loaded.server_config(), loaded.simulation_config(), loaded.match_configuration(),
      loaded.game_mode_configuration(), loaded.lobbies_configuration());
  CHECK(copied_without_profiles == loaded);
  for (const auto& entry : loaded.match_configuration().bot_roster()) {
    CHECK_FALSE(entry.profile_name.has_value());
  }
}

TEST_CASE("authored tactical profiles preserve values order and independent family names",
          "[unit][application][config][tactical]") {
  TemporaryApplicationInputWorkspace workspace;
  const ApplicationConfig loaded = fixture::load(workspace, fixture::two_profile_configuration());
  const auto profiles = loaded.tactical_profiles().profiles();
  REQUIRE(profiles.size() == 2);
  CHECK(profiles[0] == TacticalProfile::create(fixture::kFirstProfileValues));
  CHECK(profiles[1] == TacticalProfile::create(fixture::kSecondProfileValues));
  CHECK(loaded.tactical_profiles().find(profiles[0].name()) == &profiles[0]);
  CHECK(loaded.tactical_profiles().find(profiles[1].name()) == &profiles[1]);
  CHECK(loaded.tactical_profiles().find(
            simulation::BotProfileName::create(fixture::kUnknownProfileName)) == nullptr);
  REQUIRE(loaded.game_mode_configuration().hazards.size() == 1);
  const auto roster = loaded.match_configuration().bot_roster();
  REQUIRE(roster.size() == 3);
  REQUIRE(roster[0].profile_name.has_value());
  CHECK(*roster[0].profile_name == fixture::kFirstProfileName);
  CHECK_FALSE(roster[1].profile_name.has_value());
  REQUIRE(roster[2].profile_name.has_value());
  CHECK(*roster[2].profile_name == fixture::kSecondProfileName);
  CHECK(loaded.match_configuration().total_bot_count() == 4);
  const ApplicationConfig copied = loaded;
  CHECK(copied == loaded);
  CHECK(copied.tactical_profiles().profiles().data() != profiles.data());
}

TEST_CASE("tactical configuration accepts exact inclusive profile bounds without clamping",
          "[unit][application][config][tactical]") {
  TemporaryApplicationInputWorkspace workspace;
  const ApplicationConfig minimum = fixture::load(
      workspace, fixture::configuration(fixture::kProfileRoster, fixture::kDefaultMode,
                                        fixture::kMinimumProfileSection));
  const ApplicationConfig maximum = fixture::load(
      workspace, fixture::configuration(fixture::kProfileRoster, fixture::kDefaultMode,
                                        fixture::kMaximumProfileSection));
  REQUIRE(minimum.tactical_profiles().profiles().size() == 1);
  REQUIRE(maximum.tactical_profiles().profiles().size() == 1);
  CHECK(minimum.tactical_profiles().profiles()[0] ==
        TacticalProfile::create(fixture::kMinimumProfileValues));
  CHECK(maximum.tactical_profiles().profiles()[0] ==
        TacticalProfile::create(fixture::kMaximumProfileValues));
}

TEST_CASE("each tactical profile key is required by the shared strict family parser",
          "[unit][application][config][tactical][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  for (const auto& field : fixture::kRequiredFields) {
    CAPTURE(field.line);
    const auto text = test_fixture::replace_once(fixture::configuration(), field.line, "");
    try {
      static_cast<void>(fixture::load(workspace, text));
      FAIL("expected the missing tactical key to fail startup");
    } catch (const ApplicationInputError& error) {
      CHECK(error.error_code() == ApplicationInputErrorCode::kConfigurationKeyMissing);
      CHECK(error.detail().find(field.context) != std::string::npos);
    }
  }
}

TEST_CASE("tactical profile sections reject malformed syntax inert keys and noninteger ticks",
          "[unit][application][config][tactical][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  for (const auto& input : fixture::kParserFailures) {
    CAPTURE(input.replacement);
    const auto text =
        test_fixture::replace_once(fixture::configuration(), input.original, input.replacement);
    test_fixture::require_application_input_error_code(
        [&] { static_cast<void>(fixture::load(workspace, text)); }, input.code);
  }
}

TEST_CASE("tactical profile domain bounds reject startup with the exact authored key context",
          "[unit][application][config][tactical][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  for (const auto& input : fixture::kDomainFailures) {
    CAPTURE(input.replacement);
    const auto text =
        test_fixture::replace_once(fixture::configuration(), input.original, input.replacement);
    try {
      static_cast<void>(fixture::load(workspace, text));
      FAIL("expected invalid tactical profile values to fail startup");
    } catch (const ControllersValidationError& error) {
      CHECK(error.validation_code() == input.code);
      CHECK(error.context() == input.context);
      CHECK_FALSE(error.detail().empty());
    }
  }
}

TEST_CASE("tactical profile sections reject duplicate declarations rather than merging settings",
          "[unit][application][config][tactical][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  std::string text = fixture::configuration();
  text.append(fixture::kFirstProfileSection);
  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(fixture::load(workspace, text)); },
      ApplicationInputErrorCode::kConfigurationSectionDuplicate);
}

TEST_CASE("tactical section names use the controller-owned bounded identity grammar",
          "[unit][application][config][tactical][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  const auto reject_name = [&](const std::string_view name) {
    CAPTURE(name);
    const auto section = test_fixture::replace_once(std::string{fixture::kFirstProfileSection},
                                                    fixture::kFirstProfileHeader,
                                                    "[bot_profile." + std::string{name} + "]");
    test_fixture::require_domain_validation_error_code<ControllersValidationError>(
        [&] {
          static_cast<void>(
              fixture::load(workspace, fixture::configuration({}, fixture::kDefaultMode, section)));
        },
        ControllersValidationCode::kTacticalProfileNameInvalid);
  };
  for (const std::string_view name : fixture::kMalformedNames) {
    reject_name(name);
  }
  reject_name(fixture::kOversizedName);
  const auto maximum_section = test_fixture::replace_once(
      std::string{fixture::kFirstProfileSection}, fixture::kFirstProfileHeader,
      "[bot_profile." + fixture::kMaximumName + "]");
  const ApplicationConfig maximum =
      fixture::load(workspace, fixture::configuration({}, fixture::kDefaultMode, maximum_section));
  REQUIRE(maximum.tactical_profiles().profiles().size() == 1);
  CHECK(maximum.tactical_profiles().profiles()[0].name() == fixture::kMaximumName);
}

TEST_CASE("tactical configuration enforces the shared sixteen profile catalogue capacity",
          "[unit][application][config][tactical][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  const ApplicationConfig maximum = fixture::load(
      workspace,
      fixture::configuration({}, fixture::kDefaultMode,
                             fixture::profile_sections(simulation::kMaximumNpcProfileCount)));
  REQUIRE(maximum.tactical_profiles().profiles().size() == simulation::kMaximumNpcProfileCount);
  const auto expected_roster = fixture::profiled_roster(simulation::kMaximumNpcProfileCount);
  for (std::size_t index = 0; index < expected_roster.size(); ++index) {
    CHECK(maximum.tactical_profiles().profiles()[index].name() ==
          *expected_roster[index].profile_name);
  }
  test_fixture::require_domain_validation_error_code<ControllersValidationError>(
      [&] {
        static_cast<void>(fixture::load(
            workspace, fixture::configuration(
                           {}, fixture::kDefaultMode,
                           fixture::profile_sections(simulation::kMaximumNpcProfileCount + 1))));
      },
      ControllersValidationCode::kTacticalProfileCatalogueFull);
}

TEST_CASE("startup selections reject missing unexpected and unknown tactical profiles",
          "[unit][application][config][tactical][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  for (const auto& input : fixture::kSelectionFailures) {
    CAPTURE(input.roster);
    test_fixture::require_application_input_error_code(
        [&] { static_cast<void>(fixture::load(workspace, fixture::configuration(input.roster))); },
        input.code);
  }
  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(fixture::load(
            workspace, fixture::configuration(fixture::kProfileRoster, fixture::kDefaultMode, {})));
      },
      ApplicationInputErrorCode::kMatchBotProfileUnknown);
}

TEST_CASE("match profile requirements resolve from each controller registry row",
          "[unit][application][config][tactical][validation]") {
  for (const auto& registration : controllers::ControllerRegistry::registrations()) {
    CAPTURE(registration.name);
    const MatchConfiguration::BotRosterEntry plain{std::string{registration.name}, 1, {}};
    const MatchConfiguration::BotRosterEntry profiled{
        std::string{registration.name}, 1,
        simulation::BotProfileName::create(fixture::kFirstProfileName)};
    if (registration.requires_profile) {
      test_fixture::require_application_input_error_code(
          [&] { static_cast<void>(fixture::match_with_roster({plain})); },
          ApplicationInputErrorCode::kMatchBotProfileRequired);
      CHECK_NOTHROW(fixture::match_with_roster({profiled}));
    } else {
      CHECK_NOTHROW(fixture::match_with_roster({plain}));
      test_fixture::require_application_input_error_code(
          [&] { static_cast<void>(fixture::match_with_roster({profiled})); },
          ApplicationInputErrorCode::kMatchBotProfileUnexpected);
    }
  }
}

TEST_CASE("profiled roster parsing preserves declaration identity and whitespace semantics",
          "[unit][application][config][tactical]") {
  const auto entries = MatchConfiguration::parse_bot_roster(fixture::kMixedRoster);
  CHECK(entries == MatchConfiguration::parse_bot_roster(fixture::kSpacedRoster));
  const MatchConfiguration match = fixture::match_with_roster(entries);
  REQUIRE(match.bot_roster().size() == 3);
  CHECK(match.total_bot_count() == 4);
  CHECK(match.bot_roster()[0].controller_kind == match.bot_roster()[2].controller_kind);
  CHECK(match.bot_roster()[0].profile_name != match.bot_roster()[2].profile_name);
}

TEST_CASE("profiled roster parsing rejects malformed separators counts and bounded names",
          "[unit][application][config][tactical][validation]") {
  for (const std::string_view roster : fixture::kMalformedRosterTerms) {
    CAPTURE(roster);
    test_fixture::require_application_input_error_code(
        [&] { static_cast<void>(MatchConfiguration::parse_bot_roster(roster)); },
        ApplicationInputErrorCode::kMatchBotRosterInvalid);
  }
  const auto reject_name = [](const std::string_view name) {
    CAPTURE(name);
    test_fixture::require_domain_validation_error_code<simulation::SimulationValidationError>(
        [&] {
          static_cast<void>(MatchConfiguration::parse_bot_roster(
              std::string{fixture::kProfileKind} + "@" + std::string{name} + ":1"));
        },
        simulation::SimulationValidationCode::kBotProfileNameInvalid);
  };
  for (const std::string_view name : fixture::kMalformedNames) {
    reject_name(name);
  }
  reject_name(fixture::kOversizedName);
  const auto maximum_name_roster = MatchConfiguration::parse_bot_roster(
      std::string{fixture::kProfileKind} + "@" + fixture::kMaximumName + ":1");
  REQUIRE(maximum_name_roster.size() == 1);
  REQUIRE(maximum_name_roster.front().profile_name.has_value());
  CHECK(*maximum_name_roster.front().profile_name == fixture::kMaximumName);
}

TEST_CASE("duplicate roster pairs fail both parsing and direct match construction",
          "[unit][application][config][tactical][validation]") {
  test_fixture::require_application_input_error_code(
      [] {
        static_cast<void>(MatchConfiguration::parse_bot_roster(fixture::kDuplicateProfileRoster));
      },
      ApplicationInputErrorCode::kMatchBotRosterInvalid);
  auto entries = MatchConfiguration::parse_bot_roster(fixture::kProfileRoster);
  entries.push_back(entries.front());
  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(fixture::match_with_roster(entries)); },
      ApplicationInputErrorCode::kMatchBotRosterInvalid);
  entries = {{std::string{fixture::kPlainKind}, 1, {}}};
  entries.push_back(entries.front());
  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(fixture::match_with_roster(entries)); },
      ApplicationInputErrorCode::kMatchBotRosterInvalid);
}

TEST_CASE("direct match vectors cannot bypass nonzero count or roster term limits",
          "[unit][application][config][tactical][validation]") {
  auto zero_count = MatchConfiguration::parse_bot_roster(fixture::kProfileRoster);
  zero_count.front().count = 0;
  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(fixture::match_with_roster(zero_count)); },
      ApplicationInputErrorCode::kMatchBotRosterInvalid);
  zero_count.front().controller_kind = fixture::kPlainKind;
  zero_count.front().profile_name.reset();
  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(fixture::match_with_roster(zero_count)); },
      ApplicationInputErrorCode::kMatchBotRosterInvalid);
  const auto maximum = fixture::profiled_roster(MatchConfiguration::kMaximumBotRosterEntryCount);
  CHECK_NOTHROW(fixture::match_with_roster(maximum));
  CHECK(MatchConfiguration::parse_bot_roster(fixture::roster_text(maximum)) == maximum);
  const auto oversized =
      fixture::profiled_roster(MatchConfiguration::kMaximumBotRosterEntryCount + 1);
  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(fixture::match_with_roster(oversized)); },
      ApplicationInputErrorCode::kMatchBotRosterInvalid);
  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(MatchConfiguration::parse_bot_roster(fixture::roster_text(oversized)));
      },
      ApplicationInputErrorCode::kMatchBotRosterInvalid);
}

TEST_CASE("profiled rosters retain the existing total bot limit without overflow",
          "[unit][application][config][tactical][validation]") {
  auto entries = MatchConfiguration::parse_bot_roster(fixture::kProfileRoster);
  entries.front().count = MatchConfiguration::kMaximumBotCount;
  CHECK(fixture::match_with_roster(entries).total_bot_count() ==
        MatchConfiguration::kMaximumBotCount);
  ++entries.front().count;
  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(fixture::match_with_roster(entries)); },
      ApplicationInputErrorCode::kMatchBotRosterTooLarge);
  entries = fixture::profiled_roster(2);
  entries.front().count = MatchConfiguration::kMaximumBotCount;
  test_fixture::require_application_input_error_code(
      [&] { static_cast<void>(fixture::match_with_roster(entries)); },
      ApplicationInputErrorCode::kMatchBotRosterTooLarge);
}

TEST_CASE(
    "profile sections alone are valid in every mode but startup selections require lobby seats",
    "[unit][application][config][tactical][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  for (const auto& input : fixture::kModeAdmissions) {
    CAPTURE(input.mode);
    const ApplicationConfig profiles_only =
        fixture::load(workspace, fixture::configuration({}, input.mode));
    CHECK(profiles_only.tactical_profiles().profiles().size() == 1);
    CHECK(gameplay::GameModeRegistry::create(input.mode, profiles_only.game_mode_configuration())
              ->accepted_command_kinds()
              .contains(simulation::CommandKind::kStartMatch) == input.admits_profiled_roster);
    if (input.admits_profiled_roster) {
      const ApplicationConfig selected =
          fixture::load(workspace, fixture::configuration(fixture::kProfileRoster, input.mode));
      CHECK(selected.match_configuration().mode_name() == input.mode);
      CHECK(selected.game_mode_configuration() == profiles_only.game_mode_configuration());
      CHECK(selected.match_configuration().bot_roster().size() == 1);
    } else {
      test_fixture::require_application_input_error_code(
          [&] {
            static_cast<void>(fixture::load(
                workspace, fixture::configuration(fixture::kProfileRoster, input.mode)));
          },
          ApplicationInputErrorCode::kMatchBotProfileModeUnsupported);
    }
  }
}

TEST_CASE("direct application aggregates reject unknown profile selection and unsupported mode",
          "[unit][application][config][tactical][validation]") {
  TemporaryApplicationInputWorkspace workspace;
  const ApplicationConfig baseline = fixture::load(workspace, fixture::configuration());
  const auto roster = MatchConfiguration::parse_bot_roster(fixture::kProfileRoster);
  test_fixture::require_application_input_error_code(
      [&] {
        static_cast<void>(
            fixture::application_with_match(baseline, fixture::match_with_roster(roster), {}));
      },
      ApplicationInputErrorCode::kMatchBotProfileUnknown);
  for (const auto& input : fixture::kModeAdmissions) {
    CAPTURE(input.mode);
    const auto aggregate = [&] {
      return fixture::application_with_match(
          baseline, fixture::match_with_roster(roster, input.mode), baseline.tactical_profiles());
    };
    if (input.admits_profiled_roster) {
      CHECK_NOTHROW(aggregate());
    } else {
      test_fixture::require_application_input_error_code(
          [&] { static_cast<void>(aggregate()); },
          ApplicationInputErrorCode::kMatchBotProfileModeUnsupported);
    }
  }
}

} // namespace
} // namespace blob_royale::application
