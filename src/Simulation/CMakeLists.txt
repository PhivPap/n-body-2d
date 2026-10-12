project(lib-simulation)


set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

set(LOCAL_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib")

# Add local lib directories
add_subdirectory(${LOCAL_LIB_DIR}/Quadtree)

# Add library
add_library(${PROJECT_NAME} 
        ${CMAKE_CURRENT_LIST_DIR}/src/Simulation.cpp 
        ${CMAKE_CURRENT_LIST_DIR}/src/AllPairs.cpp 
        ${CMAKE_CURRENT_LIST_DIR}/src/BarnesHut.cpp)
target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)

# Link libraries & Set include paths
target_link_libraries(${PROJECT_NAME} PUBLIC lib-config)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-body)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-rlcaller)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-simulation-quadtree)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-stopwatch)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-constants)
target_link_libraries(${PROJECT_NAME} PUBLIC lib-buffered-mean-calculator)
target_link_libraries(${PROJECT_NAME} PRIVATE lib-logger)

target_compile_options(${PROJECT_NAME} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-ffast-math>)

# GPU Barnes-Hut
if(NBODY_CUDA)
    find_package(CUDAToolkit REQUIRED)

    target_sources(${PROJECT_NAME} PRIVATE
            ${CMAKE_CURRENT_LIST_DIR}/src/BarnesHutCuda.cpp
            ${CMAKE_CURRENT_LIST_DIR}/src/BarnesHutCuda.cu)
    target_compile_options(${PROJECT_NAME} PRIVATE
        $<$<COMPILE_LANGUAGE:CUDA>:--use_fast_math>
        $<$<COMPILE_LANGUAGE:CUDA>:--extra-device-vectorization>
        $<$<COMPILE_LANGUAGE:CUDA>:-Xcompiler=-march=native,-ffast-math>
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE CUDA::cudart)
endif()
