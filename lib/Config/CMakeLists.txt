project(lib-config)

# Add library
add_library(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/Config.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Logger/include)
target_link_libraries(${PROJECT_NAME} PRIVATE logger)

target_include_directories(${PROJECT_NAME} PRIVATE ${THIRD_PARTY}/json/include)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)

target_include_directories(${PROJECT_NAME} PRIVATE ${THIRD_PARTY_DIR}/SFML/include)
target_link_libraries(${PROJECT_NAME} PRIVATE sfml-system)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/StopWatch/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-stopwatch)
