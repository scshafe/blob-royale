add_subdirectory(observability)

add_executable(blob_simulation_unit_tests
  simulation/command_kind_mask_tests.cpp
  simulation/command_registry_tests.cpp
  simulation/component_registry_tests.cpp
  simulation/component_store_tests.cpp
  simulation/entity_id_reservation_tests.cpp
  simulation/game_simulation_tests.cpp
  simulation/input_batch_tests.cpp
  simulation/physics_tests.cpp
  simulation/simulation_system_tests.cpp
  simulation/snapshot_tests.cpp
  simulation/spatial_grid_tests.cpp
  simulation/system_pipeline_tests.cpp
  simulation/tick_context_tests.cpp
  simulation/world_event_registry_tests.cpp
  simulation/world_model_tests.cpp
)

blob_royale_configure_first_party_target(blob_simulation_unit_tests)

target_link_libraries(blob_simulation_unit_tests PRIVATE
  Catch2::Catch2WithMain
  blob_simulation
)

catch_discover_tests(blob_simulation_unit_tests
  TEST_PREFIX "unit.simulation."
  PROPERTIES
    LABELS "unit;simulation"
  DISCOVERY_MODE PRE_TEST
)

add_executable(blob_application_unit_tests
  application/application_config_loader_tests.cpp
  application/blob_royale_application_tests.cpp
  application/scenario_loader_tests.cpp
)

blob_royale_configure_first_party_target(blob_application_unit_tests)

target_include_directories(blob_application_unit_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_application_unit_tests PRIVATE
  Boost::json
  Catch2::Catch2WithMain
  blob_application
)

catch_discover_tests(blob_application_unit_tests
  TEST_PREFIX "unit.application."
  PROPERTIES
    LABELS "unit;application"
    TIMEOUT 15
  ADD_TAGS_AS_LABELS
  DISCOVERY_MODE PRE_TEST
)

add_subdirectory(protocol)
add_subdirectory(runtime)
add_subdirectory(server)
