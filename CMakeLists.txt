cmake_minimum_required(VERSION 3.28.3)

project(blob-royale LANGUAGES CXX)

include(CTest)

option(BLOB_ROYALE_BUILD_FUZZERS "Build Clang libFuzzer boundary targets" OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL
    "Export compile commands for deterministic analysis tooling" FORCE)

find_package(Boost 1.83 REQUIRED COMPONENTS json)
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
      # Floating-point expression contraction is off for every first-party target. The kernel's
      # drag step is a multiply-subtract, `1 - drag_per_second * dt`, and a compiler permitted to
      # contract it into one fused multiply-add computes a different binary64 value than one that
      # does not. Both GCC and Clang default to contraction being allowed, so leaving this
      # unpinned would make the committed velocity of a dragged body a property of the toolchain
      # rather than of the accepted physics, which is exactly what
      # `docs/architecture/0003-deterministic-simulation-contract.md` § "Floating-point contract"
      # forbids. It is free today because every fixture runs at zero drag; it is not free the day
      # a nonzero drag ships.
      -ffp-contract=off
    )
  else()
    message(FATAL_ERROR
      "Unsupported C++ compiler '${CMAKE_CXX_COMPILER_ID}'; use GCC or Clang")
  endif()
endfunction()

add_subdirectory(src/observability)
add_subdirectory(src/simulation)
add_subdirectory(src/gameplay)
add_subdirectory(src/runtime)
add_subdirectory(src/controllers)
add_subdirectory(src/protocol)
add_subdirectory(src/server)
add_subdirectory(src/application)
add_subdirectory(benchmarks)

add_executable(blob-royale
  main.cpp
)

blob_royale_configure_first_party_target(blob-royale)

target_link_libraries(blob-royale PRIVATE
  blob_application
)

if(BUILD_TESTING)
  set(BLOB_ROYALE_CATCH2_SOURCE_DIRECTORY
      "/opt/blob-royale-deps/catch2" CACHE PATH
      "Checksum-verified Catch2 source provisioned by the pinned Linux image")
  set(BLOB_ROYALE_CATCH2_INSTALL_PREFIX
      "/opt/blob-royale-deps/catch2-install" CACHE PATH
      "Prebuilt Catch2 package provisioned by the pinned Linux image")

  function(blob_royale_require_dependency_metadata
      metadata_path metadata_description expected_value)
    if(NOT EXISTS "${metadata_path}")
      message(FATAL_ERROR
        "Pinned ${metadata_description} metadata is missing: ${metadata_path}")
    endif()
    file(READ "${metadata_path}" actual_value)
    string(STRIP "${actual_value}" actual_value)
    if(NOT "${actual_value}" STREQUAL "${expected_value}")
      message(FATAL_ERROR
        "Pinned ${metadata_description} mismatch: expected ${expected_value}, "
        "received ${actual_value}")
    endif()
  endfunction()

  blob_royale_require_dependency_metadata(
    "${BLOB_ROYALE_CATCH2_SOURCE_DIRECTORY}/.blob-royale-source-version"
    "Catch2 version"
    "3.15.3"
  )
  blob_royale_require_dependency_metadata(
    "${BLOB_ROYALE_CATCH2_SOURCE_DIRECTORY}/.blob-royale-source-commit"
    "Catch2 commit"
    "8b08d4d79514f45f7e4ce2a607ac9c94e920d1bb"
  )
  blob_royale_require_dependency_metadata(
    "${BLOB_ROYALE_CATCH2_SOURCE_DIRECTORY}/.blob-royale-source-sha256"
    "Catch2 archive SHA-256"
    "6789b4711430fe4fba8b5efe609038bf52c81c0cbdcdedf9b5d116794dfd5f2e"
  )

  find_package(
    Catch2 3.15.3 EXACT
    CONFIG REQUIRED
    PATHS "${BLOB_ROYALE_CATCH2_INSTALL_PREFIX}/lib/cmake/Catch2"
    NO_DEFAULT_PATH
  )
  include("${Catch2_DIR}/Catch.cmake")

  add_subdirectory(tests)
endif()

if(BLOB_ROYALE_BUILD_FUZZERS)
  add_subdirectory(tests/fuzz)
endif()
