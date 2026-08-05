add_executable(blob_observability_unit_tests
  structured_logger_tests.cpp
)

blob_royale_configure_first_party_target(blob_observability_unit_tests)

target_link_libraries(blob_observability_unit_tests PRIVATE
  Catch2::Catch2WithMain
  blob_observability
)

catch_discover_tests(blob_observability_unit_tests
  TEST_PREFIX "unit.observability."
  PROPERTIES
    LABELS "unit;observability"
  DISCOVERY_MODE PRE_TEST
)
