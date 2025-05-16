

file(GLOB dependency_graph_queue_sources *.cpp)


find_package(Threads REQUIRED)

add_library(dependency_graph_queue
            STATIC )

target_sources(dependency_graph_queue
               PUBLIC
               ${dependency_graph_queue_sources}
            )

target_include_directories(dependency_graph_queue
                           PUBLIC
                           my_boost_config
                           ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_directories(dependency_graph_queue
                        PUBLIC
                        my_boost_config
                        #Threads::Threads
                      )

target_link_libraries(dependency_graph_queue
                      PUBLIC
                      my_boost_config
                      #Threads::Threads
                    )



