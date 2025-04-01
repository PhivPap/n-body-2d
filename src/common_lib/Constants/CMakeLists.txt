project(constants)

add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} INTERFACE ${THIRD_PARTY_DIR}/SFML/include)
target_link_libraries(${PROJECT_NAME} INTERFACE sfml-graphics)
