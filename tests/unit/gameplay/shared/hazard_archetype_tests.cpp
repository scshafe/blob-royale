#include "shared/hazard_archetype.hpp"

#include "gameplay_validation_error.hpp"
#include "simulation_validation_error.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <limits>
#include <string>
#include <string_view>

namespace gameplay = blob_royale::gameplay;
namespace simulation = blob_royale::simulation;

namespace {

// A hazard has no defaults to fall back on, because there is no default kind: a kind exists only
// because a section declared it. So the fixture is a section this file authors, not a
// `default_section()` the value ships, and every rejection below is one mutation of it.
[[nodiscard]] gameplay::HazardArchetype::Section valid_section() {
  return gameplay::HazardArchetype::Section{.kind_name = "comet",
                                            .radius_world_units = 10.0,
                                            .mass = 1.0,
                                            .restitution = 1.0,
                                            .speed_world_units_per_second = 260.0,
                                            .spawn_interval_seconds = 6.0,
                                            .lethal_on_contact = true,
                                            .contact_effect_policy =
                                                simulation::ContactEffectPolicy::kClosingImpact};
}

[[nodiscard]] gameplay::GameplayValidationCode
rejection_code_of(const gameplay::HazardArchetype::Section& section) {
  try {
    static_cast<void>(gameplay::HazardArchetype::create(section));
  } catch (const gameplay::GameplayValidationError& error) {
    return error.validation_code();
  }
  FAIL("the section was accepted");
  return gameplay::GameplayValidationCode::kGameModeNameUnknown;
}

[[nodiscard]] std::string rejection_context_of(const gameplay::HazardArchetype::Section& section) {
  try {
    static_cast<void>(gameplay::HazardArchetype::create(section));
  } catch (const gameplay::GameplayValidationError& error) {
    return error.context();
  }
  FAIL("the section was accepted");
  return {};
}

// The three scalars that share one rule, named by the key an operator would have to go and edit.
struct PositiveScalar final {
  std::string_view key;
  double gameplay::HazardArchetype::Section::*member;
};

constexpr std::array<PositiveScalar, 3> kPositiveScalars = {
    {{"radius_world_units", &gameplay::HazardArchetype::Section::radius_world_units},
     {"mass", &gameplay::HazardArchetype::Section::mass},
     {"speed_world_units_per_second",
      &gameplay::HazardArchetype::Section::speed_world_units_per_second}}};

} // namespace

TEST_CASE("a declared hazard kind becomes the archetype it authored",
          "[unit][gameplay][hazard][configuration]") {
  const gameplay::HazardArchetype archetype = gameplay::HazardArchetype::create(valid_section());

  CHECK(archetype.kind_name() == "comet");
  CHECK(archetype.radius() == 10.0);
  CHECK(archetype.mass() == 1.0);
  CHECK(archetype.restitution() == 1.0);
  CHECK(archetype.speed() == 260.0);
  CHECK(archetype.lethal_on_contact());
  CHECK(archetype.contact_effect_policy() == simulation::ContactEffectPolicy::kClosingImpact);
  // The one authored duration arrives already in ticks, so no spawner ever sees a value in seconds
  // and nothing multiplies by the tick rate at runtime: 6 s at 400 ticks per second.
  CHECK(archetype.spawn_interval_ticks() == 2'400);
}

TEST_CASE("hazard archetypes validate and retain their explicit contact effect default",
          "[unit][gameplay][hazard][configuration][contact_effect_admission]") {
  auto section = valid_section();
  section.contact_effect_policy = simulation::ContactEffectPolicy::kAnyTouch;
  CHECK(gameplay::HazardArchetype::create(section).contact_effect_policy() ==
        simulation::ContactEffectPolicy::kAnyTouch);
  CHECK(gameplay::HazardArchetype::create(section) !=
        gameplay::HazardArchetype::create(valid_section()));
  section.contact_effect_policy = static_cast<simulation::ContactEffectPolicy>(73);
  CHECK_THROWS_AS(gameplay::HazardArchetype::create(section),
                  simulation::SimulationValidationError);
}

TEST_CASE("restitution is accepted across its whole closed interval and refused outside it",
          "[unit][gameplay][hazard][configuration][validation]") {
  // `1` is the accepted perfectly elastic baseline and `0` is the fully damped contact, and both
  // ends are legal values a designer will reach for -- a boulder that swallows the bounce is `0`.
  // Only outside `[0, 1]` is a rejection, because a contact cannot absorb less than none of the
  // closing speed or return more than all of it.
  gameplay::HazardArchetype::Section fully_damped = valid_section();
  fully_damped.restitution = 0.0;
  CHECK(gameplay::HazardArchetype::create(fully_damped).restitution() == 0.0);

  gameplay::HazardArchetype::Section perfectly_elastic = valid_section();
  perfectly_elastic.restitution = 1.0;
  CHECK(gameplay::HazardArchetype::create(perfectly_elastic).restitution() == 1.0);

  gameplay::HazardArchetype::Section below = valid_section();
  below.restitution = -0.0001;
  CHECK(rejection_code_of(below) == gameplay::GameplayValidationCode::kHazardScalarOutOfRange);
  CHECK(rejection_context_of(below) == "hazard.comet.restitution");

  gameplay::HazardArchetype::Section above = valid_section();
  above.restitution = 1.0001;
  CHECK(rejection_code_of(above) == gameplay::GameplayValidationCode::kHazardScalarOutOfRange);

  gameplay::HazardArchetype::Section not_finite = valid_section();
  not_finite.restitution = std::numeric_limits<double>::quiet_NaN();
  CHECK(rejection_code_of(not_finite) == gameplay::GameplayValidationCode::kHazardScalarNotFinite);
}

TEST_CASE("radius, mass, and speed must be finite and strictly positive",
          "[unit][gameplay][hazard][configuration][validation]") {
  // Zero is a rejection rather than a degenerate accepted value: a hazard with no size, no mass, or
  // no speed is not a hazard, and a zero mass would divide by zero in the impulse equation the body
  // this archetype describes is headed for.
  for (const PositiveScalar& scalar : kPositiveScalars) {
    CAPTURE(scalar.key);
    const std::string expected_context = "hazard.comet." + std::string{scalar.key};

    for (const double refused : {0.0, -1.0}) {
      CAPTURE(refused);
      gameplay::HazardArchetype::Section section = valid_section();
      section.*scalar.member = refused;
      CHECK(rejection_code_of(section) ==
            gameplay::GameplayValidationCode::kHazardScalarOutOfRange);
      CHECK(rejection_context_of(section) == expected_context);
    }

    for (const double refused :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()}) {
      gameplay::HazardArchetype::Section section = valid_section();
      section.*scalar.member = refused;
      CHECK(rejection_code_of(section) == gameplay::GameplayValidationCode::kHazardScalarNotFinite);
      CHECK(rejection_context_of(section) == expected_context);
    }
  }
}

TEST_CASE("the spawn interval must convert to at least one whole tick",
          "[unit][gameplay][hazard][configuration][validation]") {
  // A quarter of a tick rounds to zero, and a zero interval spawns one body every tick until the
  // entity budget runs out, so the rule is stated in ticks after the conversion rather than in
  // seconds before it: what matters is what the spawner will read.
  gameplay::HazardArchetype::Section sub_tick = valid_section();
  sub_tick.spawn_interval_seconds = 0.001;
  CHECK(rejection_code_of(sub_tick) == gameplay::GameplayValidationCode::kHazardScalarOutOfRange);
  CHECK(rejection_context_of(sub_tick) == "hazard.comet.spawn_interval_seconds");

  gameplay::HazardArchetype::Section one_tick = valid_section();
  one_tick.spawn_interval_seconds = 0.002;
  CHECK(gameplay::HazardArchetype::create(one_tick).spawn_interval_ticks() == 1);

  // The finite and not-negative rules belong to the shared conversion, so they report its codes
  // under this hazard's own context rather than a second copy of the same rule here.
  gameplay::HazardArchetype::Section negative = valid_section();
  negative.spawn_interval_seconds = -1.0;
  CHECK(rejection_code_of(negative) == gameplay::GameplayValidationCode::kDurationNegative);
  CHECK(rejection_context_of(negative) == "hazard.comet.spawn_interval_seconds");

  gameplay::HazardArchetype::Section not_finite = valid_section();
  not_finite.spawn_interval_seconds = std::numeric_limits<double>::quiet_NaN();
  CHECK(rejection_code_of(not_finite) == gameplay::GameplayValidationCode::kDurationNotFinite);
}

TEST_CASE("a kind name outside the published grammar is refused, naming the section it came from",
          "[unit][gameplay][hazard][configuration][validation]") {
  // The kind name is open, not unchecked. It is `common.schema.json#/$defs/kind_name` because a
  // hazard is meant to be drawn and a name that could never be encoded must fail at startup rather
  // than in a frame every client has to close on. The loader deliberately does not restate this
  // rule, so this is the only place it lives.
  constexpr std::array<std::string_view, 5> refused_names = {"", "Comet", "2comet", "comet.two",
                                                             "comet-two"};
  for (const std::string_view refused : refused_names) {
    CAPTURE(refused);
    gameplay::HazardArchetype::Section section = valid_section();
    section.kind_name = std::string{refused};
    CHECK(rejection_code_of(section) == gameplay::GameplayValidationCode::kHazardKindNameInvalid);
    // The context names the section header, which is what an operator has to go and find.
    CHECK(rejection_context_of(section) == "hazard." + std::string{refused});
  }

  gameplay::HazardArchetype::Section too_long = valid_section();
  too_long.kind_name = std::string(gameplay::HazardArchetype::kMaximumKindNameLength + 1, 'a');
  CHECK(rejection_code_of(too_long) == gameplay::GameplayValidationCode::kHazardKindNameInvalid);

  gameplay::HazardArchetype::Section longest_accepted = valid_section();
  longest_accepted.kind_name = std::string(gameplay::HazardArchetype::kMaximumKindNameLength, 'a');
  // Named rather than called on the temporary, because `kind_name()` deliberately deletes its
  // rvalue overload: the accessor hands out a reference into the archetype.
  const gameplay::HazardArchetype at_the_limit =
      gameplay::HazardArchetype::create(longest_accepted);
  CHECK(at_the_limit.kind_name().size() == gameplay::HazardArchetype::kMaximumKindNameLength);
}

TEST_CASE("two archetypes are equal exactly when every declared value agrees",
          "[unit][gameplay][hazard][configuration]") {
  // `GameModeConfiguration` compares by value, and a hazard table is one of its members, so an
  // archetype that compared by name alone would make two differently tuned configurations look
  // identical to every test that asserts on the whole value.
  gameplay::HazardArchetype::Section heavier = valid_section();
  heavier.mass = 40.0;

  CHECK(gameplay::HazardArchetype::create(valid_section()) ==
        gameplay::HazardArchetype::create(valid_section()));
  CHECK_FALSE(gameplay::HazardArchetype::create(valid_section()) ==
              gameplay::HazardArchetype::create(heavier));
}
