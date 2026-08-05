add_library(blob_protocol STATIC
  http_error.cpp
  protocol_encoding_error.cpp
  protocol_json_encoding.cpp
  public_configuration.cpp
  request_id.cpp
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
