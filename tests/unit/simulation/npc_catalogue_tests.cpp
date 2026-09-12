#include "fixtures/npc_declaration_fixture.hpp"

#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::npc_declaration_fixture;

namespace {
template <typename Action>
void require_code(Action&& action, const simulation::SimulationValidationCode code) {
  try {
    action();
    FAIL("invalid catalogue must fail before publication or admission");
  } catch (const simulation::SimulationValidationError& error) {
    CHECK(error.validation_code() == code);
  }
}
} // namespace

static_assert(!std::is_default_constructible_v<simulation::BotProfileName>);

TEST_CASE("Bot profile identity owns bounded lower snake case names without an empty sentinel",
          "[unit][simulation][npc_catalogue]") {
  for (const auto invalid : fixture::kInvalidNames) {
    CAPTURE(invalid);
    require_code([&] { static_cast<void>(simulation::BotProfileName::create(invalid)); },
                 simulation::SimulationValidationCode::kBotProfileNameInvalid);
  }
  const std::string maximum(simulation::kMaximumKindNameLength, 'a');
  CHECK(simulation::BotProfileName::create(maximum) == std::string_view{maximum});
  require_code([&] { static_cast<void>(simulation::BotProfileName::create(maximum + "a")); },
               simulation::SimulationValidationCode::kBotProfileNameInvalid);
}

TEST_CASE("NPC catalogue preserves declaration order and exact optional profile membership",
          "[unit][simulation][npc_catalogue]") {
  const auto catalogue = fixture::catalogue();
  REQUIRE(catalogue.unprofiled_kinds().size() == 2);
  CHECK(catalogue.unprofiled_kinds()[0] == fixture::kPlainKind);
  REQUIRE(catalogue.profiles().size() == 2);
  CHECK(catalogue.profiles()[0] == fixture::profiled());
  CHECK(catalogue.profiles()[1] == fixture::profiled(fixture::kOtherProfileName));
  CHECK(catalogue.contains(fixture::plain()));
  CHECK(catalogue.contains(fixture::profiled()));
  CHECK_FALSE(catalogue.contains(fixture::kProfileKind));
  CHECK_FALSE(catalogue.contains(fixture::kPlainKind, fixture::kProfileName));
  CHECK_FALSE(catalogue.contains(fixture::kProfileKind, "unknown"));
  CHECK_FALSE(simulation::NpcCatalogue::empty().contains(fixture::plain()));
}

TEST_CASE("NPC catalogue admits both inclusive partition budgets and refuses excess",
          "[unit][simulation][npc_catalogue]") {
  const auto maximum = simulation::NpcCatalogue::create(
      fixture::plain_names(simulation::kMaximumUnprofiledNpcKindCount),
      fixture::profile_names(simulation::kMaximumNpcProfileCount));
  CHECK(maximum.unprofiled_kinds().size() + maximum.profiles().size() ==
        simulation::kMaximumNpcCatalogueChoiceCount);
  require_code(
      [&] {
        static_cast<void>(simulation::NpcCatalogue::create(
            fixture::plain_names(simulation::kMaximumUnprofiledNpcKindCount + 1)));
      },
      simulation::SimulationValidationCode::kNpcCatalogueLimitExceeded);
  require_code(
      [&] {
        static_cast<void>(simulation::NpcCatalogue::create(
            {}, fixture::profile_names(simulation::kMaximumNpcProfileCount + 1)));
      },
      simulation::SimulationValidationCode::kNpcCatalogueLimitExceeded);
}

TEST_CASE("NPC catalogue rejects duplicates malformed names missing profiles and mixed kinds",
          "[unit][simulation][npc_catalogue]") {
  require_code(
      [] { static_cast<void>(simulation::NpcCatalogue::create({"wanderer", "wanderer"})); },
      simulation::SimulationValidationCode::kNpcCatalogueDuplicate);
  require_code(
      [] {
        static_cast<void>(
            simulation::NpcCatalogue::create({}, {fixture::profiled(), fixture::profiled()}));
      },
      simulation::SimulationValidationCode::kNpcCatalogueDuplicate);
  require_code(
      [] {
        static_cast<void>(simulation::NpcCatalogue::create({"tactical"}, {fixture::profiled()}));
      },
      simulation::SimulationValidationCode::kNpcCatalogueMixedKind);
  require_code([] { static_cast<void>(simulation::NpcCatalogue::create({}, {fixture::plain()})); },
               simulation::SimulationValidationCode::kNpcCatalogueProfileMissing);
  for (const auto invalid : fixture::kInvalidNames) {
    require_code(
        [&] { static_cast<void>(simulation::NpcCatalogue::create({std::string{invalid}})); },
        simulation::SimulationValidationCode::kSeatKindNameInvalid);
  }
  require_code(
      [] {
        static_cast<void>(simulation::NpcCatalogue::create(
            {}, {simulation::NpcDeclaration{
                    simulation::SeatKindName{},
                    simulation::BotProfileName::create(fixture::kProfileName)}}));
      },
      simulation::SimulationValidationCode::kSeatKindNameInvalid);
}
