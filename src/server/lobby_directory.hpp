#ifndef BLOB_ROYALE_SERVER_LOBBY_DIRECTORY_HPP
#define BLOB_ROYALE_SERVER_LOBBY_DIRECTORY_HPP

#include "match_session_context.hpp"

#include "snapshot_publication.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace blob_royale::server {

// canonical: lobby_entry -- one room as the server sees it.
//
// Exactly what a session needs and nothing a session must not have: the room's read-only
// publication, its write-only `MatchSessionContext`, and a count of the sessions in it. The count
// is the server's one contribution to a room's lifecycle -- the control loop reads it to know when
// a match has been abandoned (`docs/architecture/0006-lobbies-as-rooms.md` § "The lobby
// lifecycle") -- and it is atomic because sessions count themselves in and out on the server
// thread while the control loop reads it on its own.
class LobbyEntry final {
public:
  LobbyEntry(std::uint64_t lobby_id, const runtime::SnapshotPublication& snapshot_publication,
             MatchSessionContext match_session) noexcept;

  LobbyEntry(const LobbyEntry&) = delete;
  LobbyEntry(LobbyEntry&&) = delete;
  LobbyEntry& operator=(const LobbyEntry&) = delete;
  LobbyEntry& operator=(LobbyEntry&&) = delete;
  ~LobbyEntry() = default;

  [[nodiscard]] std::uint64_t lobby_id() const noexcept { return lobby_id_; }
  [[nodiscard]] const runtime::SnapshotPublication& snapshot_publication() const noexcept {
    return *snapshot_publication_;
  }
  [[nodiscard]] const MatchSessionContext& match_session() const& noexcept {
    return match_session_;
  }
  [[nodiscard]] const MatchSessionContext& match_session() const&& = delete;

  // Sessions currently admitted into this room. Counted by the session itself when it opens its
  // match session and when it leaves, so it is the number of controllers of kind `session` the
  // room's directory would list.
  [[nodiscard]] std::size_t session_count() const noexcept {
    return session_count_.load(std::memory_order_acquire);
  }
  void count_session_in() const noexcept { session_count_.fetch_add(1, std::memory_order_acq_rel); }
  void count_session_out() const noexcept {
    session_count_.fetch_sub(1, std::memory_order_acq_rel);
  }

private:
  std::uint64_t lobby_id_;
  const runtime::SnapshotPublication* snapshot_publication_;
  MatchSessionContext match_session_;
  mutable std::atomic<std::size_t> session_count_{0};
};

// canonical: lobby_directory -- every room this server can route a session to, by lobby id.
//
// Rooms are numbered `1..N` in the order the composition root built them, and the directory is
// fixed for the process lifetime: rooms are created from `[lobbies] count` at startup and never
// reaped (ADR 0006 § "The lobby lifecycle"). Room 1 is what `/api/v3/lobbies/1/session` and every
// v1 route serve; `/api/v3/lobbies/<lobby_id>/session` resolves its segment through `find`, and
// `GET /api/v3/lobbies` lists every entry (`docs/protocol/v3.md` § "The lobby directory").
// related: lobby_entry -- one row.
// related: server_execution_context.hpp -- the holder.
class LobbyDirectory final {
public:
  // One room as the composition root describes it. The id is the room's position plus one, and
  // `create` refuses anything else so a lobby id is always an index.
  struct Room final {
    std::uint64_t lobby_id;
    const runtime::SnapshotPublication& snapshot_publication;
    MatchSessionContext match_session;
  };

  // Throws GameServerError with `SERVER.LOBBY_DIRECTORY_INVALID` for no rooms at all, or for ids
  // that are not exactly `1..N` in order.
  [[nodiscard]] static LobbyDirectory create(std::vector<Room> rooms);

  LobbyDirectory(const LobbyDirectory&) = delete;
  LobbyDirectory(LobbyDirectory&&) noexcept = default;
  LobbyDirectory& operator=(const LobbyDirectory&) = delete;
  LobbyDirectory& operator=(LobbyDirectory&&) noexcept = default;
  ~LobbyDirectory() = default;

  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  // The room with this id, or nullptr: the shape a route that must answer `404` needs.
  [[nodiscard]] const LobbyEntry* find(std::uint64_t lobby_id) const noexcept;
  // The room with this id. Throws GameServerError with `SERVER.LOBBY_DIRECTORY_INVALID` for an
  // id the directory does not hold, which a caller that holds a validated id never sees.
  [[nodiscard]] const LobbyEntry& room(std::uint64_t lobby_id) const;
  // The room at this position, `0..size()`, for a loop over every room.
  [[nodiscard]] const LobbyEntry& at(std::size_t index) const noexcept { return *entries_[index]; }

private:
  explicit LobbyDirectory(std::vector<std::unique_ptr<LobbyEntry>> entries) noexcept;

  std::vector<std::unique_ptr<LobbyEntry>> entries_;
};

} // namespace blob_royale::server

#endif
