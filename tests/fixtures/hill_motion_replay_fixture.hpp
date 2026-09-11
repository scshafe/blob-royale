#ifndef BLOB_ROYALE_TESTS_FIXTURES_HILL_MOTION_REPLAY_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_FIXTURES_HILL_MOTION_REPLAY_FIXTURE_HPP

#include "../unit/application/fixtures/hill_motion_configuration_fixture.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace blob_royale::testing::hill_motion_fixture {

inline constexpr std::array kLegacyReplays{"hill-contested", "hill-scripted-match",
                                           "hill-threshold", "hill-time-limit-draw"};
inline constexpr std::string_view kCopiedMatch = "hill-motion-copy/match.ini";

struct CopiedReplay final {
  std::filesystem::path directory;
  std::string match;
};

// Mutate an isolated copy of the accepted match authoring, never the checked-in replay or commands.
[[nodiscard]] inline CopiedReplay copy_scripted_replay(
    const application::test_fixture::TemporaryApplicationInputWorkspace& workspace) {
  const auto source =
      std::filesystem::path{BLOB_ROYALE_REPLAY_FIXTURE_DIRECTORY} / "hill-scripted-match";
  const auto directory = workspace.absent_path("hill-motion-copy");
  std::filesystem::copy(source, directory, std::filesystem::copy_options::recursive);
  std::ifstream input{directory / "match.ini", std::ios::binary};
  if (!input.is_open())
    throw std::runtime_error{"TEST.HILL_REPLAY_COPY_UNREADABLE"};
  std::string match{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  if (input.bad())
    throw std::runtime_error{"TEST.HILL_REPLAY_COPY_READ_FAILED"};
  return {directory, std::move(match)};
}

} // namespace blob_royale::testing::hill_motion_fixture

#endif
