# Top-level CMakeLists.txt

cmake_minimum_required(VERSION 3.14)
project(n-body-2d)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Project folders
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")
set(INCLUDE_DIR "${CMAKE_SOURCE_DIR}/include")
set(LIB_DIR "${CMAKE_SOURCE_DIR}/lib")
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

# Disable unused SFML modules 
set(SFML_BUILD_AUDIO OFF CACHE BOOL "Disable SFML audio module" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "Disable SFML network module" FORCE)
set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "Disable SFML examples" FORCE)

# Set shared inlude dir
include_directories(${INCLUDE_DIR})

# Add lib directories
add_subdirectory(${LIB_DIR}/Logger)
add_subdirectory(${LIB_DIR}/Config)
add_subdirectory(${LIB_DIR}/InputOutput)
add_subdirectory(${LIB_DIR}/Quadtree)
add_subdirectory(${LIB_DIR}/Body)

# Add third-party directories
add_subdirectory(${THIRD_PARTY_DIR}/csv-parser)
add_subdirectory(${THIRD_PARTY_DIR}/fmt)
add_subdirectory(${THIRD_PARTY_DIR}/json)
add_subdirectory(${THIRD_PARTY_DIR}/SFML)

# Add executable
add_executable(${PROJECT_NAME} ${SRC_DIR}/main.cpp)

# Link libraries & Set include paths
target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Logger/include)
target_link_libraries(${PROJECT_NAME} PRIVATE logger)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/InputOutput/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-input-output)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Config/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-config)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Quadtree/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-quadtree)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Body/include)
target_link_libraries(${PROJECT_NAME} PRIVATE body)

target_include_directories(${PROJECT_NAME} PRIVATE ${THIRD_PARTY_DIR}/SMFL/include)
target_link_libraries(${PROJECT_NAME} PRIVATE sfml-graphics sfml-window sfml-system)