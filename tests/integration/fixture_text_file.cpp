#include "fixture_text_file.hpp"

#include "integration_test_error.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace blob_royale::integration_test {
namespace {

void write_all(const int descriptor, const std::string_view contents,
               const std::string_view operation) {
  std::size_t offset = 0;
  while (offset < contents.size()) {
    const ssize_t result = ::write(descriptor, contents.data() + offset, contents.size() - offset);
    if (result < 0 && errno == EINTR) {
      continue;
    }
    if (result <= 0) {
      throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                                 std::string{operation},
                                 std::error_code{errno, std::generic_category()}.message()};
    }
    offset += static_cast<std::size_t>(result);
  }
}

} // namespace

std::string read_bounded_fixture_text_file(const std::filesystem::path& path,
                                           const std::size_t maximum_byte_count,
                                           const std::string_view operation) {
  std::error_code file_size_error;
  const std::uintmax_t file_size = std::filesystem::file_size(path, file_size_error);
  if (file_size_error) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, std::string{operation},
                               file_size_error.message()};
  }
  if (file_size > maximum_byte_count) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, std::string{operation},
                               "file exceeds its byte bound"};
  }

  std::ifstream input{path, std::ios::binary};
  if (!input) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, std::string{operation},
                               "file could not be opened"};
  }
  std::string contents(static_cast<std::size_t>(file_size), '\0');
  input.read(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!input || input.gcount() != static_cast<std::streamsize>(contents.size())) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, std::string{operation},
                               "file could not be read fully"};
  }
  return contents;
}

void write_fixture_text_file_atomically(const std::filesystem::path& path,
                                        const std::string_view contents,
                                        const std::string_view operation) {
  std::filesystem::path temporary_path = path;
  temporary_path += ".tmp";

  const int descriptor = ::open(temporary_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (descriptor < 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, std::string{operation},
                               std::error_code{errno, std::generic_category()}.message()};
  }

  try {
    write_all(descriptor, contents, operation);
    if (::fsync(descriptor) != 0) {
      throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed,
                                 std::string{operation},
                                 std::error_code{errno, std::generic_category()}.message()};
    }
  } catch (...) {
    static_cast<void>(::close(descriptor));
    std::error_code ignored;
    std::filesystem::remove(temporary_path, ignored);
    throw;
  }
  if (::close(descriptor) != 0) {
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, std::string{operation},
                               std::error_code{errno, std::generic_category()}.message()};
  }

  std::error_code rename_error;
  std::filesystem::rename(temporary_path, path, rename_error);
  if (rename_error) {
    std::error_code ignored;
    std::filesystem::remove(temporary_path, ignored);
    throw IntegrationTestError{IntegrationTestErrorCode::kFilesystemFailed, std::string{operation},
                               rename_error.message()};
  }
}

} // namespace blob_royale::integration_test
