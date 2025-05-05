
cmake_minimum_required(VERSION 3.15)
project(blob-royale)


set(Boost_DEBUG 1)

#find_package(Boost REQUIRED COMPONENTS system)


# using the shared libary for logging
ADD_DEFINITIONS(-DBOOST_LOG_DYN_LINK)

# using the static libary for logging
#set(Boost_USE_STATIC_LIBS ON)

find_package(Boost REQUIRED COMPONENTS 
  log 
  log_setup        # log_setup is also needed for some of the Boost::log functionality
  program_options 
  json
)


add_executable (blob-royale
    ${BOOST_BEAST_FILES}
    main.cpp
)

add_subdirectory(src/game_state)


target_link_libraries(blob-royale
  ${Boost_LIBRARIES}
  game_state
)

# Boost_LIBRARIES is required for beast which is header only
target_include_directories(blob-royale PUBLIC 
  ${Boost_INCLUDE_DIRS}
  game_state
)

#set_property(TARGET blob-royale PROPERTY FOLDER "example-http-server")


