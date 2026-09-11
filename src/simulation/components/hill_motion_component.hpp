#ifndef BLOB_ROYALE_SIMULATION_COMPONENTS_HILL_MOTION_COMPONENT_HPP
#define BLOB_ROYALE_SIMULATION_COMPONENTS_HILL_MOTION_COMPONENT_HPP

#include "component_kind_name.hpp"
#include "component_publication.hpp"
#include "random_stream_registry.hpp"
#include "tick_sequence.hpp"
#include "vector2.hpp"

#include <optional>
#include <string_view>

namespace blob_royale::simulation {

// Private scheduling facts belong to the committed world and its transaction, not the client or
// bots. Generator state remains solely in GameWorld's named random streams.
struct HillMotionSchedule final {
  TickSequence next_retarget_tick;
  RandomStreamKind random_stream;

  friend bool operator==(const HillMotionSchedule&, const HillMotionSchedule&) = default;
};

// canonical: hill_motion_component -- the roaming hill's current velocity and private schedule.
// This non-body-bound state resets explicitly in hill_movement on lobby/countdown ticks. Marker
// tours carry no HillMotion, preserving their existing published entity shape and RNG behavior.
// related: ../../gameplay/king_of_the_hill/hill_movement_system.hpp -- the only writer.
struct HillMotion final {
  Vector2 velocity;
  std::optional<HillMotionSchedule> schedule{};

  friend bool operator==(const HillMotion&, const HillMotion&) = default;
};

template <> struct ComponentKindName<HillMotion> {
  static constexpr std::string_view value = "hill_motion";
};

template <> struct ComponentPublication<HillMotion> {
  [[nodiscard]] static HillMotion published(HillMotion value) { return HillMotion{value.velocity}; }
};

} // namespace blob_royale::simulation

#endif
