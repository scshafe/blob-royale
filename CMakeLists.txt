cmake_minimum_required(VERSION 3.28.3)

project(blob-royale LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL
    "Export compile commands for deterministic analysis tooling" FORCE)

find_package(Boost 1.83 REQUIRED COMPONENTS json log log_setup program_options)
find_package(Threads REQUIRED)

# canonical: first-party-native-target-policy -- every maintained C++ target uses
# this policy so compiler requirements cannot drift between directories.
function(blob_royale_configure_first_party_target target_name)
  get_target_property(target_type ${target_name} TYPE)
  if(target_type STREQUAL "EXECUTABLE")
    target_compile_features(${target_name} PRIVATE cxx_std_20)
  else()
    target_compile_features(${target_name} PUBLIC cxx_std_20)
  endif()
  set_target_properties(${target_name} PROPERTIES
    CXX_EXTENSIONS OFF
    # The project has no C++ module units, so dependency scanning is unnecessary.
    CXX_SCAN_FOR_MODULES OFF
  )

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${target_name} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Werror
    )
  else()
    message(FATAL_ERROR
      "Unsupported C++ compiler '${CMAKE_CXX_COMPILER_ID}'; use GCC or Clang")
  endif()
endfunction()

add_subdirectory(src/boost-headers)
add_subdirectory(src/dependency_graph_queue)
add_subdirectory(src/game_engine)
add_subdirectory(src/server)

add_executable(blob-royale
  main.cpp
)

blob_royale_configure_first_party_target(blob-royale)

target_link_libraries(blob-royale PRIVATE
  my_boost_config
  game_engine
  my_server
  Threads::Threads
)
