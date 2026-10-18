project(lib-graphics-named-bodies)

# Add library
add_library(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/NamedBodies.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_link_libraries(${PROJECT_NAME} PUBLIC lib-body sfml-graphics)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-graphics-text)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-constants)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-logger)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-stopwatch)
