

file(GLOB my_boost_config_sources *.cpp)

find_package(Boost REQUIRED COMPONENTS 
  log 
  log_setup        # log_setup is also needed for some of the Boost::log functionality
  program_options 
  json
)

set(Boost_DEBUG 1)

# using the shared libary for logging
ADD_DEFINITIONS(-DBOOST_LOG_DYN_LINK)

# using the static libary for logging
#set(Boost_USE_STATIC_LIBS ON)


add_library(my_boost_config
            STATIC
            ${my_boost_config_sources}
          )

target_include_directories(my_boost_config
                           PUBLIC
                           ${CMAKE_CURRENT_SOURCE_DIR}
                           ${Boost_INCLUDE_DIRS}
                         )

target_link_directories(my_boost_config
                        PUBLIC
                        ${Boost_LIBRARY_DIRS}
                      )


target_link_libraries(my_boost_config
                      PUBLIC
                      ${Boost_LIBRARIES}
                    )


