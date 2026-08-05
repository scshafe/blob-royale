add_library(blob_simulation STATIC
  candidate_pair.cpp
  entity_id.cpp
  game_simulation.cpp
  game_world.cpp
  physics.cpp
  physics_body.cpp
  player.cpp
  player_snapshot.cpp
  simulation_config.cpp
  spatial_grid.cpp
  tick_sequence.cpp
  vector2.cpp
  world_snapshot.cpp
)

blob_royale_configure_first_party_target(blob_simulation)

target_include_directories(blob_simulation PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
