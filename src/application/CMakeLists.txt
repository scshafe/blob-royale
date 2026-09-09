add_library(blob_application_input STATIC
  application_config.cpp
  application_config_loader.cpp
  application_input_error.cpp
  application_text_file_reader.cpp
  map_loader.cpp
  match_configuration.cpp
  match_startup_validation.cpp
  scenario_loader.cpp
)

blob_royale_configure_first_party_target(blob_application_input)

target_include_directories(blob_application_input PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

# The composition boundary is the one place that resolves a registered name, so it is the one place
# that links both registries: `[match] mode=` through `blob_gameplay` and `[match] bots=` through
# `blob_controllers` (`docs/architecture/0004-gameplay-architecture.md` § "Libraries, and where a
# new thing goes"). Rejecting an unknown mode or bot kind at parse time is what makes a misspelled
# roster a startup failure rather than a match that quietly plays the wrong game.
target_link_libraries(blob_application_input PUBLIC
  blob_controllers
  blob_gameplay
  blob_server_configuration
  blob_simulation
)

add_library(blob_application STATIC
  application_lifecycle_error.cpp
  blob_royale_application.cpp
  bot_reconciliation.cpp
)

blob_royale_configure_first_party_target(blob_application)

target_include_directories(blob_application PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_application PUBLIC
  blob_application_input
  blob_controllers
  blob_gameplay
  blob_observability
  blob_server
  blob_runtime
  blob_simulation
  Threads::Threads
)
