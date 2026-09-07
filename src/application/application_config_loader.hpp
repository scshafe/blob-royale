#ifndef BLOB_ROYALE_APPLICATION_APPLICATION_CONFIG_LOADER_HPP
#define BLOB_ROYALE_APPLICATION_APPLICATION_CONFIG_LOADER_HPP

#include "application_config.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
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
    RunRequest(ApplicationConfig application_config,
               std::optional<std::filesystem::path> scenario_path);

    [[nodiscard]] const ApplicationConfig& application_config() const& noexcept {
      return application_config_;
    }
    [[nodiscard]] const ApplicationConfig& application_config() const&& = delete;

    // The scenario to seed extra entities from, or `std::nullopt` when none was named.
    //
    // **A match no longer requires one.** `[match]` names the game and the map, and the map's
    // spawn points seat every entity a spawn command creates, so a live deployment has nothing to
    // seed. A scenario remains how a fixture places specific bodies at specific ids and velocities,
    // which no spawn policy can express (`scenario_loader.hpp`).
    [[nodiscard]] const std::optional<std::filesystem::path>& scenario_path() const& noexcept {
      return scenario_path_;
    }
    [[nodiscard]] const std::optional<std::filesystem::path>& scenario_path() const&& = delete;

  private:
    ApplicationConfig application_config_;
    std::optional<std::filesystem::path> scenario_path_;
  };

  using Result = std::variant<HelpRequest, RunRequest>;

  static constexpr std::size_t kMaximumConfigurationFileBytes = 65'536;

  // Accepts only `--help`, `--config <path>`, or `--config <path> --scenario <path>`.
  // Throws ApplicationInputError or a typed domain validation error on invalid input.
  [[nodiscard]] static Result load(int argument_count, const char* const arguments[]);

  [[nodiscard]] static constexpr std::string_view help_text() noexcept {
    return "Usage: blob-royale --help\n"
           "       blob-royale --config <path> [--scenario <path>]\n";
  }
};

} // namespace blob_royale::application

#endif
