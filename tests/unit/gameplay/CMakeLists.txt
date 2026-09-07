add_executable(blob_gameplay_unit_tests
  game_mode_registry_tests.cpp
  royale/placement_recorder_system_tests.cpp
  royale/rotating_ring_spawn_policy_tests.cpp
  royale/royale_configuration_tests.cpp
  royale/royale_mode_tests.cpp
  royale/royale_objective_tests.cpp
  royale/zone_elimination_system_tests.cpp
  royale/zone_shrink_system_tests.cpp
  sandbox/sandbox_mode_tests.cpp
  shared/thrust_steering_system_tests.cpp
)

blob_royale_configure_first_party_target(blob_gameplay_unit_tests)

target_include_directories(blob_gameplay_unit_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_gameplay_unit_tests PRIVATE
  Catch2::Catch2WithMain
  blob_gameplay
)

catch_discover_tests(blob_gameplay_unit_tests
  TEST_PREFIX "unit.gameplay."
  PROPERTIES
    LABELS "unit;gameplay"
  DISCOVERY_MODE PRE_TEST
)
