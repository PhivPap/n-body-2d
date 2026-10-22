project(lib-ui)

# Add library
add_library(${PROJECT_NAME}
    ${CMAKE_CURRENT_LIST_DIR}/src/Ui.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/Format.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/Layout.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/Widgets.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_link_libraries(${PROJECT_NAME} PUBLIC imgui sfml-graphics)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-assetmanager)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-constants)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-logger)
