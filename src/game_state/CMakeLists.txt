

file(GLOB game_state_sources *.cpp)


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
                           my_boost_config
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           "../../open-source-libs/rapidcsv"
)

target_link_directories(game_state
                        PUBLIC
                        my_boost_config
                        Threads::Threads
                      )

target_link_libraries(game_state
                      PUBLIC
                      my_boost_config
                    )



