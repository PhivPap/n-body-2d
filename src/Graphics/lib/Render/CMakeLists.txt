project(lib-graphics-render)

# Add library
add_library(${PROJECT_NAME}
    ${CMAKE_CURRENT_LIST_DIR}/src/Colors.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ToneMath.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/RenderGroups.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_link_libraries(${PROJECT_NAME} PUBLIC lib-body lib-config sfml-graphics)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-graphics-named-bodies)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-constants)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-logger)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-stopwatch)
