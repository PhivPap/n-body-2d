project(lib-graphics-panel)

# Add library
add_library(${PROJECT_NAME} 
    ${CMAKE_CURRENT_LIST_DIR}/src/PanelManager.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/StatsPanel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/ConfigPanel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/CommandsPanel.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_link_libraries(${PROJECT_NAME} PUBLIC lib-assetmanager)
target_link_libraries(${PROJECT_NAME} PUBLIC sfml-system sfml-graphics)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-logger)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-constants)
