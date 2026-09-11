add_library(blob_server_configuration STATIC
  server_config.cpp
)

blob_royale_configure_first_party_target(blob_server_configuration)

target_include_directories(blob_server_configuration PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_server_configuration PUBLIC
  blob_protocol
)

add_library(blob_server_http_preflight STATIC
  http_header_preflight.cpp
)

blob_royale_configure_first_party_target(blob_server_http_preflight)

target_include_directories(blob_server_http_preflight PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_server_http_preflight PRIVATE
  blob_protocol
)

add_library(blob_server STATIC
  game_api_router.cpp
  game_server.cpp
  game_server_error.cpp
  http_session.cpp
  lobby_directory.cpp
  match_session_context.cpp
  movement_tuning_result_adapter.cpp
  peer_identity.cpp
  peer_traffic_policy.cpp
  request_id_generator.cpp
  runtime_controller_directory_view.cpp
  server_execution_context.cpp
  session_websocket_session.cpp
  snapshot_delivery_state.cpp
  snapshot_egress_budget.cpp
  snapshot_websocket_session.cpp
  tcp_listener.cpp
)

blob_royale_configure_first_party_target(blob_server)

target_compile_definitions(blob_server PRIVATE
  BOOST_ASIO_NO_DEPRECATED
)

target_include_directories(blob_server PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_server
  PUBLIC
    blob_observability
    blob_protocol
    blob_runtime
    blob_server_configuration
    blob_server_http_preflight
    Threads::Threads
)
