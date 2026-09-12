add_executable(blob_gameplay_unit_tests
  game_mode_registry_tests.cpp
  king_of_the_hill/hill_geometry_tests.cpp
  king_of_the_hill/hill_movement_system_tests.cpp
  king_of_the_hill/hill_roam_configuration_tests.cpp
  king_of_the_hill/hill_roam_movement_system_tests.cpp
  king_of_the_hill/hill_roaming_tests.cpp
  king_of_the_hill/hill_objective_tests.cpp
  king_of_the_hill/hill_rules_publisher_system_tests.cpp
  king_of_the_hill/hill_return_timing_tests.cpp
  king_of_the_hill/hill_scoring_system_tests.cpp
  king_of_the_hill/king_of_the_hill_configuration_tests.cpp
  king_of_the_hill/king_of_the_hill_mode_tests.cpp
  race/checkpoint_progress_system_tests.cpp
  race/checkpoint_respawn_system_tests.cpp
  race/course_publisher_system_tests.cpp
  race/ordered_checkpoint_trigger_tests.cpp
  race/race_motion_chronology_tests.cpp
  race/grid_spawn_policy_tests.cpp
  race/race_configuration_tests.cpp
  race/race_course_tests.cpp
  race/race_mode_tests.cpp
  race/race_objective_tests.cpp
  race/race_return_timing_tests.cpp
  race/standings_recorder_system_tests.cpp
  royale/elimination_grace_publisher_system_tests.cpp
  royale/placement_recorder_system_tests.cpp
  royale/rotating_ring_spawn_policy_tests.cpp
  royale/royale_configuration_tests.cpp
  royale/royale_mode_tests.cpp
  royale/royale_objective_tests.cpp
  royale/zone_elimination_system_tests.cpp
  royale/zone_shrink_system_tests.cpp
  sandbox/sandbox_mode_tests.cpp
  sandbox/sandbox_falling_tests.cpp
  shared/disc_geometry_tests.cpp
  shared/create_crossing_hazard_tests.cpp
  shared/duration_ticks_tests.cpp
  shared/guarded_pair_contact_tests.cpp
  shared/hazard_archetype_tests.cpp
  shared/hazard_spawn_system_tests.cpp
  shared/input_lock_tests.cpp
  shared/hazard_stream_promotion_tests.cpp
  shared/lethal_hazard_contact_rule_tests.cpp
  shared/lifetime_expiry_system_tests.cpp
  shared/locomotion_promotion_tests.cpp
  shared/locomotion_tests.cpp
  shared/lobby_start_rule_tests.cpp
  shared/match_reset_system_tests.cpp
  shared/next_free_spawn_point_policy_tests.cpp
  shared/respawn_system_tests.cpp
  shared/status_system_tests.cpp
  shared/support_loss_trigger_tests.cpp
  shared/roster_tests.cpp
  shared/spawn_point_probe_tests.cpp
  shared/thrust_steering_system_tests.cpp
)

blob_royale_configure_first_party_target(blob_gameplay_unit_tests)

target_include_directories(blob_gameplay_unit_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_gameplay_unit_tests PRIVATE
  Catch2::Catch2WithMain
  blob_gameplay
)

catch_discover_tests(blob_gameplay_unit_tests
  TEST_PREFIX "unit.gameplay."
  PROPERTIES
    LABELS "unit;gameplay"
  DISCOVERY_MODE PRE_TEST
)
