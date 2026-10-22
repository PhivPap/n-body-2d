# Top-level CMakeLists.txt

cmake_minimum_required(VERSION 3.24)
project(n-body-2d)

# Set the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Optimized build (-O3) unless another build type is asked for
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# CUDA, needed by the GPU Barnes-Hut only. The CUDA code is compiled for the GPU found at
# configure time, unless NBODY_CUDA_ARCHITECTURES names others (e.g. 89 or all-major).
# CMAKE_CUDA_ARCHITECTURES is overridden: older build directories cached CMake's default.
include(CheckLanguage)
check_language(CUDA)
if(CMAKE_CUDA_COMPILER)
    option(NBODY_CUDA "Build the GPU Barnes-Hut (needs the CUDA toolkit)" ON)
else()
    option(NBODY_CUDA "Build the GPU Barnes-Hut (needs the CUDA toolkit)" OFF)
endif()
set(NBODY_CUDA_ARCHITECTURES native CACHE STRING "CUDA architectures of the GPU Barnes-Hut")
if(NBODY_CUDA)
    set(CMAKE_CUDA_ARCHITECTURES ${NBODY_CUDA_ARCHITECTURES})
    set(CMAKE_CUDA_RUNTIME_LIBRARY Shared)
    enable_language(CUDA)
endif()

# Link time optimization, except in Debug builds
include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_output LANGUAGES CXX)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG FALSE)
else()
    message(WARNING "LTO is not supported: ${ipo_output}")
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

# Code for this CPU, for all targets: with LTO, GCC only inlines across functions built for the
# same target
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-march=native>")

# Add third-party directories
add_subdirectory(${THIRD_PARTY_DIR}/csv-parser)
# csv-parser builds itself with --coverage in Debug builds, so its users must link the runtime
target_link_options(csv INTERFACE $<$<CONFIG:Debug>:--coverage>)
add_subdirectory(${THIRD_PARTY_DIR}/fmt)
add_subdirectory(${THIRD_PARTY_DIR}/json)
add_subdirectory(${THIRD_PARTY_DIR}/SFML)
add_subdirectory(${THIRD_PARTY_DIR}/argparse)

# Dear ImGui and its SFML backend, which have no CMake build to add as a subdirectory here
add_library(imgui
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY_DIR}/imgui-sfml/imgui-SFML.cpp)
target_include_directories(imgui SYSTEM PUBLIC ${THIRD_PARTY_DIR}/imgui
        ${THIRD_PARTY_DIR}/imgui-sfml)
# ImVec2 and ImVec4 convert to and from sf::Vector2 and sf::Color
target_compile_definitions(imgui PUBLIC IMGUI_USER_CONFIG="imconfig-SFML.h")
target_link_libraries(imgui PUBLIC sfml-graphics)

# Warnings for this project's code. Only the simulation libraries use -ffast-math, which (on x86)
# also stops LTO from inlining across them and the rest, so keep hot calls inline in headers.
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra>")
if(NBODY_CUDA)
    add_compile_definitions(NBODY_CUDA)
endif()

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
add_subdirectory(${SRC_DIR}/Ui)
add_subdirectory(${SRC_DIR}/Hub)
add_subdirectory(${SRC_DIR}/Graphics)
add_subdirectory(${SRC_DIR}/Controller)

# Add executable
add_executable(${PROJECT_NAME} ${SRC_DIR}/main.cpp)

# Flush denormals to zero, like the whole program did when it was all built with -ffast-math
target_link_options(${PROJECT_NAME} PRIVATE $<$<CXX_COMPILER_ID:GNU>:-mdaz-ftz>)

# Link libraries & Set include paths
target_link_libraries(${PROJECT_NAME} PRIVATE lib-simulation)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-graphics)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-ui)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-graphics-text)
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

# Tests, run with `ctest --test-dir build`
enable_testing()
add_subdirectory(tests)
