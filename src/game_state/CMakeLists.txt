

file(GLOB game_state_sources *.cpp)

ADD_DEFINITIONS(-DBOOST_LOG_DYN_LINK)


find_package(Boost REQUIRED COMPONENTS 
  log 
  log_setup        # log_setup is also needed for some of the Boost::log functionality
  program_options 
  json
)


add_library(game_state 
            STATIC )

target_sources(game_state
               PUBLIC
               ${game_state_sources}
               #            #${game_state_sources}
               #            phy_vector.cpp
               #            game_piece.cpp
               #            partition.cpp
               #            player.cpp
               #            game_state.cpp
            )

target_include_directories(game_state
                           PUBLIC
                           ${Boost_INCLUDE_DIRS}
                           ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_directories(game_state
                        PUBLIC
                        ${Boost_LIBRARY_DIRS}
                        Threads::Threads
                      )

target_link_libraries(game_state
                      PUBLIC
                      ${Boost_LIBRARIES}
                    )



