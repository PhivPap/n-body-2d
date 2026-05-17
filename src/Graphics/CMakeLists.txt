project(lib-graphics)

set(LOCAL_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")

# Add local lib directories
add_subdirectory(${LOCAL_LIB_DIR}/ViewPort)
add_subdirectory(${LOCAL_LIB_DIR}/Panel)
add_subdirectory(${LOCAL_LIB_DIR}/Selector)

# Add library
add_library(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/Graphics.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_link_libraries(${PROJECT_NAME} PUBLIC lib-config)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-body)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-stopwatch)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-constants)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-rlcaller)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-graphics-viewport)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-buffered-mean-calculator)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-graphics-panel)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-graphics-selector)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-logger)
