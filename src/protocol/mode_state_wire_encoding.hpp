#ifndef BLOB_ROYALE_PROTOCOL_MODE_STATE_WIRE_ENCODING_HPP
#define BLOB_ROYALE_PROTOCOL_MODE_STATE_WIRE_ENCODING_HPP

#include "component_encoding.hpp"
#include "component_wire_bound.hpp"
#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"
#include "protocol_v2_constants.hpp"

#include "controller_id.hpp"
#include "entity_id.hpp"
#include "match_phase.hpp"
#include "mode_match_state_registry.hpp"
#include "mode_states/king_of_the_hill_mode_state.hpp"
#include "mode_states/no_mode_state.hpp"
#include "mode_states/race_mode_state.hpp"
#include "mode_states/royale_placements_mode_state.hpp"
#include "tick_sequence.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace blob_royale::protocol {

// One mode's contribution to the generic `match.placements` array, before the encoding boundary
// resolves the controller each entry also carries on the wire.
//
// **Placements are generic on the wire and mode-owned in the world.** They describe entities that
// no longer exist, so they are not entity-shaped and cannot be components, which is why
// `docs/protocol/v2.md` § "Alternatives considered" publishes them in `match` while refusing to
// publish the zone there. This value is the seam: every mode-state arm contributes its ranking in
// this one shape and the encoder writes one array.
struct ModeStatePlacement final {
  simulation::EntityId entity;
  // Carried by the mode-state arm itself. The tick that records a placement destroys the entity in
  // the same tick, so nothing outside the recorded value can recover the link
  // (`src/simulation/mode_states/royale_placements_mode_state.hpp`).
  simulation::ControllerId controller;
  std::uint64_t placement{};
  simulation::TickSequence eliminated_tick;

  friend bool operator==(const ModeStatePlacement&, const ModeStatePlacement&) = default;
};

// canonical: mode_state_wire_encoding -- what one mode-state block publishes on the v2 wire.
// @extension-point snapshot_mode_state
//
// Declared and never defined, exactly like `simulation::ModeMatchStateSchemaId`, so an arm added to
// `ModeMatchState` without a wire encoding fails to compile at the snapshot encoder rather than
// publishing an unregistered schema id every client must close on.
//
// The wire schema id is **not** the simulation's schema id. The world calls royale's block
// `royale_placements` because that is what it holds; the wire calls it
// `blob-royale://protocol/v2/mode-state/royale` because the wire block is the mode's state minus
// the ranking the generic `match.placements` array already carries. Mapping here rather than
// renaming either side keeps both names accurate.
//
// Adding a mode-state block end to end:
//
//   new  src/simulation/mode_states/<schema>_mode_state.hpp     the value struct
//   edit src/simulation/mode_match_state_registry.hpp           one variant arm, one schema id
//   edit src/protocol/mode_state_wire_encoding.hpp              this specialization
//   new  docs/protocol/schema/v2/<mode>-mode-state.schema.json  the closed wire shape
//   edit docs/protocol/schema/v2/common.schema.json             one mode_state_schema_id member
//   edit docs/protocol/schema/v2/match-data.schema.json         one if/then row
//   edit src/protocol/protocol_v2_constants.hpp                 the id constant
// related: src/simulation/mode_match_state_registry.hpp -- the closed list this mirrors.
template <typename ModeStateType> struct ModeStateWireEncoding;

// A mode whose state is entirely entity-shaped publishes the `none` id and an **empty object**
// rather than a null, so a decoder has one code path instead of a nullable branch
// (`docs/protocol/v2.md` § "snapshot").
template <> struct ModeStateWireEncoding<simulation::NoModeState> {
  static constexpr std::string_view kSchemaId = kNoModeStateSchemaId;

  static void encode_value(const simulation::NoModeState&, ComponentObjectSink&) {}

  static void append_placements(const simulation::NoModeState&, std::vector<ModeStatePlacement>&) {}
};

// Royale's block carries `previous_phase` and `elimination_grace_ticks`: the zone is the `Zone`
// component of the zone entity and the ordered ranking travels in the generic `match.placements`
// array (`docs/architecture/0005-royale-mode.md` § "Match section fields").
//
// `elimination_grace_ticks` is the one member here that is mode *configuration* rather than
// observed match state, and it is published for a reason the counter it bounds makes plain: every
// snapshot already carries `zone_exposure.outside_ticks`, and a consecutive-tick counter without
// its bound cannot be turned into "how long do I have". It rides the snapshot rather than the
// welcome frame because `welcome-data.schema.json` is mode-agnostic -- it names `mode` and `map`
// and nothing a mode owns -- and because a value carried per frame is a value a recorded frame
// still has, which is where a countdown has to keep working.
template <> struct ModeStateWireEncoding<simulation::RoyalePlacementsModeState> {
  static constexpr std::string_view kSchemaId = kRoyaleModeStateSchemaId;

  // Member order is the order `docs/protocol/v2.md` § "Object member order" declares for this
  // block, which is the order they are written here.
  static void encode_value(const simulation::RoyalePlacementsModeState& mode_state,
                           ComponentObjectSink& sink) {
    sink.set_string("previous_phase", simulation::match_phase_name(mode_state.previous_phase));
    sink.set_unsigned("elimination_grace_ticks", mode_state.elimination_grace_ticks);
  }

  static void append_placements(const simulation::RoyalePlacementsModeState& mode_state,
                                std::vector<ModeStatePlacement>& placements) {
    placements.reserve(placements.size() + mode_state.placements.size());
    for (const simulation::RoyalePlacement& placement : mode_state.placements) {
      placements.push_back(ModeStatePlacement{placement.entity, placement.controller,
                                              placement.placement, placement.elimination_tick});
    }
  }
};

// King of the hill's block is three declared constants and nothing observed: the denominators a
// client needs to read the scores, the presence counters, and the clock every frame already
// carries. Scores are `score` components, the hill is the `hill` component of the hill entity, and
// nothing of the hill's rides `match.placements`
// (`docs/architecture/0007-king-of-the-hill-and-race-modes.md` § "Mode state and the wire"). Added
// in 2.5.
template <> struct ModeStateWireEncoding<simulation::KingOfTheHillModeState> {
  static constexpr std::string_view kSchemaId = kKingOfTheHillModeStateSchemaId;

  // Member order is the order `docs/protocol/v2.md` § "Object member order" declares.
  static void encode_value(const simulation::KingOfTheHillModeState& mode_state,
                           ComponentObjectSink& sink) {
    sink.set_unsigned("points_to_win", mode_state.points_to_win);
    sink.set_unsigned("point_interval_ticks", mode_state.point_interval_ticks);
    sink.set_unsigned("time_limit_ticks", mode_state.time_limit_ticks);
  }

  static void append_placements(const simulation::KingOfTheHillModeState&,
                                std::vector<ModeStatePlacement>&) {}
};

// Race carries its declared course and durations plus finishes. Its generic placements stay
// empty because a finish is not an elimination; the controller is recorded in each standing.
// Added under the open 2.5 minor, in ADR 0007's declared member order.
template <> struct ModeStateWireEncoding<simulation::RaceModeState> {
  static constexpr std::string_view kSchemaId = kRaceModeStateSchemaId;

  static void encode_value(const simulation::RaceModeState& mode_state, ComponentObjectSink& sink) {
    static_assert(kRaceCoursePointLimit == simulation::kMaximumMapMarkerCount);
    require_positive_world_scalar(mode_state.track_half_width,
                                  "snapshot_message.data.match.mode_state.value.track_half_width");
    require_positive_world_scalar(mode_state.checkpoint_radius,
                                  "snapshot_message.data.match.mode_state.value.checkpoint_radius");
    require_value(mode_state.checkpoint_radius <= mode_state.track_half_width, "checkpoint_radius",
                  "checkpoint radius must be no greater than track half-width");
    require_value(mode_state.track.size() >= 2 && mode_state.track.size() <= kRaceCoursePointLimit,
                  "track", "track must contain 2 to 4096 points");
    require_value(!mode_state.checkpoints.empty() &&
                      mode_state.checkpoints.size() <= kRaceCoursePointLimit,
                  "checkpoints", "checkpoints must contain 1 to 4096 points");
    require_value(mode_state.time_limit_ticks <= kMaximumSafeInteger, "time_limit_ticks",
                  "time limit must be a nonnegative safe integer");
    require_value(mode_state.finish_window_ticks <= kMaximumSafeInteger, "finish_window_ticks",
                  "finish window must be a nonnegative safe integer");
    if (mode_state.standings.size() > kMatchPlacementLimit) {
      throw ProtocolEncodingError{ProtocolEncodingErrorCode::kPlacementLimitExceeded,
                                  "snapshot_message.data.match.mode_state.value.standings",
                                  "standing count exceeds the accepted protocol v2 ranking limit"};
    }
    sink.set_number("track_half_width", mode_state.track_half_width);
    sink.set_number("checkpoint_radius", mode_state.checkpoint_radius);
    const auto encode_points = [&sink](const std::string_view name,
                                       const std::vector<simulation::Vector2>& points) {
      sink.set_object_array(name, points.size(),
                            [&points](const std::size_t index, ComponentObjectSink& entry) {
                              entry.set_number("x", points[index].x());
                              entry.set_number("y", points[index].y());
                            });
    };
    encode_points("track", mode_state.track);
    encode_points("checkpoints", mode_state.checkpoints);
    sink.set_unsigned("time_limit_ticks", mode_state.time_limit_ticks);
    sink.set_unsigned("finish_window_ticks", mode_state.finish_window_ticks);
    sink.set_object_array(
        "standings", mode_state.standings.size(),
        [&mode_state](const std::size_t index, ComponentObjectSink& entry) {
          const simulation::RaceStanding& standing = mode_state.standings[index];
          const std::string context = "standings[" + std::to_string(index) + "]";
          require_value(standing.placement >= 1 && standing.placement <= kMatchPlacementLimit,
                        context + ".placement",
                        "placement must be in the inclusive range 1 to 1024");
          if (standing.finished_tick.value() == 0) {
            throw ProtocolEncodingError{ProtocolEncodingErrorCode::kSnapshotTickOutOfRange,
                                        "snapshot_message.data.match.mode_state.value." + context +
                                            ".finished_tick",
                                        "finished tick must be in the inclusive range 1 to 2^53-1"};
          }
          entry.set_unsigned("entity_id", standing.entity.value());
          entry.set_unsigned("controller_id", standing.controller.value());
          entry.set_unsigned("placement", standing.placement);
          entry.set_unsigned("finished_tick", standing.finished_tick.value());
        });
  }

  static void append_placements(const simulation::RaceModeState&,
                                std::vector<ModeStatePlacement>&) {}

private:
  static void require_value(const bool valid, const std::string_view member,
                            const std::string_view detail) {
    if (!valid) {
      throw ProtocolEncodingError{ProtocolEncodingErrorCode::kComponentValueOutOfRange,
                                  "snapshot_message.data.match.mode_state.value." +
                                      std::string{member},
                                  std::string{detail}};
    }
  }
};

// The wire schema id of the held block. Total over the closed variant and generated from the
// specializations, so a new arm cannot silently answer with an existing id.
[[nodiscard]] inline std::string_view
mode_state_wire_schema_id_of(const simulation::ModeMatchState& mode_state) noexcept {
  return std::visit(
      []<typename ModeStateType>(const ModeStateType&) -> std::string_view {
        return ModeStateWireEncoding<ModeStateType>::kSchemaId;
      },
      mode_state);
}

// The held block's `value` members, in declared order.
inline void encode_mode_state_value(const simulation::ModeMatchState& mode_state,
                                    ComponentObjectSink& sink) {
  std::visit(
      [&sink]<typename ModeStateType>(const ModeStateType& held) {
        ModeStateWireEncoding<ModeStateType>::encode_value(held, sink);
      },
      mode_state);
}

// The held block's contribution to the generic `match.placements` array, in recorded order.
inline void append_mode_state_placements(const simulation::ModeMatchState& mode_state,
                                         std::vector<ModeStatePlacement>& placements) {
  std::visit(
      [&placements]<typename ModeStateType>(const ModeStateType& held) {
        ModeStateWireEncoding<ModeStateType>::append_placements(held, placements);
      },
      mode_state);
}

} // namespace blob_royale::protocol

#endif
