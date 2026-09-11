add_executable(blob_runtime_unit_tests
  command_mailbox_tests.cpp
  command_sink_tests.cpp
  controller_directory_tests.cpp
  entity_id_allocator_tests.cpp
  movement_tuning_exchange_tests.cpp
  simulation_runtime_tests.cpp
  snapshot_publication_tests.cpp
  tick_deadline_tests.cpp
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
