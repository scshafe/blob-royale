add_library(blob_observability STATIC
  structured_logger.cpp
)

blob_royale_configure_first_party_target(blob_observability)

target_include_directories(blob_observability PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
