#include "controllers_validation_error.hpp"
#include "fixtures/tactical_profile_fixture.hpp"
#include "simulation_limits.hpp"

#include <catch2/catch_test_macros.hpp>
#include <set>

namespace controllers = blob_royale::controllers;
namespace simulation = blob_royale::simulation;
namespace fixture = blob_royale::testing::tactical_profile_fixture;

TEST_CASE("Tactical seed derivation matches independent ordered unsigned mixing",
          "[unit][controllers][tactical_seed]") {
  for (const auto match_seed : {std::uint64_t{0}, fixture::kIdentity.match_seed,
                                std::numeric_limits<std::uint64_t>::max()}) {
    for (const auto name : {fixture::kName, fixture::kOtherName, std::string_view{"a_b123"}}) {
      for (const auto running_tick :
           {std::uint64_t{0}, std::uint64_t{13}, simulation::TickSequence::kMaximumValue}) {
        const controllers::TacticalSeedIdentity identity{match_seed, fixture::kIdentity.lobby_id,
                                                         fixture::kIdentity.seat_index};
        const auto typed_name = simulation::BotProfileName::create(name);
        CHECK(controllers::tactical_seed_for(identity, typed_name,
                                             simulation::TickSequence::create(running_tick)) ==
              fixture::frozen_seed(identity, name, running_tick));
      }
    }
  }
}

TEST_CASE(
    "Tactical seed identity varies by each authored axis without depending on catalogue order",
    "[unit][controllers][tactical_seed]") {
  const auto name = simulation::BotProfileName::create(fixture::kName);
  const auto tick = simulation::TickSequence::create(13);
  std::set<std::uint64_t> samples;
  samples.insert(controllers::tactical_seed_for(fixture::kIdentity, name, tick));
  auto changed = fixture::kIdentity;
  ++changed.match_seed;
  samples.insert(controllers::tactical_seed_for(changed, name, tick));
  changed = fixture::kIdentity;
  ++changed.lobby_id;
  samples.insert(controllers::tactical_seed_for(changed, name, tick));
  changed = fixture::kIdentity;
  ++changed.seat_index;
  samples.insert(controllers::tactical_seed_for(changed, name, tick));
  samples.insert(controllers::tactical_seed_for(
      fixture::kIdentity, simulation::BotProfileName::create(fixture::kOtherName), tick));
  samples.insert(controllers::tactical_seed_for(fixture::kIdentity, name, tick.next()));
  CHECK(samples.size() == 6); // This finite fixture is not a universal collision-freedom claim.
  const auto profiles = fixture::profiles(2);
  const auto forward = controllers::TacticalProfileCatalogue::create(profiles);
  const auto reversed = controllers::TacticalProfileCatalogue::create({profiles[1], profiles[0]});
  CHECK(controllers::tactical_seed_for(fixture::kIdentity, forward.profiles()[0].name(), tick) ==
        controllers::tactical_seed_for(fixture::kIdentity, reversed.profiles()[1].name(), tick));
}

TEST_CASE("Tactical seed identity rejects absent lobby and seats outside the shared domain",
          "[unit][controllers][tactical_seed]") {
  CHECK_NOTHROW(controllers::validate_tactical_seed_identity(fixture::kIdentity));
  for (const auto identity :
       {controllers::TacticalSeedIdentity{fixture::kIdentity.match_seed, 0, 0},
        controllers::TacticalSeedIdentity{fixture::kIdentity.match_seed,
                                          simulation::kMaximumProtocolSafeInteger + 1, 0},
        controllers::TacticalSeedIdentity{fixture::kIdentity.match_seed, 1,
                                          simulation::kMaximumLobbySeatCount}}) {
    try {
      controllers::validate_tactical_seed_identity(identity);
      FAIL("invalid authored identity must not construct a tactical bot");
    } catch (const controllers::ControllersValidationError& error) {
      CHECK(error.validation_code() ==
            controllers::ControllersValidationCode::kTacticalSeedIdentityInvalid);
    }
  }
}
