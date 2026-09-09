#include "lobby_directory.hpp"

#include "game_server_error.hpp"
#include "match_session_context.hpp"
#include "server_test_fixture.hpp"

#include "snapshot_publication.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <utility>
#include <vector>

namespace server = blob_royale::server;
namespace fixture = blob_royale::server::test_fixture;
namespace runtime = blob_royale::runtime;

TEST_CASE("LobbyDirectory numbers its rooms 1..N in order and answers by id",
          "[unit][server][lobbies]") {
  const runtime::SnapshotPublication first_publication = fixture::initial_publication();
  const runtime::SnapshotPublication second_publication = fixture::initial_publication();
  fixture::MatchSessionFixture first_match;
  fixture::MatchSessionFixture second_match;

  std::vector<server::LobbyDirectory::Room> rooms;
  rooms.push_back({.lobby_id = 1,
                   .snapshot_publication = first_publication,
                   .match_session = first_match.context(1)});
  rooms.push_back({.lobby_id = 2,
                   .snapshot_publication = second_publication,
                   .match_session = second_match.context(2)});
  const server::LobbyDirectory directory = server::LobbyDirectory::create(std::move(rooms));

  REQUIRE(directory.size() == 2);
  CHECK(directory.room(1).lobby_id() == 1);
  CHECK(directory.room(2).lobby_id() == 2);
  CHECK(&directory.room(2).snapshot_publication() == &second_publication);
  CHECK(directory.room(2).match_session().lobby_id() == 2);
  CHECK(&directory.at(0) == &directory.room(1));
  // An id nobody holds is a null from `find` -- the shape a `404` route needs -- and a typed
  // failure from `room`, which a caller holding a validated id never sees.
  CHECK(directory.find(0) == nullptr);
  CHECK(directory.find(3) == nullptr);
  CHECK(directory.find(2) == &directory.room(2));
  CHECK_THROWS_AS(directory.room(3), server::GameServerError);
}

TEST_CASE("LobbyDirectory refuses no rooms, rooms out of order, and a context for another room",
          "[unit][server][lobbies]") {
  const runtime::SnapshotPublication publication = fixture::initial_publication();
  fixture::MatchSessionFixture match;

  CHECK_THROWS_AS(server::LobbyDirectory::create({}), server::GameServerError);

  std::vector<server::LobbyDirectory::Room> misnumbered;
  misnumbered.push_back(
      {.lobby_id = 2, .snapshot_publication = publication, .match_session = match.context(2)});
  CHECK_THROWS_AS(server::LobbyDirectory::create(std::move(misnumbered)), server::GameServerError);

  std::vector<server::LobbyDirectory::Room> disagreeing;
  disagreeing.push_back(
      {.lobby_id = 1, .snapshot_publication = publication, .match_session = match.context(2)});
  CHECK_THROWS_AS(server::LobbyDirectory::create(std::move(disagreeing)), server::GameServerError);
}

TEST_CASE("LobbyEntry counts the sessions in its room", "[unit][server][lobbies]") {
  const runtime::SnapshotPublication publication = fixture::initial_publication();
  fixture::MatchSessionFixture match;
  const server::LobbyDirectory directory = fixture::single_lobby(publication, match.context());

  const server::LobbyEntry& room = directory.room(1);
  CHECK(room.session_count() == 0);
  room.count_session_in();
  room.count_session_in();
  CHECK(room.session_count() == 2);
  room.count_session_out();
  CHECK(room.session_count() == 1);
}
