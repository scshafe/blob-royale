#ifndef BLOB_ROYALE_TESTING_RESPAWN_COMPONENT_LIFETIME_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_RESPAWN_COMPONENT_LIFETIME_FIXTURE_HPP

#include "../../simulation/fixtures/component_lifetime_fixture.hpp"

#include "events/elimination_event.hpp"

#include <array>
#include <cstdint>

namespace blob_royale::testing::respawn_component_lifetime_fixture {

namespace lifetime = component_lifetime_fixture;
inline constexpr std::uint64_t kEliminatedPlayer = lifetime::kLivePlayer;
inline constexpr std::uint64_t kExistingBodylessPlayer = 1;
inline constexpr std::uint64_t kBodylessNonparticipant = 9;
inline constexpr std::uint64_t kUnknownEntity = 1000;
inline constexpr std::array<std::uint64_t, 2> kRespawnDelays{0, 3};

[[nodiscard]] inline simulation::GameWorld world_with_duplicate_eliminations() {
  auto world = lifetime::mixed_world();
  // The store-level fixture deliberately exercises timers as persistent values. Live bodies do
  // not carry return timers in production; remove those here before exercising real respawn.
  for (const auto id : lifetime::kLiveEntities) {
    world.mutable_store<simulation::RespawnTimer>().erase(lifetime::entity(id));
  }
  lifetime::attach_bound_components(world, lifetime::entity(kBodylessNonparticipant));
  for (const auto id : {kEliminatedPlayer, kEliminatedPlayer, lifetime::kNonparticipantBody,
                        kExistingBodylessPlayer, kUnknownEntity}) {
    world.emit(simulation::EliminationEvent{lifetime::entity(id)});
  }
  return world;
}

} // namespace blob_royale::testing::respawn_component_lifetime_fixture

#endif
