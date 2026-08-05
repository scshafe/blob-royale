add_library(dependency_graph_queue STATIC
  cycle_dependency.cpp
  priority_lock.cpp
)

blob_royale_configure_first_party_target(dependency_graph_queue)

target_include_directories(dependency_graph_queue PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(dependency_graph_queue PUBLIC
  my_boost_config
  Threads::Threads
)
