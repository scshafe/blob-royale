add_executable(blob_runtime_unit_tests
  simulation_runtime_tests.cpp
  snapshot_publication_tests.cpp
)

blob_royale_configure_first_party_target(blob_runtime_unit_tests)

target_link_libraries(blob_runtime_unit_tests PRIVATE
  Catch2::Catch2WithMain
  blob_runtime
)

catch_discover_tests(blob_runtime_unit_tests
  TEST_PREFIX "unit.runtime."
  PROPERTIES
    LABELS "unit;runtime;concurrency"
    TIMEOUT 15
  ADD_TAGS_AS_LABELS
  DISCOVERY_MODE PRE_TEST
)
