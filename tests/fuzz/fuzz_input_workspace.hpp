#ifndef BLOB_ROYALE_TESTS_FUZZ_FUZZ_INPUT_WORKSPACE_HPP
#define BLOB_ROYALE_TESTS_FUZZ_FUZZ_INPUT_WORKSPACE_HPP

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <unistd.h>

namespace blob_royale::fuzz {

// Fixed harness-authored companions, never paths or content selected by fuzz input.
struct FuzzCompanionFile final {
  std::string_view name;
  std::string_view contents;
};

// canonical: fuzz_input_workspace -- one process-local bounded file boundary for libFuzzer.
// Production loaders intentionally retain their regular-file and size checks; fuzzing exercises
// that exact acquisition path instead of exposing a test-only parser entry point.
class FuzzInputWorkspace final {
public:
  explicit FuzzInputWorkspace(std::string file_name,
                              const std::initializer_list<FuzzCompanionFile> companions = {})
      : directory_(std::filesystem::temp_directory_path() /
                   ("blob-royale-fuzz-" + std::to_string(::getpid()))),
        file_path_(directory_ / std::move(file_name)) {
    std::error_code error;
    std::filesystem::create_directories(file_path_.parent_path(), error);
    if (error) {
      std::abort();
    }
    for (const FuzzCompanionFile& companion : companions) {
      write_file(file_path_.parent_path() / companion.name,
                 std::span<const std::uint8_t>{
                     reinterpret_cast<const std::uint8_t*>(companion.contents.data()),
                     companion.contents.size()});
    }
  }

  FuzzInputWorkspace(const FuzzInputWorkspace&) = delete;
  FuzzInputWorkspace(FuzzInputWorkspace&&) = delete;
  FuzzInputWorkspace& operator=(const FuzzInputWorkspace&) = delete;
  FuzzInputWorkspace& operator=(FuzzInputWorkspace&&) = delete;

  ~FuzzInputWorkspace() {
    std::error_code ignored_error;
    std::filesystem::remove_all(directory_, ignored_error);
  }

  [[nodiscard]] const std::filesystem::path& write(const std::span<const std::uint8_t> bytes) {
    write_file(file_path_, bytes);
    return file_path_;
  }

private:
  static void write_file(const std::filesystem::path& path,
                         const std::span<const std::uint8_t> bytes) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
      std::abort();
    }
    if (!bytes.empty()) {
      output.write(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    output.close();
    if (!output) {
      std::abort();
    }
  }

  std::filesystem::path directory_;
  std::filesystem::path file_path_;
};

} // namespace blob_royale::fuzz

#endif
