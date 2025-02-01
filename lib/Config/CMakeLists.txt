project(lib-config)

# Project folder
set(SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/src")

# Find source files
file(GLOB_RECURSE SRC_FILES
  "${SRC_DIR}/*.cpp"
)

# Add library
add_library(${PROJECT_NAME} ${SRC_FILES})

# Link other libs
target_link_libraries(${PROJECT_NAME} PRIVATE logger nlohmann_json::nlohmann_json)
