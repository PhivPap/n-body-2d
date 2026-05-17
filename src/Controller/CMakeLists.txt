project(lib-controller)

# Add library
add_library(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/Controller.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths    
target_link_libraries(${PROJECT_NAME} PUBLIC lib-simulation)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-graphics)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-config)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-rlcaller)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-logger)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-body)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-stopwatch)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-constants)
target_link_libraries(${PROJECT_NAME} PRIVATE sfml-graphics sfml-window sfml-system)
