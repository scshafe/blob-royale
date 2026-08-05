add_library(blob_server_integration_support STATIC
  fixture_text_file.cpp
  integration_test_error.cpp
  loopback_http_client.cpp
  protocol_contract_validation.cpp
  server_exit_result.cpp
  server_fixture_state.cpp
  snapshot_websocket_client.cpp
  structured_server_log_observer.cpp
)

blob_royale_configure_first_party_target(blob_server_integration_support)

target_include_directories(blob_server_integration_support PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_server_integration_support PUBLIC
  Boost::json
  Threads::Threads
)

add_executable(blob_server_process_fixture
  server_process_fixture.cpp
)

blob_royale_configure_first_party_target(blob_server_process_fixture)

target_link_libraries(blob_server_process_fixture PRIVATE
  blob_server_integration_support
)

add_executable(blob_server_protocol_integration
  server_protocol_contract_tests.cpp
)

blob_royale_configure_first_party_target(blob_server_protocol_integration)

target_link_libraries(blob_server_protocol_integration PRIVATE
  blob_server_integration_support
)

add_executable(blob_server_abuse_soak
  server_abuse_soak_tests.cpp
)

blob_royale_configure_first_party_target(blob_server_abuse_soak)

target_link_libraries(blob_server_abuse_soak PRIVATE
  blob_server_integration_support
)

add_dependencies(blob_server_process_fixture blob-royale)
add_dependencies(blob_server_protocol_integration blob-royale)
add_dependencies(blob_server_abuse_soak blob-royale)

set(blob_server_fixture_directory
    "${CMAKE_CURRENT_BINARY_DIR}/server-process-fixture")

add_test(
  NAME integration.server_process_fixture.setup
  COMMAND
    "$<TARGET_FILE:blob_server_process_fixture>"
    setup
    --server-executable "$<TARGET_FILE:blob-royale>"
    --fixture-directory "${blob_server_fixture_directory}"
)

set_tests_properties(integration.server_process_fixture.setup PROPERTIES
  FIXTURES_SETUP blob_royale_server_process
  LABELS "integration;server;process"
  RESOURCE_LOCK blob_royale_server_process_fixture
  TIMEOUT 25
)

add_test(
  NAME integration.server_protocol_contracts
  COMMAND
    "$<TARGET_FILE:blob_server_protocol_integration>"
    --fixture-directory "${blob_server_fixture_directory}"
)

set_tests_properties(integration.server_protocol_contracts PROPERTIES
  FIXTURES_REQUIRED blob_royale_server_process
  LABELS "integration;server;http;websocket;protocol"
  RESOURCE_LOCK blob_royale_server_process_fixture
  TIMEOUT 30
)

add_test(
  NAME integration.server_process_fixture.cleanup
  COMMAND
    "$<TARGET_FILE:blob_server_process_fixture>"
    cleanup
    --fixture-directory "${blob_server_fixture_directory}"
)

set_tests_properties(integration.server_process_fixture.cleanup PROPERTIES
  FIXTURES_CLEANUP blob_royale_server_process
  LABELS "integration;server;process"
  RESOURCE_LOCK blob_royale_server_process_fixture
  TIMEOUT 20
)

set(blob_server_backpressure_fixture_directory
    "${CMAKE_CURRENT_BINARY_DIR}/server-backpressure-fixture")

add_test(
  NAME integration.server_backpressure_fixture.setup
  COMMAND
    "$<TARGET_FILE:blob_server_process_fixture>"
    setup
    --server-executable "$<TARGET_FILE:blob-royale>"
    --fixture-directory "${blob_server_backpressure_fixture_directory}"
    --workload backpressure
)

set_tests_properties(integration.server_backpressure_fixture.setup PROPERTIES
  FIXTURES_SETUP blob_royale_server_backpressure_process
  LABELS "abuse;backpressure;integration;server;soak"
  RESOURCE_LOCK blob_royale_server_process_fixture
  TIMEOUT 25
)

add_test(
  NAME integration.server_abuse_soak
  COMMAND
    "$<TARGET_FILE:blob_server_abuse_soak>"
    --fixture-directory "${blob_server_backpressure_fixture_directory}"
)

set_tests_properties(integration.server_abuse_soak PROPERTIES
  FIXTURES_REQUIRED blob_royale_server_backpressure_process
  LABELS "abuse;backpressure;churn;http;integration;server;soak;websocket"
  RESOURCE_LOCK blob_royale_server_process_fixture
  TIMEOUT 45
)

add_test(
  NAME integration.server_backpressure_fixture.cleanup
  COMMAND
    "$<TARGET_FILE:blob_server_process_fixture>"
    cleanup
    --fixture-directory "${blob_server_backpressure_fixture_directory}"
)

set_tests_properties(integration.server_backpressure_fixture.cleanup PROPERTIES
  FIXTURES_CLEANUP blob_royale_server_backpressure_process
  LABELS "abuse;backpressure;integration;server;soak"
  RESOURCE_LOCK blob_royale_server_process_fixture
  TIMEOUT 20
)
