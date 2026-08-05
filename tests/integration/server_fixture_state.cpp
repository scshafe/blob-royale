#include "server_fixture_state.hpp"

#include "fixture_text_file.hpp"
#include "integration_test_error.hpp"

#include <boost/json/object.hpp>
#include <boost/json/parse.hpp>
#include <boost/json/serialize.hpp>
#include <boost/json/value.hpp>

#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::integration_test {
namespace {

constexpr std::string_view kStateFileName = "server-fixture-state.json";
constexpr std::string_view kExitResultFileName = "server-exit-result.json";

[[nodiscard]] const boost::json::object& require_exact_object(const boost::json::value& value) {
  if (!value.is_object()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, "fixture_state.decode",
                               "fixture state must be a JSON object"};
  }
  const boost::json::object& object = value.as_object();
  constexpr std::string_view kRequiredMembers[] = {
      "supervisor_process_id", "server_process_id",   "port", "configuration_path", "scenario_path",
      "server_log_path",       "supervisor_log_path",
  };
  if (object.size() != std::size(kRequiredMembers)) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, "fixture_state.decode",
                               "fixture state member set is invalid"};
  }
  for (const std::string_view member : kRequiredMembers) {
    if (!object.contains(boost::json::string_view{member.data(), member.size()})) {
      throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                                 "fixture_state.decode", "fixture state member is missing"};
    }
  }
  return object;
}

[[nodiscard]] std::int64_t require_signed_integer(const boost::json::object& object,
                                                  const std::string_view member) {
  const boost::json::value& value =
      object.at(boost::json::string_view{member.data(), member.size()});
  if (!value.is_int64()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, "fixture_state.decode",
                               "process ID must be a signed integer"};
  }
  return value.as_int64();
}

[[nodiscard]] std::uint64_t require_unsigned_integer(const boost::json::object& object,
                                                     const std::string_view member) {
  const boost::json::value& value =
      object.at(boost::json::string_view{member.data(), member.size()});
  if (value.is_uint64()) {
    return value.as_uint64();
  }
  if (value.is_int64() && value.as_int64() >= 0) {
    return static_cast<std::uint64_t>(value.as_int64());
  }
  throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, "fixture_state.decode",
                             "port must be an unsigned integer"};
}

[[nodiscard]] std::filesystem::path require_path(const boost::json::object& object,
                                                 const std::string_view member) {
  const boost::json::value& value =
      object.at(boost::json::string_view{member.data(), member.size()});
  if (!value.is_string() || value.as_string().empty()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, "fixture_state.decode",
                               "fixture path must be a nonempty string"};
  }
  const boost::json::string& path_text = value.as_string();
  return std::filesystem::path{std::string{path_text.data(), path_text.size()}};
}

} // namespace

ServerFixtureState ServerFixtureState::create(
    const pid_t supervisor_process_id, const pid_t server_process_id, const std::uint16_t port,
    std::filesystem::path configuration_path, std::filesystem::path scenario_path,
    std::filesystem::path server_log_path, std::filesystem::path supervisor_log_path) {
  if (supervisor_process_id <= 1 || server_process_id <= 1 || port == 0 ||
      configuration_path.empty() || scenario_path.empty() || server_log_path.empty() ||
      supervisor_log_path.empty()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kArgumentInvalid, "fixture_state.create",
                               "fixture state values are invalid"};
  }
  return ServerFixtureState{
      supervisor_process_id,         server_process_id,        port,
      std::move(configuration_path), std::move(scenario_path), std::move(server_log_path),
      std::move(supervisor_log_path)};
}

ServerFixtureState ServerFixtureState::load(const std::filesystem::path& fixture_directory) {
  boost::json::value value;
  try {
    value = boost::json::parse(read_bounded_fixture_text_file(state_path(fixture_directory), 16'384,
                                                              "fixture_state.read"));
  } catch (const IntegrationTestError&) {
    throw;
  } catch (const std::exception& error) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, "fixture_state.decode",
                               error.what()};
  }
  const boost::json::object& object = require_exact_object(value);

  const std::int64_t supervisor_process_id =
      require_signed_integer(object, "supervisor_process_id");
  const std::int64_t server_process_id = require_signed_integer(object, "server_process_id");
  const std::uint64_t port = require_unsigned_integer(object, "port");
  if (supervisor_process_id <= 1 || server_process_id <= 1 ||
      supervisor_process_id > std::numeric_limits<pid_t>::max() ||
      server_process_id > std::numeric_limits<pid_t>::max() || port == 0 ||
      port > std::numeric_limits<std::uint16_t>::max()) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, "fixture_state.decode",
                               "fixture numeric value is out of range"};
  }

  return create(static_cast<pid_t>(supervisor_process_id), static_cast<pid_t>(server_process_id),
                static_cast<std::uint16_t>(port), require_path(object, "configuration_path"),
                require_path(object, "scenario_path"), require_path(object, "server_log_path"),
                require_path(object, "supervisor_log_path"));
}

void ServerFixtureState::save(const std::filesystem::path& fixture_directory) const {
  boost::json::object object;
  object.emplace("supervisor_process_id", supervisor_process_id_);
  object.emplace("server_process_id", server_process_id_);
  object.emplace("port", port_);
  object.emplace("configuration_path", configuration_path_.string());
  object.emplace("scenario_path", scenario_path_.string());
  object.emplace("server_log_path", server_log_path_.string());
  object.emplace("supervisor_log_path", supervisor_log_path_.string());
  write_fixture_text_file_atomically(state_path(fixture_directory), boost::json::serialize(object),
                                     "fixture_state.write");
}

std::filesystem::path
ServerFixtureState::state_path(const std::filesystem::path& fixture_directory) {
  return fixture_directory / kStateFileName;
}

std::filesystem::path
ServerFixtureState::exit_result_path(const std::filesystem::path& fixture_directory) {
  return fixture_directory / kExitResultFileName;
}

ServerFixtureState::ServerFixtureState(const pid_t supervisor_process_id,
                                       const pid_t server_process_id, const std::uint16_t port,
                                       std::filesystem::path configuration_path,
                                       std::filesystem::path scenario_path,
                                       std::filesystem::path server_log_path,
                                       std::filesystem::path supervisor_log_path) noexcept
    : supervisor_process_id_(supervisor_process_id), server_process_id_(server_process_id),
      port_(port), configuration_path_(std::move(configuration_path)),
      scenario_path_(std::move(scenario_path)), server_log_path_(std::move(server_log_path)),
      supervisor_log_path_(std::move(supervisor_log_path)) {}

} // namespace blob_royale::integration_test
