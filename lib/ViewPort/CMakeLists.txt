project(lib-viewport)

# Add library
add_library(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/ViewPort.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Logger/include)
target_link_libraries(${PROJECT_NAME} PRIVATE logger)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Constants/include)
target_link_libraries(${PROJECT_NAME} PRIVATE constants)

target_include_directories(${PROJECT_NAME} PRIVATE ${THIRD_PARTY_DIR}/SFML/include)
target_link_libraries(${PROJECT_NAME} PRIVATE sfml-system sfml-graphics)
