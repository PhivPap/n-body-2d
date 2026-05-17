project(lib-body)

# Add library
add_library(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/Body.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_link_libraries(${PROJECT_NAME} PUBLIC sfml-system)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-logger)
