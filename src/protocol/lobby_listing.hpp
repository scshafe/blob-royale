#ifndef BLOB_ROYALE_PROTOCOL_LOBBY_LISTING_HPP
#define BLOB_ROYALE_PROTOCOL_LOBBY_LISTING_HPP

#include "match_phase.hpp"

#include <cstdint>
#include <string>

namespace blob_royale::protocol {

// canonical: lobby_listing -- one room as `GET /api/v2/lobbies` publishes it.
//
// A plain value the server fills from a room's latest snapshot and its admitted-session count, in
// the members and order `lobby-directory-data.schema.json#/$defs/lobby_listing` names. The
// encoder validates every bound before it emits a byte, so a directory that could not validate is
// an encoding failure here rather than a document a client refuses (`docs/protocol/v2.md`
// § "The lobby directory"). `tick_sequence` zero is the one non-snapshot value: a room that has
// published no tick, because its runtime is not ready or has failed, lists as unhealthy with every
// count zero.
struct LobbyListing final {
  std::uint64_t lobby_id;
  std::string mode_name;
  std::string map_name;
  simulation::MatchPhase phase;
  std::uint64_t phase_started_tick;
  std::uint64_t tick_sequence;
  std::uint64_t seat_count;
  std::uint64_t seat_count_maximum;
  std::uint64_t filled_seat_count;
  std::uint64_t npc_seat_count;
  std::uint64_t session_count;
  bool healthy;

  friend bool operator==(const LobbyListing&, const LobbyListing&) = default;
};

} // namespace blob_royale::protocol

#endif
