add_library(blob_gameplay STATIC
  game_mode_registry.cpp
  king_of_the_hill/hill_geometry.cpp
  king_of_the_hill/hill_movement_system.cpp
  king_of_the_hill/hill_objective.cpp
  king_of_the_hill/hill_rules_publisher_system.cpp
  king_of_the_hill/hill_scoring_system.cpp
  king_of_the_hill/king_of_the_hill_configuration.cpp
  king_of_the_hill/king_of_the_hill_mode.cpp
  race/race_configuration.cpp
  race/race_course.cpp
  royale/elimination_grace_publisher_system.cpp
  royale/placement_recorder_system.cpp
  royale/royale_configuration.cpp
  royale/royale_mode.cpp
  royale/zone_elimination_system.cpp
  royale/zone_shrink_system.cpp
  sandbox/sandbox_mode.cpp
  shared/duration_ticks.cpp
  shared/hazard_archetype.cpp
  shared/hazard_crossing.cpp
  shared/hazard_spawn_system.cpp
  shared/lethal_hazard_contact_rule.cpp
  shared/lifetime_expiry_system.cpp
  shared/match_reset_system.cpp
  shared/respawn_system.cpp
  shared/thrust_steering_system.cpp
)

blob_royale_configure_first_party_target(blob_gameplay)

target_include_directories(blob_gameplay PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

# The one-way dependency of `docs/architecture/0002-simulation-architecture.md`: rules depend on
# values and nothing else. `blob_gameplay` links no runtime, no protocol, no server, and no Boost.
target_link_libraries(blob_gameplay PUBLIC
  blob_simulation
)
