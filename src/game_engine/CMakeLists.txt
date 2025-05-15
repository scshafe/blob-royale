

file(GLOB game_engine_sources *.cpp)


find_package(Threads REQUIRED)

add_library(game_engine 
            STATIC )

target_sources(game_engine
               PUBLIC
               ${game_engine_sources}
            )

target_include_directories(game_engine
                           PUBLIC
                           my_boost_config
                           dependency_graph_queue
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           "../../open-source-libs/rapidcsv"
)

target_link_directories(game_engine
                        PUBLIC
                        my_boost_config
                        dependency_graph_queue
                        Threads::Threads
                      )

target_link_libraries(game_engine
                      PUBLIC
                      my_boost_config
                      dependency_graph_queue
                      Threads::Threads
                    )



