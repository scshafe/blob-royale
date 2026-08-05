#include "game_server_error.hpp"

#include <utility>

namespace blob_royale::server {

GameServerError::GameServerError(const GameServerErrorCode error_code, std::string context,
                                 std::string detail)
    : std::runtime_error(build_message(error_code, context, detail)), error_code_(error_code),
      context_(std::move(context)), detail_(std::move(detail)) {}

std::string GameServerError::build_message(const GameServerErrorCode error_code,
                                           const std::string_view context,
                                           const std::string_view detail) {
  std::string message;
  message.reserve(game_server_error_code_name(error_code).size() + context.size() + detail.size() +
                  4);
  message.append(game_server_error_code_name(error_code));
  message.append(" [");
  message.append(context);
  message.append("]: ");
  message.append(detail);
  return message;
}

} // namespace blob_royale::server
