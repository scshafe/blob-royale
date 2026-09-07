#include "application_text_file_reader.hpp"

#include "application_input_error.hpp"

#include <cstdint>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>

namespace blob_royale::application {
namespace {

struct FileErrorCodes final {
  ApplicationInputErrorCode missing;
  ApplicationInputErrorCode not_regular;
  ApplicationInputErrorCode too_large;
  ApplicationInputErrorCode read_failed;
};

[[nodiscard]] constexpr FileErrorCodes
file_error_codes(const ApplicationTextFileKind file_kind) noexcept {
  if (file_kind == ApplicationTextFileKind::kConfiguration) {
    return {ApplicationInputErrorCode::kConfigurationFileMissing,
            ApplicationInputErrorCode::kConfigurationPathNotRegularFile,
            ApplicationInputErrorCode::kConfigurationFileTooLarge,
            ApplicationInputErrorCode::kConfigurationFileReadFailed};
  }
  if (file_kind == ApplicationTextFileKind::kMap) {
    return {ApplicationInputErrorCode::kMapFileMissing,
            ApplicationInputErrorCode::kMapPathNotRegularFile,
            ApplicationInputErrorCode::kMapFileTooLarge,
            ApplicationInputErrorCode::kMapFileReadFailed};
  }
  return {ApplicationInputErrorCode::kScenarioFileMissing,
          ApplicationInputErrorCode::kScenarioPathNotRegularFile,
          ApplicationInputErrorCode::kScenarioFileTooLarge,
          ApplicationInputErrorCode::kScenarioFileReadFailed};
}

[[nodiscard]] std::string path_context(const std::filesystem::path& path) {
  const std::string text = path.string();
  return text.empty() ? std::string{"<empty-path>"} : text;
}

[[noreturn]] void throw_filesystem_error(const ApplicationInputErrorCode error_code,
                                         const std::filesystem::path& path,
                                         const std::string_view operation,
                                         const std::error_code& cause) {
  std::string detail{operation};
  detail.append(" failed: ");
  detail.append(cause.message());
  throw ApplicationInputError{error_code, path_context(path), std::move(detail)};
}

} // namespace

std::string read_application_text_file(const std::filesystem::path& path,
                                       const ApplicationTextFileKind file_kind,
                                       const std::size_t maximum_byte_count) {
  const FileErrorCodes codes = file_error_codes(file_kind);
  if (path.empty()) {
    throw ApplicationInputError{codes.missing, path_context(path), "path must not be empty"};
  }

  std::error_code status_error;
  const std::filesystem::file_status status = std::filesystem::status(path, status_error);
  if (status_error) {
    if (status_error == std::errc::no_such_file_or_directory) {
      throw ApplicationInputError{codes.missing, path_context(path), "file does not exist"};
    }
    throw_filesystem_error(codes.read_failed, path, "status", status_error);
  }
  if (!std::filesystem::exists(status)) {
    throw ApplicationInputError{codes.missing, path_context(path), "file does not exist"};
  }
  if (!std::filesystem::is_regular_file(status)) {
    throw ApplicationInputError{codes.not_regular, path_context(path),
                                "path must identify a regular file"};
  }

  std::error_code size_error;
  const std::uintmax_t file_byte_count = std::filesystem::file_size(path, size_error);
  if (size_error) {
    throw_filesystem_error(codes.read_failed, path, "file-size query", size_error);
  }
  if (file_byte_count > static_cast<std::uintmax_t>(maximum_byte_count)) {
    throw ApplicationInputError{codes.too_large, path_context(path),
                                "file exceeds the configured byte limit"};
  }
  if (file_byte_count > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
    throw ApplicationInputError{codes.too_large, path_context(path),
                                "file cannot be represented by the input stream"};
  }

  std::ifstream input{path, std::ios::binary};
  if (!input.is_open()) {
    throw ApplicationInputError{codes.read_failed, path_context(path), "file could not be opened"};
  }

  const auto expected_byte_count = static_cast<std::streamsize>(file_byte_count);
  std::string contents(static_cast<std::size_t>(file_byte_count), '\0');
  if (expected_byte_count > 0) {
    input.read(contents.data(), expected_byte_count);
    if (input.gcount() != expected_byte_count) {
      throw ApplicationInputError{codes.read_failed, path_context(path),
                                  "file changed or could not be read completely"};
    }
  }

  char unexpected_byte = '\0';
  if (input.get(unexpected_byte)) {
    const ApplicationInputErrorCode code =
        file_byte_count >= static_cast<std::uintmax_t>(maximum_byte_count) ? codes.too_large
                                                                           : codes.read_failed;
    throw ApplicationInputError{code, path_context(path), "file changed while it was being read"};
  }
  if (input.bad()) {
    throw ApplicationInputError{codes.read_failed, path_context(path),
                                "an input/output error occurred while reading"};
  }
  return contents;
}

} // namespace blob_royale::application
