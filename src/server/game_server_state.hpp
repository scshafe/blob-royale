#ifndef BLOB_ROYALE_SERVER_GAME_SERVER_STATE_HPP
#define BLOB_ROYALE_SERVER_GAME_SERVER_STATE_HPP

#include <string_view>

namespace blob_royale::server {

enum class GameServerState {
  kReady,
  kStarting,
  kRunning,
  kStopping,
  kStopped,
  kFailed,
};

[[nodiscard]] constexpr std::string_view game_server_state_name(const GameServerState state) {
  switch (state) {
  case GameServerState::kReady:
    return "ready";
  case GameServerState::kStarting:
    return "starting";
  case GameServerState::kRunning:
    return "running";
  case GameServerState::kStopping:
    return "stopping";
  case GameServerState::kStopped:
    return "stopped";
  case GameServerState::kFailed:
    return "failed";
  }
  return "invalid";
}

} // namespace blob_royale::server

#endif
