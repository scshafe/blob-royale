add_library(blob_protocol STATIC
  bounded_json_serialization.cpp
  command_decoding.cpp
  http_error.cpp
  protocol_encoding_error.cpp
  protocol_json_encoding.cpp
  protocol_v3_frame_conformance.cpp
  protocol_v3_json_encoding.cpp
  public_configuration.cpp
  request_id.cpp
  session_welcome.cpp
  utc_timestamp.cpp
  v3_http_error.cpp
)

blob_royale_configure_first_party_target(blob_protocol)

target_include_directories(blob_protocol PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(blob_protocol
  PUBLIC
    blob_simulation
  PRIVATE
    Boost::json
)
