# Top-level CMakeLists.txt

cmake_minimum_required(VERSION 3.14)
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
target_include_directories(${PROJECT_NAME} PRIVATE ${SRC_DIR}/Simulation/include)
target_link_libraries(${PROJECT_NAME} PRIVATE simulation)

target_include_directories(${PROJECT_NAME} PRIVATE ${SRC_DIR}/Graphics/include)
target_link_libraries(${PROJECT_NAME} PRIVATE graphics)

target_include_directories(${PROJECT_NAME} PRIVATE ${SRC_DIR}/Controller/include)
target_link_libraries(${PROJECT_NAME} PRIVATE controller)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Logger/include)
target_link_libraries(${PROJECT_NAME} PRIVATE logger)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/InputOutput/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-input-output)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Config/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-config)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Body/include)
target_link_libraries(${PROJECT_NAME} PRIVATE body)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/CLArgs/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-clargs)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/StopWatch/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-stopwatch)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Constants/include)
target_link_libraries(${PROJECT_NAME} PRIVATE constants)

target_include_directories(${PROJECT_NAME} PRIVATE ${THIRD_PARTY_DIR}/SMFL/include)
target_link_libraries(${PROJECT_NAME} PRIVATE sfml-graphics sfml-window sfml-system)
