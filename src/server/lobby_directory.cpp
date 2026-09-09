#include "lobby_directory.hpp"

#include "game_server_error.hpp"

#include <string>
#include <utility>

namespace blob_royale::server {

LobbyEntry::LobbyEntry(const std::uint64_t lobby_id,
                       const runtime::SnapshotPublication& snapshot_publication,
                       MatchSessionContext match_session) noexcept
    : lobby_id_(lobby_id), snapshot_publication_(&snapshot_publication),
      match_session_(std::move(match_session)) {}

LobbyDirectory LobbyDirectory::create(std::vector<Room> rooms) {
  if (rooms.empty()) {
    throw GameServerError{GameServerErrorCode::kLobbyDirectoryInvalid, "lobby_directory.rooms",
                          "a server serves at least one room"};
  }
  std::vector<std::unique_ptr<LobbyEntry>> entries;
  entries.reserve(rooms.size());
  for (Room& room : rooms) {
    const std::uint64_t expected = entries.size() + 1;
    if (room.lobby_id != expected || room.match_session.lobby_id() != expected) {
      throw GameServerError{GameServerErrorCode::kLobbyDirectoryInvalid, "lobby_directory.rooms",
                            "rooms are numbered 1..N in order; position " +
                                std::to_string(expected) + " was given lobby id " +
                                std::to_string(room.lobby_id) + " and a session context for " +
                                std::to_string(room.match_session.lobby_id())};
    }
    entries.push_back(std::make_unique<LobbyEntry>(room.lobby_id, room.snapshot_publication,
                                                   std::move(room.match_session)));
  }
  return LobbyDirectory{std::move(entries)};
}

LobbyDirectory::LobbyDirectory(std::vector<std::unique_ptr<LobbyEntry>> entries) noexcept
    : entries_(std::move(entries)) {}

const LobbyEntry* LobbyDirectory::find(const std::uint64_t lobby_id) const noexcept {
  if (lobby_id == 0 || lobby_id > entries_.size()) {
    return nullptr;
  }
  return entries_[lobby_id - 1].get();
}

const LobbyEntry& LobbyDirectory::room(const std::uint64_t lobby_id) const {
  const LobbyEntry* const entry = find(lobby_id);
  if (entry == nullptr) {
    throw GameServerError{GameServerErrorCode::kLobbyDirectoryInvalid, "lobby_directory.room",
                          "no room has lobby id " + std::to_string(lobby_id) +
                              "; the directory holds " + std::to_string(entries_.size())};
  }
  return *entry;
}

} // namespace blob_royale::server
