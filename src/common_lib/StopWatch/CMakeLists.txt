project(lib-stopwatch)

# Add library
add_library(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/StopWatch.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Link dependencies & Set include paths
target_link_libraries(${PROJECT_NAME} PRIVATE lib-logger)
target_link_libraries(${PROJECT_NAME} PUBLIC fmt::fmt)
