project(logger)

add_library(${PROJECT_NAME} INTERFACE)

target_include_directories(${PROJECT_NAME} INTERFACE ${THIRD_PARTY_DIR}/fmt/include)
target_link_libraries(${PROJECT_NAME} INTERFACE fmt::fmt)
