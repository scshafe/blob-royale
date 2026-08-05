add_executable(blob_simulation_benchmarks
  blob_simulation_benchmarks.cpp
)

blob_royale_configure_first_party_target(blob_simulation_benchmarks)

target_link_libraries(blob_simulation_benchmarks PRIVATE
  blob_protocol
  blob_server
  blob_simulation
  Boost::json
)

target_compile_definitions(blob_simulation_benchmarks PRIVATE
  BLOB_ROYALE_BENCHMARK_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
  BLOB_ROYALE_BENCHMARK_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
  BLOB_ROYALE_BENCHMARK_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
)
