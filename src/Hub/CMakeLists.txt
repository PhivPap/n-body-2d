project(lib-hub)

# Add library
add_library(${PROJECT_NAME}
    ${CMAKE_CURRENT_LIST_DIR}/src/Catalogue.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/Subprocess.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-logger)
