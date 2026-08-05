add_library(my_server STATIC
  helpers.cpp
  my_http_server.cpp
  my_listener.cpp
  my_websocket.cpp
)

blob_royale_configure_first_party_target(my_server)

target_include_directories(my_server PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(my_server PUBLIC
  game_engine
  my_boost_config
  Threads::Threads
)
