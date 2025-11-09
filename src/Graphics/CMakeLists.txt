project(graphics)

set(LOCAL_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")

# Add local lib directories
add_subdirectory(${LOCAL_LIB_DIR}/ViewPort)
add_subdirectory(${LOCAL_LIB_DIR}/Panel)

# Add library
add_library(${PROJECT_NAME} ${CMAKE_CURRENT_LIST_DIR}/src/Graphics.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
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

target_include_directories(${PROJECT_NAME} PUBLIC ${LIB_DIR}/RLCaller/include)
# target_link_libraries(${PROJECT_NAME} PRIVATE rlcaller)

target_include_directories(${PROJECT_NAME} PUBLIC ${LOCAL_LIB_DIR}/ViewPort/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-viewport)

target_include_directories(${PROJECT_NAME} PUBLIC ${LIB_DIR}/BufferedMeanCalculator/include)

target_include_directories(${PROJECT_NAME} PUBLIC ${LOCAL_LIB_DIR}/Panel/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-panel)

target_include_directories(${PROJECT_NAME} PRIVATE ${THIRD_PARTY_DIR}/SMFL/include)
target_link_libraries(${PROJECT_NAME} PRIVATE sfml-graphics sfml-window sfml-system)
