add_library(blob_runtime STATIC
  command_mailbox.cpp
  command_sink.cpp
  command_sink_error.cpp
  controller_directory.cpp
  entity_id_allocator.cpp
  movement_tuning_result_delivery.cpp
  simulation_runtime.cpp
  simulation_runtime_lifecycle_error.cpp
  snapshot_publication.cpp
)

blob_royale_configure_first_party_target(blob_runtime)

target_include_directories(blob_runtime PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_runtime PUBLIC
  blob_simulation
  Threads::Threads
)
