add_executable(blob_protocol_unit_tests
  command_decoding_tests.cpp
  http_error_tests.cpp
  protocol_json_encoding_tests.cpp
  protocol_v2_json_encoding_tests.cpp
  public_configuration_tests.cpp
  request_id_tests.cpp
)

blob_royale_configure_first_party_target(blob_protocol_unit_tests)

target_compile_definitions(blob_protocol_unit_tests PRIVATE
  BLOB_ROYALE_PROTOCOL_SCHEMA_DIRECTORY="${PROJECT_SOURCE_DIR}/docs/protocol/schema/v1"
  BLOB_ROYALE_PROTOCOL_V2_SCHEMA_DIRECTORY="${PROJECT_SOURCE_DIR}/docs/protocol/schema/v2"
)

target_include_directories(blob_protocol_unit_tests PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_protocol_unit_tests PRIVATE
  Boost::json
  Catch2::Catch2WithMain
  blob_protocol
)

catch_discover_tests(blob_protocol_unit_tests
  TEST_PREFIX "unit.protocol."
  PROPERTIES
    LABELS "unit;protocol"
  DISCOVERY_MODE PRE_TEST
)
