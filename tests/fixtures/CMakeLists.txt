add_executable(blob_royale_fixture_tests
  scenario_fixture_tests.cpp
)

blob_royale_configure_first_party_target(blob_royale_fixture_tests)

target_compile_definitions(blob_royale_fixture_tests PRIVATE
  BLOB_ROYALE_SCENARIO_FIXTURE_DIRECTORY="${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_royale_fixture_tests PRIVATE
  Catch2::Catch2WithMain
)

catch_discover_tests(blob_royale_fixture_tests
  TEST_PREFIX "fixtures."
  PROPERTIES
    LABELS "fixtures"
  DISCOVERY_MODE PRE_TEST
)
