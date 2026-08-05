add_executable(blob_royale_unit_tests
  game_engine/phy_vector_tests.cpp
)

blob_royale_configure_first_party_target(blob_royale_unit_tests)

target_link_libraries(blob_royale_unit_tests PRIVATE
  Catch2::Catch2WithMain
  game_engine
)

catch_discover_tests(blob_royale_unit_tests
  TEST_PREFIX "unit."
  PROPERTIES
    LABELS "unit"
  DISCOVERY_MODE PRE_TEST
)
