add_library(blob_simulation STATIC
  candidate_pair.cpp
  contact_rule_table.cpp
  controller_id.cpp
  entity_id.cpp
  game_simulation.cpp
  game_simulation_setup.cpp
  game_world.cpp
  input_batch.cpp
  map_definition.cpp
  match_lifecycle_system.cpp
  physics.cpp
  physics_body.cpp
  player_snapshot.cpp
  simulation_config.cpp
  spatial_grid.cpp
  spawn_seating.cpp
  spawn_system.cpp
  swept_geometry.cpp
  system_pipeline.cpp
  team_id.cpp
  tick_sequence.cpp
  vector2.cpp
  world_snapshot.cpp
)

blob_royale_configure_first_party_target(blob_simulation)

target_include_directories(blob_simulation PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
