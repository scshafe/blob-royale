

file(GLOB game_state_sources *.cpp)


add_library(game_state 
            STATIC )

target_sources(game_state
               PUBLIC
            #${game_state_sources}
            phy_vector.cpp
            game_piece.cpp
            partition.cpp
            player.cpp
            game_state.cpp
            )

target_include_directories(game_state
                           PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR}
)



