#ifndef BLOB_ROYALE_TESTS_INTEGRATION_FIXTURE_TEXT_FILE_HPP
#define BLOB_ROYALE_TESTS_INTEGRATION_FIXTURE_TEXT_FILE_HPP

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace blob_royale::integration_test {

// canonical: fixture_text_file -- bounded reads and atomic writes for fixture handoff files.
[[nodiscard]] std::string read_bounded_fixture_text_file(const std::filesystem::path& path,
                                                         std::size_t maximum_byte_count,
                                                         std::string_view operation);

void write_fixture_text_file_atomically(const std::filesystem::path& path,
                                        std::string_view contents, std::string_view operation);

} // namespace blob_royale::integration_test

#endif
