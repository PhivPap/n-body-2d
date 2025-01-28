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

# Filter directories named exactly "include"
file(GLOB_RECURSE ALL_DIRS RELATIVE ${CMAKE_SOURCE_DIR} LIST_DIRECTORIES true "*")
set(ALL_INCLUDE_DIRS "")
foreach(DIR ${ALL_DIRS})
    if(DIR MATCHES ".*/include$|^include$")
        list(APPEND ALL_INCLUDE_DIRS ${DIR})
    endif()
endforeach()
message(STATUS "Include directories: ${ALL_INCLUDE_DIRS}")

# Set project include directories
include_directories(${ALL_INCLUDE_DIRS})

# Add lib directories
add_subdirectory(${LIB_DIR}/InputOutput)

# Add third-party directories
add_subdirectory(${THIRD_PARTY_DIR}/csv-parser)
add_subdirectory(${THIRD_PARTY_DIR}/fmt)


# Find source files
file(GLOB_RECURSE SRC_FILES
    "${SRC_DIR}/*.cpp"
)

# Add executable
add_executable(${PROJECT_NAME} ${SRC_FILES})

# Link libraries
target_link_libraries(${PROJECT_NAME} fmt::fmt lib-input-output)
