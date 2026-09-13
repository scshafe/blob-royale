add_executable(blob_royale_fixture_tests
  cross_mode_production_replay_fixture_tests.cpp
  deployment_fixture_tests.cpp
  king_of_the_hill_replay_fixture_tests.cpp
  map_fixture_tests.cpp
  race_replay_fixture_tests.cpp
  replay_fixture.cpp
  replay_map_fixture_tests.cpp
  royale_replay_fixture_tests.cpp
  scenario_fixture_tests.cpp
  terrain_geometry_fixture_tests.cpp
)

blob_royale_configure_first_party_target(blob_royale_fixture_tests)

target_compile_definitions(blob_royale_fixture_tests PRIVATE
  BLOB_ROYALE_BENCHMARK_FIXTURE_DIRECTORY="${CMAKE_SOURCE_DIR}/benchmarks/fixtures"
  BLOB_ROYALE_DEPLOYMENT_FIXTURE_DIRECTORY="${CMAKE_SOURCE_DIR}/deploy/ubuntu-pc"
  BLOB_ROYALE_MAPS_DIRECTORY="${CMAKE_SOURCE_DIR}/maps"
  BLOB_ROYALE_REPLAY_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/replays"
  BLOB_ROYALE_SCENARIO_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}"
  BLOB_ROYALE_SHIPPED_CONFIGURATION_FIXTURE_DIRECTORY="${CMAKE_SOURCE_DIR}/config"
  BLOB_ROYALE_TERRAIN_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}/terrain"
)

target_include_directories(blob_royale_fixture_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_royale_fixture_tests PRIVATE
  Catch2::Catch2WithMain
  Boost::json
  blob_application
  blob_gameplay
)

catch_discover_tests(blob_royale_fixture_tests
  TEST_PREFIX "fixtures."
  PROPERTIES
    LABELS "fixtures"
  DISCOVERY_MODE PRE_TEST
)
