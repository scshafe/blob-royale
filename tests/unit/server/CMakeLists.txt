add_executable(blob_server_unit_tests
  npc_catalogue_session_tests.cpp
  game_api_router_tests.cpp
  game_server_tests.cpp
  http_header_preflight_tests.cpp
  lobby_directory_tests.cpp
  movement_tuning_result_adapter_tests.cpp
  peer_identity_tests.cpp
  peer_traffic_policy_tests.cpp
  server_config_tests.cpp
  server_execution_context_tests.cpp
  session_websocket_session_tests.cpp
  snapshot_delivery_state_tests.cpp
  snapshot_egress_budget_tests.cpp
  snapshot_websocket_session_tests.cpp
)

blob_royale_configure_first_party_target(blob_server_unit_tests)

target_include_directories(blob_server_unit_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(blob_server_unit_tests PRIVATE
  Boost::json
  Catch2::Catch2WithMain
  blob_server
)

catch_discover_tests(blob_server_unit_tests
  TEST_PREFIX "unit.server."
  PROPERTIES
    LABELS "unit;server;network"
    TIMEOUT 20
  ADD_TAGS_AS_LABELS
  DISCOVERY_MODE PRE_TEST
)
