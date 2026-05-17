# Top-level CMakeLists.txt

cmake_minimum_required(VERSION 3.23)
project(n-body-2d)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# g++/clang++ optimization flags
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
if(ipo_supported)
    set(CMAKE_CXX_FLAGS "-O3 -flto -march=native -ffast-math")
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    set(CMAKE_CXX_FLAGS "-O3 -march=native -ffast-math")
    message(WARNING "LTO is not supported: ${ipo_output}")
endif()

# Warn debug builds may fail
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(WARNING "Debug builds may break csv lib")
endif()

# Project folders
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")
set(LIB_DIR "${SRC_DIR}/common_lib")
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")
set(ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")

# Disable unused SFML modules 
set(SFML_BUILD_AUDIO OFF CACHE BOOL "Disable SFML audio module" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "Disable SFML network module" FORCE)
set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "Disable SFML examples" FORCE)

# Add lib directories
add_subdirectory(${LIB_DIR}/Logger)
add_subdirectory(${LIB_DIR}/Config)
add_subdirectory(${LIB_DIR}/InputOutput)
add_subdirectory(${LIB_DIR}/Body)
add_subdirectory(${LIB_DIR}/CLArgs)
add_subdirectory(${LIB_DIR}/StopWatch)
add_subdirectory(${LIB_DIR}/Constants)
add_subdirectory(${LIB_DIR}/BufferedMeanCalculator)
add_subdirectory(${LIB_DIR}/AssetManager)
add_subdirectory(${LIB_DIR}/RLCaller)

add_subdirectory(${SRC_DIR}/Simulation)
add_subdirectory(${SRC_DIR}/Graphics)
add_subdirectory(${SRC_DIR}/Controller)

# Add third-party directories
add_subdirectory(${THIRD_PARTY_DIR}/csv-parser)
add_subdirectory(${THIRD_PARTY_DIR}/fmt)
add_subdirectory(${THIRD_PARTY_DIR}/json)
add_subdirectory(${THIRD_PARTY_DIR}/SFML)
add_subdirectory(${THIRD_PARTY_DIR}/argparse)

# Add executable
add_executable(${PROJECT_NAME} ${SRC_DIR}/main.cpp)

# Link libraries & Set include paths
target_link_libraries(${PROJECT_NAME} PRIVATE lib-simulation)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-graphics)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-controller)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-logger)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-input-output)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-config)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-body)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-clargs)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-stopwatch)

# Copy assets to build directory after build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${ASSETS_DIR}
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets
)
