#include "controller_directory.hpp"
#include "controller_id.hpp"
#include "controller_presentation.hpp"
#include "runtime_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <barrier>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;

namespace {

constexpr std::uint64_t kFirstControllerId = 1;
constexpr std::uint64_t kSecondControllerId = 2;
constexpr std::size_t kConcurrentWriterCount = 4;
constexpr std::size_t kRegistrationsPerWriter = 128;

[[nodiscard]] simulation::ControllerId controller_fixture(const std::uint64_t value) {
  return simulation::ControllerId::create(value);
}

} // namespace

TEST_CASE("ControllerDirectory registers and returns one controller's presentation values",
          "[unit][runtime][controller_directory]") {
  runtime::ControllerDirectory directory;

  REQUIRE(directory.register_controller(controller_fixture(kFirstControllerId), "session", "Ada") ==
          runtime::ControllerRegistrationResult::kRegistered);

  const std::optional<runtime::ControllerPresentation> presentation =
      directory.find(controller_fixture(kFirstControllerId));
  REQUIRE(presentation.has_value());
  REQUIRE(presentation->controller_kind == "session");
  REQUIRE(presentation->display_name == "Ada");
  REQUIRE(directory.contains(controller_fixture(kFirstControllerId)));
  REQUIRE(directory.size() == 1);
}

TEST_CASE("ControllerDirectory rejects a duplicate registration and keeps the first entry",
          "[unit][runtime][controller_directory]") {
  runtime::ControllerDirectory directory;
  REQUIRE(directory.register_controller(controller_fixture(kFirstControllerId), "session", "Ada") ==
          runtime::ControllerRegistrationResult::kRegistered);

  REQUIRE(directory.register_controller(controller_fixture(kFirstControllerId), "wanderer",
                                        "Impostor") ==
          runtime::ControllerRegistrationResult::kRejectedDuplicateControllerId);

  // First registration wins, so a later frame cannot rename an established player.
  const std::optional<runtime::ControllerPresentation> presentation =
      directory.find(controller_fixture(kFirstControllerId));
  REQUIRE(presentation.has_value());
  REQUIRE(presentation->controller_kind == "session");
  REQUIRE(presentation->display_name == "Ada");
  REQUIRE(directory.size() == 1);
}

TEST_CASE("ControllerDirectory returns nullopt for an unknown controller id",
          "[unit][runtime][controller_directory]") {
  runtime::ControllerDirectory directory;
  REQUIRE(directory.register_controller(controller_fixture(kFirstControllerId), "session", "Ada") ==
          runtime::ControllerRegistrationResult::kRegistered);

  REQUIRE_FALSE(directory.find(controller_fixture(kSecondControllerId)).has_value());
  REQUIRE_FALSE(directory.contains(controller_fixture(kSecondControllerId)));
}

TEST_CASE("ControllerDirectory erases an entry on close so an unknown lookup is the closed case",
          "[unit][runtime][controller_directory]") {
  runtime::ControllerDirectory directory;
  REQUIRE(directory.register_controller(controller_fixture(kFirstControllerId), "session", "Ada") ==
          runtime::ControllerRegistrationResult::kRegistered);

  REQUIRE(directory.close(controller_fixture(kFirstControllerId)) ==
          runtime::ControllerCloseResult::kClosed);

  REQUIRE_FALSE(directory.find(controller_fixture(kFirstControllerId)).has_value());
  REQUIRE(directory.size() == 0);
}

TEST_CASE("ControllerDirectory reports a second close rather than failing",
          "[unit][runtime][controller_directory]") {
  runtime::ControllerDirectory directory;
  REQUIRE(directory.register_controller(controller_fixture(kFirstControllerId), "session", "Ada") ==
          runtime::ControllerRegistrationResult::kRegistered);
  REQUIRE(directory.close(controller_fixture(kFirstControllerId)) ==
          runtime::ControllerCloseResult::kClosed);

  REQUIRE(directory.close(controller_fixture(kFirstControllerId)) ==
          runtime::ControllerCloseResult::kUnknownControllerId);
}

TEST_CASE("ControllerDirectory closes an id it never registered without failing",
          "[unit][runtime][controller_directory]") {
  runtime::ControllerDirectory directory;

  REQUIRE(directory.close(controller_fixture(kFirstControllerId)) ==
          runtime::ControllerCloseResult::kUnknownControllerId);
}

TEST_CASE("ControllerDirectory rejects an empty controller kind",
          "[unit][runtime][controller_directory]") {
  runtime::ControllerDirectory directory;

  REQUIRE(directory.register_controller(controller_fixture(kFirstControllerId), "", "Ada") ==
          runtime::ControllerRegistrationResult::kRejectedControllerKind);
  REQUIRE(directory.size() == 0);
}

TEST_CASE("ControllerDirectory rejects an oversized controller kind",
          "[unit][runtime][controller_directory]") {
  runtime::ControllerDirectory directory;
  const std::string oversized(runtime::kMaximumControllerKindLength + 1, 'k');

  REQUIRE(directory.register_controller(controller_fixture(kFirstControllerId), oversized, "Ada") ==
          runtime::ControllerRegistrationResult::kRejectedControllerKind);
}

TEST_CASE("ControllerDirectory rejects an oversized display name",
          "[unit][runtime][controller_directory]") {
  runtime::ControllerDirectory directory;
  const std::string oversized(runtime::kMaximumDisplayNameLength + 1, 'n');

  REQUIRE(
      directory.register_controller(controller_fixture(kFirstControllerId), "session", oversized) ==
      runtime::ControllerRegistrationResult::kRejectedDisplayName);
}

TEST_CASE("ControllerDirectory rejects a control character in a proxy-supplied display name",
          "[unit][runtime][controller_directory]") {
  runtime::ControllerDirectory directory;

  REQUIRE(directory.register_controller(controller_fixture(kFirstControllerId), "session",
                                        std::string("Ada\nBob")) ==
          runtime::ControllerRegistrationResult::kRejectedDisplayName);
  REQUIRE(directory.register_controller(controller_fixture(kFirstControllerId), "session",
                                        std::string("Ada\x7F")) ==
          runtime::ControllerRegistrationResult::kRejectedDisplayName);
}

TEST_CASE("ControllerDirectory accepts an empty display name",
          "[unit][runtime][controller_directory]") {
  runtime::ControllerDirectory directory;

  REQUIRE(directory.register_controller(controller_fixture(kFirstControllerId), "session", "") ==
          runtime::ControllerRegistrationResult::kRegistered);
  REQUIRE(directory.find(controller_fixture(kFirstControllerId))->display_name.empty());
}

TEST_CASE("ControllerDirectory refuses a registration past its bound",
          "[unit][runtime][controller_directory]") {
  runtime::ControllerDirectory directory;
  for (std::size_t index = 0; index < runtime::kMaximumControllerDirectoryEntryCount; ++index) {
    REQUIRE(directory.register_controller(controller_fixture(static_cast<std::uint64_t>(index) + 1),
                                          "session", "Ada") ==
            runtime::ControllerRegistrationResult::kRegistered);
  }

  const std::uint64_t overflowing_id = runtime::kMaximumControllerDirectoryEntryCount + 1;
  REQUIRE(directory.register_controller(controller_fixture(overflowing_id), "session", "Ada") ==
          runtime::ControllerRegistrationResult::kRejectedDirectoryFull);
  REQUIRE(directory.size() == runtime::kMaximumControllerDirectoryEntryCount);
}

TEST_CASE("ControllerDirectory serves concurrent writers and readers",
          "[unit][runtime][controller_directory][concurrency]") {
  runtime::ControllerDirectory directory;
  std::barrier start_line(static_cast<std::ptrdiff_t>(kConcurrentWriterCount + 1));
  std::vector<std::jthread> writers;
  writers.reserve(kConcurrentWriterCount);

  for (std::size_t writer_index = 0; writer_index < kConcurrentWriterCount; ++writer_index) {
    writers.emplace_back([&, writer_index] {
      start_line.arrive_and_wait();
      for (std::size_t registration = 0; registration < kRegistrationsPerWriter; ++registration) {
        const std::uint64_t controller_id =
            static_cast<std::uint64_t>(writer_index * kRegistrationsPerWriter + registration) + 1;
        static_cast<void>(
            directory.register_controller(controller_fixture(controller_id), "session", "Ada"));
      }
    });
  }

  start_line.arrive_and_wait();
  // The encoding boundary reads while the network thread writes, which is the production pattern.
  for (std::size_t read = 0; read < kRegistrationsPerWriter; ++read) {
    static_cast<void>(directory.find(controller_fixture(kFirstControllerId)));
    static_cast<void>(directory.size());
  }
  for (std::jthread& writer : writers) {
    writer.join();
  }

  REQUIRE(directory.size() == kConcurrentWriterCount * kRegistrationsPerWriter);
}
