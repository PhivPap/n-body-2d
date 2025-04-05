project(controller)

# Add library
add_library(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/Controller.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths    
target_include_directories(${PROJECT_NAME} PRIVATE ${SRC_DIR}/Simulation/include)
target_link_libraries(${PROJECT_NAME} PRIVATE simulation)

target_include_directories(${PROJECT_NAME} PRIVATE ${SRC_DIR}/Graphics/include)
target_link_libraries(${PROJECT_NAME} PRIVATE graphics)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Exit/include)
target_link_libraries(${PROJECT_NAME} PRIVATE exit)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Logger/include)
target_link_libraries(${PROJECT_NAME} PRIVATE logger)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Config/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-config)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Body/include)
target_link_libraries(${PROJECT_NAME} PRIVATE body)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/StopWatch/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-stopwatch)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Constants/include)
target_link_libraries(${PROJECT_NAME} PRIVATE constants)

target_include_directories(${PROJECT_NAME} PRIVATE ${THIRD_PARTY_DIR}/SMFL/include)
target_link_libraries(${PROJECT_NAME} PRIVATE sfml-graphics sfml-window sfml-system)
