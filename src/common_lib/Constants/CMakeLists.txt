project(lib-constants)

# Add library
add_library(${PROJECT_NAME} INTERFACE)
target_include_directories(${PROJECT_NAME} INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_link_libraries(${PROJECT_NAME} INTERFACE sfml-graphics sfml-system)
