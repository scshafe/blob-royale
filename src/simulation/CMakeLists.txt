add_library(blob_simulation STATIC
  arena_bounds.cpp
  candidate_pair.cpp
  contact_rule_table.cpp
  continuous_motion.cpp
  controller_id.cpp
  entity_id.cpp
  game_simulation.cpp
  game_simulation_setup.cpp
  game_world.cpp
  input_batch.cpp
  map_definition.cpp
  match_lifecycle_system.cpp
  motion_triggers.cpp
  movement_tuning.cpp
  physics.cpp
  physics_body.cpp
  player_snapshot.cpp
  simulation_config.cpp
  spatial_grid.cpp
  spawn_seating.cpp
  spawn_system.cpp
  swept_geometry.cpp
  terrain_definition.cpp
  terrain_queries.cpp
  system_pipeline.cpp
  team_id.cpp
  tick_sequence.cpp
  tick_window.cpp
  vector2.cpp
  world_snapshot.cpp
)

blob_royale_configure_first_party_target(blob_simulation)

target_include_directories(blob_simulation PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
