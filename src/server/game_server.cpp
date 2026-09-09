#include "game_server.hpp"

#include "game_server_error.hpp"

#include <exception>
#include <memory>
#include <mutex>
#include <utility>

namespace blob_royale::server {

GameServer::GameServer(ServerConfig server_config, const LobbyDirectory& lobbies,
                       observability::StructuredLogger& logger)
    : logger_(logger), server_context_(std::make_shared<ServerExecutionContext>(
                           io_context_, std::move(server_config), lobbies, logger_)) {
  logger_.write({.severity = observability::LogSeverity::kInfo,
                 .event = "server.ready",
                 .lifecycle_state = "ready"});
}

GameServer::~GameServer() { stop(); }

void GameServer::run() {
  {
    std::scoped_lock transition_lock{lifecycle_transition_mutex_};
    {
      std::scoped_lock lifecycle_lock{lifecycle_mutex_};
      if (lifecycle_state_ == GameServerState::kStopped && !run_started_) {
        run_started_ = true;
        return;
      }
      if (lifecycle_state_ != GameServerState::kReady) {
        throw GameServerError{GameServerErrorCode::kLifecycleInvalid, "game_server.run",
                              "server run is single-use and requires the ready state"};
      }
      run_started_ = true;
      lifecycle_state_ = GameServerState::kStarting;
    }
    logger_.write({.severity = observability::LogSeverity::kInfo,
                   .event = "server.starting",
                   .lifecycle_state = "starting"});
    lifecycle_changed_.notify_all();
  }

  try {
    tcp_listener_ = std::make_shared<TcpListener>(io_context_, server_context_);
    const std::weak_ptr<TcpListener> weak_listener = tcp_listener_;
    server_context_->set_listener_stop_action([weak_listener] {
      if (const auto listener = weak_listener.lock()) {
        listener->stop();
      }
    });
    tcp_listener_->start();
    bool transitioned_to_running = false;
    {
      std::scoped_lock transition_lock{lifecycle_transition_mutex_};
      {
        std::scoped_lock lifecycle_lock{lifecycle_mutex_};
        if (lifecycle_state_ == GameServerState::kStarting) {
          lifecycle_state_ = GameServerState::kRunning;
          transitioned_to_running = true;
        }
      }
      if (transitioned_to_running) {
        logger_.write({.severity = observability::LogSeverity::kInfo,
                       .event = "server.running",
                       .lifecycle_state = "running"});
      }
      lifecycle_changed_.notify_all();
    }
    io_context_.run();

    if (const std::exception_ptr context_failure = server_context_->failure();
        context_failure != nullptr) {
      std::rethrow_exception(context_failure);
    }
    set_state(GameServerState::kStopped);
  } catch (...) {
    if (tcp_listener_ != nullptr) {
      tcp_listener_->stop();
    }
    io_context_.stop();
    {
      std::scoped_lock transition_lock{lifecycle_transition_mutex_};
      {
        std::scoped_lock lifecycle_lock{lifecycle_mutex_};
        if (failure_ == nullptr) {
          failure_ = std::current_exception();
        }
        lifecycle_state_ = GameServerState::kFailed;
      }
      logger_.write({.severity = observability::LogSeverity::kError,
                     .event = "server.failed",
                     .lifecycle_state = "failed"});
      lifecycle_changed_.notify_all();
    }
    throw;
  }
}

void GameServer::stop() noexcept {
  bool request_context_stop = false;
  bool stopped_before_run = false;
  {
    std::scoped_lock transition_lock{lifecycle_transition_mutex_};
    {
      std::scoped_lock lifecycle_lock{lifecycle_mutex_};
      switch (lifecycle_state_) {
      case GameServerState::kReady:
        lifecycle_state_ = GameServerState::kStopped;
        io_context_.stop();
        stopped_before_run = true;
        break;
      case GameServerState::kStarting:
      case GameServerState::kRunning:
        lifecycle_state_ = GameServerState::kStopping;
        request_context_stop = true;
        break;
      case GameServerState::kStopping:
      case GameServerState::kStopped:
      case GameServerState::kFailed:
        return;
      }
    }
    if (stopped_before_run) {
      logger_.write({.severity = observability::LogSeverity::kInfo,
                     .event = "server.stopped",
                     .lifecycle_state = "stopped"});
    } else if (request_context_stop) {
      logger_.write({.severity = observability::LogSeverity::kInfo,
                     .event = "server.stopping",
                     .lifecycle_state = "stopping"});
    }
    lifecycle_changed_.notify_all();
  }
  if (request_context_stop) {
    server_context_->request_stop();
  }
}

GameServerState GameServer::state() const noexcept {
  std::scoped_lock lifecycle_lock{lifecycle_mutex_};
  return lifecycle_state_;
}

bool GameServer::wait_for_state(const GameServerState expected_state,
                                const std::chrono::steady_clock::duration timeout) const {
  std::unique_lock lifecycle_lock{lifecycle_mutex_};
  return lifecycle_changed_.wait_for(lifecycle_lock, timeout, [this, expected_state] {
    return lifecycle_state_ == expected_state;
  });
}

bool GameServer::wait_for_startup_resolution(
    const std::chrono::steady_clock::duration timeout) const {
  std::unique_lock lifecycle_lock{lifecycle_mutex_};
  return lifecycle_changed_.wait_for(lifecycle_lock, timeout, [this] {
    return lifecycle_state_ == GameServerState::kRunning ||
           lifecycle_state_ == GameServerState::kStopping ||
           lifecycle_state_ == GameServerState::kStopped ||
           lifecycle_state_ == GameServerState::kFailed;
  });
}

void GameServer::rethrow_if_failed() const {
  std::exception_ptr failure;
  {
    std::scoped_lock lifecycle_lock{lifecycle_mutex_};
    failure = failure_;
  }
  if (failure != nullptr) {
    std::rethrow_exception(failure);
  }
}

void GameServer::set_state(const GameServerState state) noexcept {
  {
    std::scoped_lock transition_lock{lifecycle_transition_mutex_};
    {
      std::scoped_lock lifecycle_lock{lifecycle_mutex_};
      lifecycle_state_ = state;
    }
    if (state == GameServerState::kStopped) {
      logger_.write({.severity = observability::LogSeverity::kInfo,
                     .event = "server.stopped",
                     .lifecycle_state = "stopped"});
    }
    lifecycle_changed_.notify_all();
  }
}

} // namespace blob_royale::server
