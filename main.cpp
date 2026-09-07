#include "application_config_loader.hpp"
#include "application_input_error.hpp"
#include "application_lifecycle_error.hpp"
#include "blob_royale_application.hpp"
#include "controllers_validation_error.hpp"
#include "game_server_error.hpp"
#include "game_world.hpp"
#include "gameplay_validation_error.hpp"
#include "map_definition.hpp"
#include "map_loader.hpp"
#include "protocol_encoding_error.hpp"
#include "scenario_loader.hpp"
#include "server_config.hpp"
#include "simulation_runtime_lifecycle_error.hpp"
#include "simulation_runtime_state.hpp"
#include "simulation_validation_error.hpp"
#include "structured_logger.hpp"

#include <exception>
#include <iostream>
#include <string_view>
#include <utility>
#include <variant>

namespace {

constexpr int kInvalidInvocationExitCode = 64;
constexpr int kConfigurationExitCode = 78;
constexpr int kRuntimeFailureExitCode = 1;

void report_process_failure(blob_royale::observability::StructuredLogger& logger,
                            const std::string_view code, const std::string_view context,
                            const std::string_view detail) noexcept {
  logger.write({.severity = blob_royale::observability::LogSeverity::kError,
                .event = "process.failed",
                .lifecycle_state = "failed",
                .error_code = code,
                .context = context,
                .detail = detail});
}

} // namespace

int main(const int argument_count, const char* const arguments[]) {
  namespace application = blob_royale::application;
  blob_royale::observability::StructuredLogger logger{std::cerr};

  try {
    application::ApplicationConfigLoader::Result startup_request =
        application::ApplicationConfigLoader::load(argument_count, arguments);
    if (std::holds_alternative<application::ApplicationConfigLoader::HelpRequest>(
            startup_request)) {
      std::cout << application::ApplicationConfigLoader::help_text();
      return 0;
    }

    const auto& run_request =
        std::get<application::ApplicationConfigLoader::RunRequest>(startup_request);
    const auto& application_config = run_request.application_config();
    const blob_royale::simulation::MapDefinition map =
        application::MapLoader::load(application_config.match_configuration().map_directory());
    // A scenario seeds extra entities on top of the map; a match without one is the map's static
    // content plus whatever spawns into it.
    blob_royale::simulation::GameWorld initial_world =
        run_request.scenario_path().has_value()
            ? application::ScenarioLoader::load(*run_request.scenario_path(),
                                                application_config.simulation_config(), map,
                                                application_config.match_configuration().seed())
            : blob_royale::simulation::GameWorld::create(
                  application_config.simulation_config(), map,
                  application_config.match_configuration().seed());
    application::BlobRoyaleApplication blob_royale = application::BlobRoyaleApplication::create(
        application_config, map, std::move(initial_world), logger);
    blob_royale.run();
    return 0;
  } catch (const application::ApplicationInputError& error) {
    report_process_failure(logger, error.code(), error.context(), error.detail());
    return error.error_code() == application::ApplicationInputErrorCode::kCommandLineInvalid
               ? kInvalidInvocationExitCode
               : kConfigurationExitCode;
  } catch (const blob_royale::server::ServerConfigValidationError& error) {
    report_process_failure(logger, error.code(), error.context(), error.detail());
    return kConfigurationExitCode;
  } catch (const blob_royale::simulation::SimulationValidationError& error) {
    report_process_failure(logger, error.code(), error.context(), error.detail());
    return kConfigurationExitCode;
  } catch (const blob_royale::gameplay::GameplayValidationError& error) {
    report_process_failure(logger, error.code(), error.context(), error.detail());
    return kConfigurationExitCode;
  } catch (const blob_royale::controllers::ControllersValidationError& error) {
    report_process_failure(logger, error.code(), error.context(), error.detail());
    return kConfigurationExitCode;
  } catch (const application::ApplicationLifecycleError& error) {
    report_process_failure(logger, error.code(), error.context(), error.detail());
  } catch (const blob_royale::server::GameServerError& error) {
    report_process_failure(logger, error.code(), error.context(), error.detail());
  } catch (const blob_royale::runtime::SimulationRuntimeLifecycleError& error) {
    report_process_failure(
        logger, error.code(), error.operation(),
        blob_royale::runtime::simulation_runtime_state_name(error.current_state()));
  } catch (const blob_royale::protocol::ProtocolEncodingError& error) {
    report_process_failure(logger, error.code(), error.context(), error.detail());
  } catch (const std::exception& error) {
    report_process_failure(logger, "APPLICATION.UNEXPECTED_FAILURE", "main", error.what());
  } catch (...) {
    report_process_failure(logger, "APPLICATION.NON_STANDARD_FAILURE", "main",
                           "non-standard exception");
  }
  return kRuntimeFailureExitCode;
}
