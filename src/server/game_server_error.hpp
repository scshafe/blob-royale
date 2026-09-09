#ifndef BLOB_ROYALE_SERVER_GAME_SERVER_ERROR_HPP
#define BLOB_ROYALE_SERVER_GAME_SERVER_ERROR_HPP

#include <stdexcept>
#include <string>
#include <string_view>

namespace blob_royale::server {

enum class GameServerErrorCode {
  kLifecycleInvalid,
  kListenerOpenFailed,
  kListenerBindFailed,
  kListenerListenFailed,
  kListenerAcceptFailed,
  kHttpResponseEncodingFailed,
  kSnapshotEncodingFailed,
  kSessionInvariantFailed,
  kLobbyDirectoryInvalid,
};

[[nodiscard]] constexpr std::string_view
game_server_error_code_name(const GameServerErrorCode error_code) noexcept {
  switch (error_code) {
  case GameServerErrorCode::kLifecycleInvalid:
    return "SERVER.LIFECYCLE.INVALID";
  case GameServerErrorCode::kListenerOpenFailed:
    return "SERVER.LISTENER.OPEN_FAILED";
  case GameServerErrorCode::kListenerBindFailed:
    return "SERVER.LISTENER.BIND_FAILED";
  case GameServerErrorCode::kListenerListenFailed:
    return "SERVER.LISTENER.LISTEN_FAILED";
  case GameServerErrorCode::kListenerAcceptFailed:
    return "SERVER.LISTENER.ACCEPT_FAILED";
  case GameServerErrorCode::kHttpResponseEncodingFailed:
    return "SERVER.HTTP.RESPONSE_ENCODING_FAILED";
  case GameServerErrorCode::kSnapshotEncodingFailed:
    return "SERVER.WEBSOCKET.SNAPSHOT_ENCODING_FAILED";
  case GameServerErrorCode::kSessionInvariantFailed:
    return "SERVER.SESSION.INVARIANT_FAILED";
  case GameServerErrorCode::kLobbyDirectoryInvalid:
    return "SERVER.LOBBY_DIRECTORY_INVALID";
  }
  return "SERVER.ERROR_CODE_INVALID";
}

// canonical: game_server_error -- one hard server failure that must reach the composition root.
class GameServerError final : public std::runtime_error {
public:
  GameServerError(GameServerErrorCode error_code, std::string context, std::string detail);

  [[nodiscard]] GameServerErrorCode error_code() const noexcept { return error_code_; }
  [[nodiscard]] std::string_view code() const noexcept {
    return game_server_error_code_name(error_code_);
  }
  [[nodiscard]] const std::string& context() const& noexcept { return context_; }
  [[nodiscard]] const std::string& context() const&& = delete;
  [[nodiscard]] const std::string& detail() const& noexcept { return detail_; }
  [[nodiscard]] const std::string& detail() const&& = delete;

private:
  [[nodiscard]] static std::string build_message(GameServerErrorCode error_code,
                                                 std::string_view context, std::string_view detail);

  GameServerErrorCode error_code_;
  std::string context_;
  std::string detail_;
};

} // namespace blob_royale::server

#endif
