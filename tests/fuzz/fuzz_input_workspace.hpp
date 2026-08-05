#ifndef BLOB_ROYALE_TESTS_FUZZ_FUZZ_INPUT_WORKSPACE_HPP
#define BLOB_ROYALE_TESTS_FUZZ_FUZZ_INPUT_WORKSPACE_HPP

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include <unistd.h>

namespace blob_royale::fuzz {

// canonical: fuzz_input_workspace -- one process-local bounded file boundary for libFuzzer.
// Production loaders intentionally retain their regular-file and size checks; fuzzing exercises
// that exact acquisition path instead of exposing a test-only parser entry point.
class FuzzInputWorkspace final {
public:
  explicit FuzzInputWorkspace(std::string file_name)
      : directory_(std::filesystem::temp_directory_path() /
                   ("blob-royale-fuzz-" + std::to_string(::getpid()))),
        file_path_(directory_ / std::move(file_name)) {
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) {
      std::abort();
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
    std::ofstream output{file_path_, std::ios::binary | std::ios::trunc};
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
    return file_path_;
  }

private:
  std::filesystem::path directory_;
  std::filesystem::path file_path_;
};

} // namespace blob_royale::fuzz

#endif
