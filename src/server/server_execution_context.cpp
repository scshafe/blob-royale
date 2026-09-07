#include "server_execution_context.hpp"

#include "game_server_error.hpp"
#include "server_limits.hpp"

#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>

#include <exception>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace blob_royale::server {

ServerExecutionContext::ServerExecutionContext(
    boost::asio::io_context& io_context, ServerConfig server_config,
    const runtime::SnapshotPublication& snapshot_publication,
    MatchSessionContext match_session_context, observability::StructuredLogger& logger)
    : io_context_(io_context), server_config_(std::move(server_config)),
      snapshot_publication_(snapshot_publication),
      match_session_context_(std::move(match_session_context)), logger_(logger),
      game_api_router_(server_config_, snapshot_publication_, peer_traffic_policy_,
                       request_id_generator_),
      shutdown_timer_(io_context_) {}

ServerExecutionContext::SessionId
ServerExecutionContext::register_session(SessionStopAction stop_action) {
  if (!stop_action) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed, "server.sessions",
                          "session stop action must not be empty"};
  }
  if (next_session_id_ == std::numeric_limits<SessionId>::max()) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed, "server.sessions",
                          "session identity sequence exhausted"};
  }
  const SessionId session_id = next_session_id_++;
  sessions_.emplace(session_id, std::move(stop_action));
  return session_id;
}

void ServerExecutionContext::unregister_session(const SessionId session_id) noexcept {
  sessions_.erase(session_id);
  stop_if_drained();
}

void ServerExecutionContext::set_listener_stop_action(ListenerStopAction stop_action) {
  if (!stop_action) {
    throw GameServerError{GameServerErrorCode::kSessionInvariantFailed, "server.listener",
                          "listener stop action must not be empty"};
  }
  listener_stop_action_ = std::move(stop_action);
}

void ServerExecutionContext::request_stop() noexcept {
  try {
    boost::asio::post(io_context_, [self = shared_from_this()] { self->begin_stop_on_executor(); });
  } catch (...) {
    io_context_.stop();
  }
}

std::exception_ptr ServerExecutionContext::failure() const noexcept {
  std::scoped_lock failure_lock{failure_mutex_};
  return failure_;
}

void ServerExecutionContext::fail(std::exception_ptr failure) noexcept {
  try {
    boost::asio::post(io_context_,
                      [self = shared_from_this(), failure] { self->fail_on_executor(failure); });
  } catch (...) {
    {
      std::scoped_lock failure_lock{failure_mutex_};
      if (failure_ == nullptr) {
        failure_ = failure;
      }
    }
    io_context_.stop();
  }
}

void ServerExecutionContext::begin_stop_on_executor() {
  if (stopping_) {
    return;
  }
  stopping_ = true;
  if (listener_stop_action_) {
    listener_stop_action_();
  }
  stop_sessions(SessionStopMode::kGraceful);
  stop_if_drained();
  if (io_context_.stopped()) {
    return;
  }

  shutdown_timer_.expires_after(ServerLimits::kServerShutdownDeadline);
  shutdown_timer_.async_wait([self = shared_from_this()](const boost::system::error_code& error) {
    if (error == boost::asio::error::operation_aborted) {
      return;
    }
    self->stop_sessions(SessionStopMode::kImmediate);
    self->io_context_.stop();
  });
}

void ServerExecutionContext::fail_on_executor(std::exception_ptr failure) noexcept {
  {
    std::scoped_lock failure_lock{failure_mutex_};
    if (failure_ == nullptr) {
      failure_ = failure;
    }
  }
  stopping_ = true;
  if (listener_stop_action_) {
    listener_stop_action_();
  }
  stop_sessions(SessionStopMode::kImmediate);
  io_context_.stop();
}

void ServerExecutionContext::stop_sessions(const SessionStopMode mode) noexcept {
  std::vector<SessionStopAction> stop_actions;
  try {
    stop_actions.reserve(sessions_.size());
    for (const auto& [session_id, stop_action] : sessions_) {
      static_cast<void>(session_id);
      stop_actions.push_back(stop_action);
    }
  } catch (...) {
    std::scoped_lock failure_lock{failure_mutex_};
    if (failure_ == nullptr) {
      failure_ = std::current_exception();
    }
    io_context_.stop();
    return;
  }

  for (const SessionStopAction& stop_action : stop_actions) {
    try {
      stop_action(mode);
    } catch (...) {
      std::scoped_lock failure_lock{failure_mutex_};
      if (failure_ == nullptr) {
        failure_ = std::current_exception();
      }
    }
  }
}

void ServerExecutionContext::stop_if_drained() noexcept {
  if (!stopping_ || !sessions_.empty()) {
    return;
  }
  try {
    static_cast<void>(shutdown_timer_.cancel());
  } catch (...) {
    // The context is terminal and will be stopped below regardless.
  }
  io_context_.stop();
}

} // namespace blob_royale::server
