project(lib-fpscounter)

# Add library
add_library(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/FPSCounter.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_include_directories(${PROJECT_NAME} PUBLIC ${LIB_DIR}/StopWatch/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-stopwatch)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Constants/include)
target_link_libraries(${PROJECT_NAME} PRIVATE constants)
