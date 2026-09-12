add_library(blob_controllers STATIC
  chaser_controller.cpp
  controller.cpp
  controller_host.cpp
  controller_observation_queries.cpp
  controller_registry.cpp
  controller_steering.cpp
  hill_seeker_controller.cpp
  observation.cpp
  racer_controller.cpp
  scripted_replay_controller.cpp
  tactical_controller.cpp
  tactical_objective_candidates.cpp
  tactical_profile.cpp
  tactical_profile_catalogue.cpp
  tactical_seed_identity.cpp
  wanderer_controller.cpp
)

blob_royale_configure_first_party_target(blob_controllers)

target_include_directories(blob_controllers PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

# The one-way dependency of `docs/architecture/0002-simulation-architecture.md`: decisions depend on
# the runtime's two capabilities and on simulation values, and on nothing else. `blob_controllers`
# links no gameplay, no protocol, no server, no observability, and no Boost. Both edges the ADR's
# target table names are listed even though `blob_runtime` already carries `blob_simulation`
# publicly, so the build file states the same pair the table does.
target_link_libraries(blob_controllers PUBLIC
  blob_runtime
  blob_simulation
)
