project(lib-simulation-quadtree)

# Add library
add_library(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/Quadtree.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Link other libs
target_link_libraries(${PROJECT_NAME} PUBLIC lib-body)
target_link_libraries(${PROJECT_NAME} PUBLIC sfml-graphics)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-logger)
