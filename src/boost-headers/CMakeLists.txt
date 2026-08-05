add_library(my_boost_config STATIC
  boost-log.cpp
  boost-program-options.cpp
)

blob_royale_configure_first_party_target(my_boost_config)

target_compile_definitions(my_boost_config PUBLIC
  BOOST_LOG_DYN_LINK
)

target_include_directories(my_boost_config PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(my_boost_config PUBLIC
  Boost::json
  Boost::log
  Boost::log_setup
  Boost::program_options
  Threads::Threads
)
