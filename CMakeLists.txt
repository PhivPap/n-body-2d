# Top-level CMakeLists.txt

cmake_minimum_required(VERSION 3.14)
project(n-body-2d)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Warn debug builds may fail
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    message(WARNING "Debug builds break from csv lib")
endif()

# Optimization vs debug symbols option
option(OPT "Enable max optimization" OFF)
if (OPT) 
    set(CMAKE_BUILD_TYPE Release)
    set(CMAKE_CXX_FLAGS "-O3")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output)
    if(ipo_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
        message(WARNING "LTO is not supported: ${ipo_output}")
    endif()
else ()
    set(CMAKE_CXX_FLAGS "-g")
endif()

# Project folders
set(SRC_DIR "${CMAKE_SOURCE_DIR}/src")
set(LIB_DIR "${SRC_DIR}/lib")
set(THIRD_PARTY_DIR "${CMAKE_SOURCE_DIR}/third_party")

# Disable unused SFML modules 
set(SFML_BUILD_AUDIO OFF CACHE BOOL "Disable SFML audio module" FORCE)
set(SFML_BUILD_NETWORK OFF CACHE BOOL "Disable SFML network module" FORCE)
set(SFML_BUILD_EXAMPLES OFF CACHE BOOL "Disable SFML examples" FORCE)

# Add lib directories
add_subdirectory(${LIB_DIR}/Logger)
add_subdirectory(${LIB_DIR}/Config)
add_subdirectory(${LIB_DIR}/InputOutput)
add_subdirectory(${LIB_DIR}/Quadtree)
add_subdirectory(${LIB_DIR}/Body)
add_subdirectory(${LIB_DIR}/CLArgs)
add_subdirectory(${LIB_DIR}/StopWatch)
add_subdirectory(${LIB_DIR}/ViewPort)
add_subdirectory(${LIB_DIR}/Constants)

# Add third-party directories
add_subdirectory(${THIRD_PARTY_DIR}/csv-parser)
add_subdirectory(${THIRD_PARTY_DIR}/fmt)
add_subdirectory(${THIRD_PARTY_DIR}/json)
add_subdirectory(${THIRD_PARTY_DIR}/SFML)
add_subdirectory(${THIRD_PARTY_DIR}/argparse)

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

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/CLArgs/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-clargs)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/StopWatch/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-stopwatch)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/ViewPort/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-viewport)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Constants/include)
target_link_libraries(${PROJECT_NAME} PRIVATE constants)

target_include_directories(${PROJECT_NAME} PRIVATE ${THIRD_PARTY_DIR}/SMFL/include)
target_link_libraries(${PROJECT_NAME} PRIVATE sfml-graphics sfml-window sfml-system)
