#ifndef BLOB_ROYALE_APPLICATION_APPLICATION_TEXT_FILE_READER_HPP
#define BLOB_ROYALE_APPLICATION_APPLICATION_TEXT_FILE_READER_HPP

#include <cstddef>
#include <filesystem>
#include <string>

namespace blob_royale::application {

enum class ApplicationTextFileKind { kConfiguration, kScenario, kMap };

// canonical: application_text_file_read -- bounded, exact reads for startup text inputs.
// Throws ApplicationInputError with a kind-specific code for every filesystem failure.
[[nodiscard]] std::string read_application_text_file(const std::filesystem::path& path,
                                                     ApplicationTextFileKind file_kind,
                                                     std::size_t maximum_byte_count);

} // namespace blob_royale::application

#endif
