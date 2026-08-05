#include "server_exit_result.hpp"

#include "fixture_text_file.hpp"
#include "integration_test_error.hpp"
#include "server_fixture_state.hpp"

#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>

#include <cstdint>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>

namespace blob_royale::integration_test {
namespace {

constexpr std::size_t kMaximumExitResultBytes = 4'096;

[[nodiscard]] std::int64_t require_integer(const boost::json::value& value,
                                           const std::string_view context) {
  if (!value.is_int64()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                               "server_exit_result.decode", std::string{context}};
  }
  return value.as_int64();
}

} // namespace

ServerExitResult ServerExitResult::exited(const pid_t server_process_id, const int exit_code,
                                          const bool forced_shutdown) {
  if (server_process_id <= 1 || exit_code < 0 || exit_code > 255) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                               "server_exit_result.create", "exit observation is invalid"};
  }
  return ServerExitResult{server_process_id, exit_code, std::nullopt, forced_shutdown};
}

ServerExitResult ServerExitResult::signaled(const pid_t server_process_id, const int signal_number,
                                            const bool forced_shutdown) {
  if (server_process_id <= 1 || signal_number <= 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid,
                               "server_exit_result.create", "signal observation is invalid"};
  }
  return ServerExitResult{server_process_id, std::nullopt, signal_number, forced_shutdown};
}

ServerExitResult ServerExitResult::load(const std::filesystem::path& fixture_directory) {
  boost::json::value value;
  try {
    value = boost::json::parse(
        read_bounded_fixture_text_file(ServerFixtureState::exit_result_path(fixture_directory),
                                       kMaximumExitResultBytes, "server_exit_result.read"));
  } catch (const IntegrationTestError&) {
    throw;
  } catch (const std::exception& error) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                               "server_exit_result.decode", error.what()};
  }
  if (!value.is_object()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                               "server_exit_result.decode", "exit result must be an object"};
  }
  const boost::json::object& object = value.as_object();
  constexpr std::string_view kMembers[] = {"server_process_id", "exit_code", "signal_number",
                                           "forced_shutdown"};
  if (object.size() != std::size(kMembers)) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                               "server_exit_result.decode", "exit result member set is invalid"};
  }
  for (const std::string_view member : kMembers) {
    if (!object.contains(boost::json::string_view{member.data(), member.size()})) {
      throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                                 "server_exit_result.decode", "exit result member is missing"};
    }
  }

  const std::int64_t process_id =
      require_integer(object.at("server_process_id"), "server_process_id must be an integer");
  if (process_id <= 1 || process_id > std::numeric_limits<pid_t>::max()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                               "server_exit_result.decode", "server_process_id is out of range"};
  }
  const auto optional_integer = [&](const std::string_view member) -> std::optional<int> {
    const boost::json::value& member_value =
        object.at(boost::json::string_view{member.data(), member.size()});
    if (member_value.is_null()) {
      return std::nullopt;
    }
    const std::int64_t integer = require_integer(member_value, "exit result must be an integer");
    if (integer < 0 || integer > std::numeric_limits<int>::max()) {
      throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                                 "server_exit_result.decode", "exit result is out of range"};
    }
    return static_cast<int>(integer);
  };
  if (!object.at("forced_shutdown").is_bool()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                               "server_exit_result.decode", "forced_shutdown must be boolean"};
  }

  const std::optional<int> exit_code = optional_integer("exit_code");
  const std::optional<int> signal_number = optional_integer("signal_number");
  if (exit_code.has_value() == signal_number.has_value()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                               "server_exit_result.decode",
                               "exactly one terminal process observation is required"};
  }
  return ServerExitResult{static_cast<pid_t>(process_id), exit_code, signal_number,
                          object.at("forced_shutdown").as_bool()};
}

void ServerExitResult::save(const std::filesystem::path& fixture_directory) const {
  boost::json::object object;
  object.emplace("server_process_id", server_process_id_);
  if (exit_code_.has_value()) {
    object.emplace("exit_code", *exit_code_);
  } else {
    object.emplace("exit_code", nullptr);
  }
  if (signal_number_.has_value()) {
    object.emplace("signal_number", *signal_number_);
  } else {
    object.emplace("signal_number", nullptr);
  }
  object.emplace("forced_shutdown", forced_shutdown_);
  write_fixture_text_file_atomically(ServerFixtureState::exit_result_path(fixture_directory),
                                     boost::json::serialize(object), "server_exit_result.write");
}

ServerExitResult::ServerExitResult(const pid_t server_process_id, std::optional<int> exit_code,
                                   std::optional<int> signal_number,
                                   const bool forced_shutdown) noexcept
    : server_process_id_(server_process_id), exit_code_(exit_code), signal_number_(signal_number),
      forced_shutdown_(forced_shutdown) {}

} // namespace blob_royale::integration_test
