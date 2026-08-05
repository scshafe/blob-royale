#ifndef BLOB_ROYALE_APPLICATION_APPLICATION_CONFIG_LOADER_HPP
#define BLOB_ROYALE_APPLICATION_APPLICATION_CONFIG_LOADER_HPP

#include "application_config.hpp"

#include <cstddef>
#include <filesystem>
#include <string_view>
#include <variant>

namespace blob_royale::application {

// canonical: application_config_loader -- the only command-line and INI startup parser.
class ApplicationConfigLoader final {
public:
  struct HelpRequest final {
    friend bool operator==(const HelpRequest&, const HelpRequest&) = default;
  };

  class RunRequest final {
  public:
    RunRequest(ApplicationConfig application_config, std::filesystem::path scenario_path);

    [[nodiscard]] const ApplicationConfig& application_config() const& noexcept {
      return application_config_;
    }
    [[nodiscard]] const ApplicationConfig& application_config() const&& = delete;
    [[nodiscard]] const std::filesystem::path& scenario_path() const& noexcept {
      return scenario_path_;
    }
    [[nodiscard]] const std::filesystem::path& scenario_path() const&& = delete;

  private:
    ApplicationConfig application_config_;
    std::filesystem::path scenario_path_;
  };

  using Result = std::variant<HelpRequest, RunRequest>;

  static constexpr std::size_t kMaximumConfigurationFileBytes = 65'536;

  // Accepts only `--help` or exactly `--config <path> --scenario <path>`.
  // Throws ApplicationInputError or a typed domain validation error on invalid input.
  [[nodiscard]] static Result load(int argument_count, const char* const arguments[]);

  [[nodiscard]] static constexpr std::string_view help_text() noexcept {
    return "Usage: blob-royale --help\n"
           "       blob-royale --config <path> --scenario <path>\n";
  }
};

} // namespace blob_royale::application

#endif
