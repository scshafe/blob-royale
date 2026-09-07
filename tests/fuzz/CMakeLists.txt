if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "Blob Royale fuzz targets require the pinned Clang toolchain")
endif()

function(blob_royale_add_fuzzer target_name source_file)
  add_executable(${target_name}
    ${source_file}
  )

  blob_royale_configure_first_party_target(${target_name})

  target_link_libraries(${target_name} PRIVATE
    ${ARGN}
  )

  target_link_options(${target_name} PRIVATE
    -fsanitize=fuzzer,address,undefined
  )
endfunction()

blob_royale_add_fuzzer(
  blob_application_config_fuzzer
  application_config_fuzzer.cpp
  blob_application_input
  # The loader resolves `[match] mode` and `[match] bots` through these registries, so their typed
  # rejections are part of its contract and the harness must be able to name them.
  blob_gameplay
  blob_controllers
)
blob_royale_add_fuzzer(
  blob_application_cli_fuzzer
  application_cli_fuzzer.cpp
  blob_application_input
)
blob_royale_add_fuzzer(
  blob_http_request_fuzzer
  http_request_fuzzer.cpp
  blob_protocol
  blob_server_http_preflight
)
blob_royale_add_fuzzer(
  blob_protocol_request_id_fuzzer
  protocol_request_id_fuzzer.cpp
  blob_protocol
)
blob_royale_add_fuzzer(
  blob_protocol_command_fuzzer
  protocol_command_fuzzer.cpp
  blob_protocol
)
blob_royale_add_fuzzer(
  blob_scenario_fuzzer
  scenario_fuzzer.cpp
  blob_application_input
)

add_custom_target(blob_royale_fuzzers DEPENDS
  blob_application_config_fuzzer
  blob_application_cli_fuzzer
  blob_http_request_fuzzer
  blob_protocol_command_fuzzer
  blob_protocol_request_id_fuzzer
  blob_scenario_fuzzer
)
