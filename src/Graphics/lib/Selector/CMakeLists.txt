project(lib-selector)

# Add library
add_library(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/Selector.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

target_include_directories(${PROJECT_NAME} PUBLIC ${LIB_DIR}/Body/include)
target_link_libraries(${PROJECT_NAME} PUBLIC body)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Constants/include)
target_link_libraries(${PROJECT_NAME} PRIVATE constants)

target_include_directories(${PROJECT_NAME} PUBLIC ${THIRD_PARTY_DIR}/SMFL/include)
target_link_libraries(${PROJECT_NAME} PUBLIC sfml-system)