#include "bounded_name.hpp"
#include "contact_rule_name.hpp"
#include "contact_rule_name_policy.hpp"
#include "fixtures/bounded_name_fixture.hpp"
#include "fixtures/bounded_name_frozen_reference.hpp"
#include "race_road_name.hpp"
#include "seat_kind_name_policy.hpp"
#include "seat_roster.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::bounded_name_fixture;
namespace frozen = blob_royale::testing::bounded_name_reference;

namespace {

using CandidateSeatName = simulation::BoundedName<simulation::SeatKindNamePolicy>;
using CandidateContactName = simulation::BoundedName<simulation::ContactRuleNamePolicy>;
using RequiredName = simulation::BoundedName<fixture::RequiredNamePolicy>;

template <typename Name>
concept HasMutableTemporaryView = requires(Name name) { std::move(name).value(); };

template <typename Name>
concept HasConstTemporaryView = requires(const Name name) { std::move(name).value(); };

template <typename Left, typename Right>
concept HasCrossNameEquality = requires(const Left left, const Right right) { left == right; };

template <typename Name> constexpr bool preserves_value_type_contract() {
  return std::same_as<decltype(std::declval<const Name&>().value()), std::string_view> &&
         noexcept(std::declval<const Name&>().value()) &&
         noexcept(std::declval<const Name&>().empty()) && !HasMutableTemporaryView<Name> &&
         !HasConstTemporaryView<Name> && !std::is_constructible_v<Name, std::string_view> &&
         !std::is_convertible_v<Name, std::string_view> && std::is_copy_constructible_v<Name> &&
         std::is_copy_assignable_v<Name> && std::is_nothrow_move_constructible_v<Name> &&
         std::is_nothrow_move_assignable_v<Name> && std::is_nothrow_destructible_v<Name>;
}

static_assert(preserves_value_type_contract<simulation::SeatKindName>());
static_assert(preserves_value_type_contract<simulation::ContactRuleName>());
static_assert(preserves_value_type_contract<simulation::RaceRoadName>());
static_assert(preserves_value_type_contract<frozen::SeatKindName>());
static_assert(preserves_value_type_contract<frozen::ContactRuleName>());
static_assert(preserves_value_type_contract<CandidateSeatName>());
static_assert(preserves_value_type_contract<CandidateContactName>());
static_assert(preserves_value_type_contract<RequiredName>());

static_assert(std::is_default_constructible_v<simulation::SeatKindName>);
static_assert(std::is_default_constructible_v<simulation::ContactRuleName>);
static_assert(std::is_default_constructible_v<frozen::SeatKindName>);
static_assert(std::is_default_constructible_v<frozen::ContactRuleName>);
static_assert(std::is_default_constructible_v<CandidateSeatName>);
static_assert(std::is_default_constructible_v<CandidateContactName>);
static_assert(!std::is_default_constructible_v<RequiredName>);
static_assert(!std::is_default_constructible_v<simulation::RaceRoadName>);
static_assert(std::is_same_v<simulation::SeatKindName, CandidateSeatName>);
static_assert(std::is_same_v<simulation::ContactRuleName, CandidateContactName>);
static_assert(!std::is_same_v<CandidateSeatName, CandidateContactName>);
static_assert(!std::is_same_v<CandidateSeatName, RequiredName>);
static_assert(!std::is_convertible_v<CandidateSeatName, CandidateContactName>);
static_assert(!std::is_convertible_v<CandidateContactName, CandidateSeatName>);
static_assert(!std::is_constructible_v<RequiredName, CandidateSeatName>);
static_assert(!HasCrossNameEquality<CandidateSeatName, CandidateContactName>);
static_assert(!std::is_same_v<simulation::RaceRoadName, CandidateSeatName>);
static_assert(!std::is_same_v<simulation::RaceRoadName, CandidateContactName>);
static_assert(!std::is_constructible_v<simulation::RaceRoadName, CandidateSeatName>);
static_assert(!std::is_constructible_v<simulation::RaceRoadName, CandidateContactName>);
static_assert(!std::is_constructible_v<CandidateSeatName, simulation::RaceRoadName>);
static_assert(!std::is_constructible_v<CandidateContactName, simulation::RaceRoadName>);
static_assert(!HasCrossNameEquality<simulation::RaceRoadName, CandidateSeatName>);
static_assert(!HasCrossNameEquality<simulation::RaceRoadName, CandidateContactName>);

static_assert(simulation::SeatKindName::kCapacity == frozen::SeatKindName::kCapacity);
static_assert(simulation::ContactRuleName::kCapacity == frozen::ContactRuleName::kCapacity);
static_assert(CandidateSeatName::kCapacity == frozen::SeatKindName::kCapacity);
static_assert(CandidateContactName::kCapacity == frozen::ContactRuleName::kCapacity);
static_assert(simulation::RaceRoadName::kCapacity == frozen::kMaximumKindNameLength);

template <typename Name>
[[nodiscard]] fixture::Rejection rejection_from(const std::string_view value) {
  std::optional<fixture::Rejection> result;
  try {
    static_cast<void>(Name::create(value));
  } catch (const simulation::SimulationValidationError& error) {
    result.emplace(fixture::Rejection{error.validation_code(), std::string(error.code()),
                                      error.context(), error.detail(), error.what()});
  }
  REQUIRE(result.has_value());
  return *result;
}

template <typename Old, typename Candidate, typename Reference, typename ExpectedRejection>
void check_factory_contract(const ExpectedRejection& expected_rejection) {
  for (const fixture::Input& input : fixture::inputs()) {
    INFO(input.scenario);
    if (input.accepted) {
      const Old old = Old::create(input.value);
      const Candidate candidate = Candidate::create(input.value);
      const Reference reference = Reference::create(input.value);
      CHECK(old.value() == input.value);
      CHECK(reference.value() == input.value);
      CHECK(candidate.value() == reference.value());
      CHECK(candidate.value() == old.value());
      CHECK_FALSE(candidate.empty());
    } else {
      const fixture::Rejection expected = expected_rejection(input.value);
      CHECK(rejection_from<Old>(input.value) == expected);
      CHECK(rejection_from<Reference>(input.value) == expected);
      CHECK(rejection_from<Candidate>(input.value) == expected);
    }
  }
}

template <typename Name> void check_owned_value_contract() {
  const Name owned = [] {
    std::string source(fixture::kOwnedName);
    Name name = Name::create(source);
    CHECK(name.value().data() != source.data());
    source.assign(source.size(), 'z');
    CHECK(name.value() == fixture::kOwnedName);
    return name;
  }();
  CHECK(owned.value() == fixture::kOwnedName);

  const Name temporary_source = Name::create(std::string(fixture::kOwnedName));
  CHECK(temporary_source == owned);
  CHECK(temporary_source.value().data() != owned.value().data());

  const Name copied(owned);
  CHECK(copied == owned);
  CHECK(copied.value().data() != owned.value().data());

  Name move_source(copied);
  const Name moved(std::move(move_source));
  CHECK(moved == owned);
  CHECK(move_source == owned);
  CHECK(moved.value().data() != move_source.value().data());

  const Name short_name = Name::create(fixture::kShortName);
  Name assigned = Name::create(fixture::maximum_name());
  assigned = short_name;
  CHECK(assigned == Name::create(fixture::kShortName));
  CHECK(assigned.value().data() != short_name.value().data());

  Name move_assigned = Name::create(fixture::maximum_name());
  Name short_move_source = Name::create(fixture::kShortName);
  move_assigned = std::move(short_move_source);
  CHECK(move_assigned == Name::create(fixture::kShortName));
  CHECK(short_move_source == short_name);
  CHECK(move_assigned.value().data() != short_move_source.value().data());

  CHECK(short_name == fixture::kShortName);
  CHECK(fixture::kShortName == short_name);
  CHECK_FALSE(short_name == Name::create(fixture::kDifferentName));
  CHECK_FALSE(short_name == fixture::kDifferentName);
  CHECK_FALSE(short_name.empty());
}

template <typename Name> void check_empty_default_contract() {
  const Name empty;
  CHECK(empty.empty());
  CHECK(empty.value().empty());
  CHECK(empty == std::string_view{});
  CHECK(empty == Name{});
  CHECK_FALSE(empty == Name::create(fixture::kShortName));

  Name assigned = Name::create(fixture::maximum_name());
  assigned = empty;
  CHECK(assigned == Name{});
  CHECK(assigned.empty());

  Name move_assigned = Name::create(fixture::maximum_name());
  move_assigned = Name{};
  CHECK(move_assigned == Name{});
  CHECK(move_assigned.empty());
}

} // namespace

TEST_CASE("bounded seat-name candidate preserves the old factory and frozen exact diagnostics",
          "[unit][simulation][bounded_name][promotion]") {
  check_factory_contract<simulation::SeatKindName, CandidateSeatName, frozen::SeatKindName>(
      fixture::seat_rejection);
}

TEST_CASE("bounded contact-name candidate preserves the old factory and frozen exact diagnostics",
          "[unit][simulation][bounded_name][promotion]") {
  check_factory_contract<simulation::ContactRuleName, CandidateContactName,
                         frozen::ContactRuleName>(fixture::contact_rejection);
}

TEST_CASE("bounded seat-name candidate preserves owned lifetime copy move and zero-tail equality",
          "[unit][simulation][bounded_name][promotion]") {
  check_owned_value_contract<simulation::SeatKindName>();
  check_owned_value_contract<frozen::SeatKindName>();
  check_owned_value_contract<CandidateSeatName>();
}

TEST_CASE(
    "bounded contact-name candidate preserves owned lifetime copy move and zero-tail equality",
    "[unit][simulation][bounded_name][promotion]") {
  check_owned_value_contract<simulation::ContactRuleName>();
  check_owned_value_contract<frozen::ContactRuleName>();
  check_owned_value_contract<CandidateContactName>();
}

TEST_CASE("bounded name candidate preserves both existing empty-default sentinels",
          "[unit][simulation][bounded_name][promotion]") {
  check_empty_default_contract<simulation::SeatKindName>();
  check_empty_default_contract<simulation::ContactRuleName>();
  check_empty_default_contract<frozen::SeatKindName>();
  check_empty_default_contract<frozen::ContactRuleName>();
  check_empty_default_contract<CandidateSeatName>();
  check_empty_default_contract<CandidateContactName>();
}

TEST_CASE("required bounded-name policy admits only validated construction and keeps owned moves",
          "[unit][simulation][bounded_name][promotion]") {
  check_owned_value_contract<RequiredName>();
  for (const fixture::Input& input : fixture::inputs()) {
    INFO(input.scenario);
    if (input.accepted) {
      const RequiredName name = RequiredName::create(input.value);
      CHECK(name.value() == input.value);
      CHECK_FALSE(name.empty());
    } else {
      CHECK(rejection_from<RequiredName>(input.value) == fixture::seat_rejection(input.value));
    }
  }
}

TEST_CASE("required race-road name accepts bounded wire identities with road-domain diagnostics",
          "[unit][simulation][bounded_name][race_road_name]") {
  for (const fixture::Input& input : fixture::inputs()) {
    INFO(input.scenario);
    if (input.accepted) {
      const simulation::RaceRoadName name = simulation::RaceRoadName::create(input.value);
      CHECK(name.value() == input.value);
      CHECK_FALSE(name.empty());
    } else {
      CHECK(rejection_from<simulation::RaceRoadName>(input.value) ==
            fixture::road_rejection(input.value));
    }
  }
}

TEST_CASE("required race-road name owns copied moved and assigned bytes without an empty sentinel",
          "[unit][simulation][bounded_name][race_road_name]") {
  check_owned_value_contract<simulation::RaceRoadName>();
}
