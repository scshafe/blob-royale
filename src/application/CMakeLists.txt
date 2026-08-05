add_library(blob_application_input STATIC
  application_config.cpp
  application_config_loader.cpp
  application_input_error.cpp
  application_text_file_reader.cpp
  scenario_loader.cpp
)

blob_royale_configure_first_party_target(blob_application_input)

target_include_directories(blob_application_input PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_application_input PUBLIC
  blob_server_configuration
  blob_simulation
)

add_library(blob_application STATIC
  application_lifecycle_error.cpp
  blob_royale_application.cpp
)

blob_royale_configure_first_party_target(blob_application)

target_include_directories(blob_application PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_application PUBLIC
  blob_application_input
  blob_observability
  blob_server
  blob_runtime
  blob_simulation
  Threads::Threads
)
