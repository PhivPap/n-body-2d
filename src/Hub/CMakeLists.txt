project(lib-hub)

# Add library
add_library(${PROJECT_NAME}
    ${CMAKE_CURRENT_LIST_DIR}/src/Catalogue.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/Subprocess.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ScenarioHub.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/IntroCard.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
# The repository, where the hub finds the scenarios/ folder and the generator by default
target_compile_definitions(${PROJECT_NAME} PRIVATE NBODY_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Link libraries & Set include paths
target_link_libraries(${PROJECT_NAME} PUBLIC sfml-system)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-ui)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-config)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-constants)
target_link_libraries(${PROJECT_NAME} PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-logger)
