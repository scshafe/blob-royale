

file(GLOB my_server_sources *.cpp)


add_library(my_server
            STATIC
          )


target_sources(my_server
               PUBLIC
               ${my_server_sources}
             )


target_include_directories(my_server
                           PUBLIC
                           my_boost_config
                           game_engine
                           ${CMAKE_CURRENT_SOURCE_DIR}
                         )


target_link_directories(my_server
                        PUBLIC
                        my_boost_config
                        game_engine
                      )

target_link_libraries(my_server
                      PUBLIC
                      my_boost_config
                      game_engine
                    )


