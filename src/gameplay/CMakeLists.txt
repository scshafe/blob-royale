add_library(blob_gameplay STATIC
  game_mode_registry.cpp
  royale/placement_recorder_system.cpp
  royale/royale_configuration.cpp
  royale/royale_mode.cpp
  royale/zone_elimination_system.cpp
  royale/zone_shrink_system.cpp
  sandbox/sandbox_mode.cpp
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
