project(simulation LANGUAGES CXX CUDA)


set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_ARCHITECTURES "native")

set(LOCAL_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")

# Add local lib directories
add_subdirectory(${LOCAL_LIB_DIR}/Quadtree)

# Add library
add_library(${PROJECT_NAME} 
        ${CMAKE_CURRENT_LIST_DIR}/src/Simulation.cpp 
        ${CMAKE_CURRENT_LIST_DIR}/src/AllPairs.cpp 
        ${CMAKE_CURRENT_LIST_DIR}/src/BarnesHut.cpp 
        ${CMAKE_CURRENT_LIST_DIR}/src/BarnesHutCuda.cpp
        ${CMAKE_CURRENT_LIST_DIR}/src/BarnesHutCuda.cu)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Logger/include)
target_link_libraries(${PROJECT_NAME} PRIVATE logger)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Config/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-config)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Body/include)
target_link_libraries(${PROJECT_NAME} PRIVATE body)

target_include_directories(${PROJECT_NAME} PUBLIC ${LIB_DIR}/RLCaller/include)

target_include_directories(${PROJECT_NAME} PUBLIC ${LOCAL_LIB_DIR}/Quadtree/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-quadtree)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/StopWatch/include)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-stopwatch)

target_include_directories(${PROJECT_NAME} PRIVATE ${LIB_DIR}/Constants/include)
target_link_libraries(${PROJECT_NAME} PRIVATE constants)

target_include_directories(${PROJECT_NAME} PUBLIC ${LIB_DIR}/BufferedMeanCalculator/include)

target_include_directories(${PROJECT_NAME} PRIVATE ${THIRD_PARTY_DIR}/SMFL/include)
target_link_libraries(${PROJECT_NAME} PRIVATE sfml-graphics sfml-window sfml-system)

# CUB / libcudacxx from vendored CCCL (header-only)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${THIRD_PARTY_DIR}/CCCL/cub
    ${THIRD_PARTY_DIR}/CCCL/libcudacxx/include
    ${THIRD_PARTY_DIR}/CCCL/thrust
)

find_package(CUDAToolkit REQUIRED)

enable_language(CUDA)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
)
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<COMPILE_LANGUAGE:CUDA>:-rdc=true>
    $<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math>
    $<$<COMPILE_LANGUAGE:CUDA>:--extra-device-vectorization>
    $<$<COMPILE_LANGUAGE:CUDA>:--dlink-time-opt>
    $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-O3,-march=native,-ffast-math>
)
set_target_properties(simulation PROPERTIES
    CUDA_RESOLVE_DEVICE_SYMBOLS ON
)

add_library(cudadevrt STATIC IMPORTED)
set_target_properties(cudadevrt PROPERTIES
    IMPORTED_LOCATION
        ${CUDAToolkit_LIBRARY_ROOT}/targets/x86_64-linux/lib/libcudadevrt.a
)
target_link_libraries(${PROJECT_NAME} PRIVATE cudadevrt CUDA::cudart)
