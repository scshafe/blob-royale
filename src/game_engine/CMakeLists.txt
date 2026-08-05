add_library(game_engine STATIC
  game_engine.cpp
  game_engine_parameters.cpp
  game_piece.cpp
  map_object.cpp
  partition.cpp
  phy_vector.cpp
  player.cpp
)

blob_royale_configure_first_party_target(game_engine)

target_include_directories(game_engine PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(game_engine SYSTEM PRIVATE
  "${PROJECT_SOURCE_DIR}/open-source-libs/rapidcsv"
)

target_link_libraries(game_engine PUBLIC
  dependency_graph_queue
  my_boost_config
  Threads::Threads
)
