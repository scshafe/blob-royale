
cmake_minimum_required(VERSION 3.15)
project(blob-royale)

find_package(Threads REQUIRED)

add_executable (blob-royale
    main.cpp
)

add_subdirectory(src/boost-headers)
add_subdirectory(src/game_state)
add_subdirectory(src/server)

target_link_libraries(blob-royale
                      Threads::Threads
                      my_boost_config
                      game_state
                      my_server
                      )

# Boost_LIBRARIES is required for beast which is header only
target_include_directories(blob-royale PUBLIC 
  my_boost_config
  game_state
  my_server
)



