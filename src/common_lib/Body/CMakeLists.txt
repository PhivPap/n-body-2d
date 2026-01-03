project(body)

# Add library
add_library(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/Body.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_include_directories(${PROJECT_NAME} PUBLIC ${THIRD_PARTY_DIR}/SFML/include)
target_link_libraries(${PROJECT_NAME} PUBLIC sfml-graphics)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Logger/include)
target_link_libraries(${PROJECT_NAME} PRIVATE logger)
