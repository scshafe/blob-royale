add_executable(blob_controllers_unit_tests
  chaser_controller_tests.cpp
  controller_host_tests.cpp
  controller_registry_tests.cpp
  controller_steering_promotion_tests.cpp
  controller_thrust_request_tests.cpp
  controller_tests.cpp
  hill_seeker_controller_tests.cpp
  human_bot_symmetry_tests.cpp
  observation_tests.cpp
  racer_controller_tests.cpp
  racer_projection_promotion_tests.cpp
  scripted_replay_controller_tests.cpp
  tactical_arrival_brake_dynamics_tests.cpp
  tactical_controller_tests.cpp
  tactical_objective_candidates_tests.cpp
  tactical_profile_tests.cpp
  tactical_profile_catalogue_tests.cpp
  tactical_seed_identity_tests.cpp
  wanderer_controller_tests.cpp
)

blob_royale_configure_first_party_target(blob_controllers_unit_tests)

target_include_directories(blob_controllers_unit_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../gameplay"
)

# `blob_gameplay` is a **test-only** link and must never appear in `src/controllers/CMakeLists.txt`:
# the end-to-end pass seats bots in the real `sandbox` mode behind a real `SimulationRuntime`, and an
# in-test mode written to avoid this link would be a second copy of `SandboxMode` and of
# `thrust_steering` (`docs/architecture/0002-simulation-architecture.md` § "Decision" keeps
# `blob_controllers` on `blob_runtime` and `blob_simulation` alone).
target_link_libraries(blob_controllers_unit_tests PRIVATE
  Catch2::Catch2WithMain
  blob_controllers
  blob_gameplay
)

catch_discover_tests(blob_controllers_unit_tests
  TEST_PREFIX "unit.controllers."
  PROPERTIES
    LABELS "unit;controllers"
    TIMEOUT 30
  ADD_TAGS_AS_LABELS
  DISCOVERY_MODE PRE_TEST
)
