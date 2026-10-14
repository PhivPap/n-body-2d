project(tests)

# Every test executable returns non-zero if one of its checks failed
add_executable(test-config ${CMAKE_CURRENT_LIST_DIR}/ConfigTest.cpp)
target_link_libraries(test-config PRIVATE lib-config lib-logger nlohmann_json::nlohmann_json)
add_test(NAME config COMMAND test-config)

add_executable(test-input-output ${CMAKE_CURRENT_LIST_DIR}/InputOutputTest.cpp)
target_link_libraries(test-input-output PRIVATE lib-input-output lib-logger)
add_test(NAME input-output COMMAND test-input-output)

# test-orbit [solar-system.csv]: orbital elements, attractors and orbit tracing, and with the CSV
# the elements the generator gave the committed Solar System
add_executable(test-orbit ${CMAKE_CURRENT_LIST_DIR}/OrbitTest.cpp)
target_link_libraries(test-orbit PRIVATE lib-graphics-orbit lib-graphics-named-bodies
        lib-input-output lib-constants lib-logger)
add_test(NAME orbital-elements
        COMMAND test-orbit ${CMAKE_SOURCE_DIR}/scenarios/solar-system.csv)

add_executable(test-named-bodies ${CMAKE_CURRENT_LIST_DIR}/NamedBodiesTest.cpp)
target_link_libraries(test-named-bodies PRIVATE lib-graphics-named-bodies
        lib-graphics-selector lib-body lib-constants lib-logger)
add_test(NAME named-bodies COMMAND test-named-bodies)

# test-simulation <test> <algorithm> runs one physics test on one algorithm, linked like the
# simulator (see the top-level CMakeLists.txt)
add_executable(test-simulation ${CMAKE_CURRENT_LIST_DIR}/SimulationTest.cpp)
target_link_libraries(test-simulation PRIVATE lib-simulation lib-logger)
target_link_options(test-simulation PRIVATE $<$<CXX_COMPILER_ID:GNU>:-mdaz-ftz>)
add_test(NAME orbit-all-pairs COMMAND test-simulation orbit "All Pairs")
add_test(NAME orbit-barnes-hut COMMAND test-simulation orbit Barnes-Hut)
add_test(NAME agreement-barnes-hut COMMAND test-simulation agreement Barnes-Hut)
add_test(NAME pause-all-pairs COMMAND test-simulation pause "All Pairs")
add_test(NAME pause-barnes-hut COMMAND test-simulation pause Barnes-Hut)
add_test(NAME degenerate-all-pairs COMMAND test-simulation degenerate "All Pairs")
add_test(NAME degenerate-barnes-hut COMMAND test-simulation degenerate Barnes-Hut)
if(NBODY_CUDA)
    add_test(NAME orbit-barnes-hut-gpu COMMAND test-simulation orbit "Barnes-Hut GPU")
    add_test(NAME agreement-barnes-hut-gpu COMMAND test-simulation agreement "Barnes-Hut GPU")
    add_test(NAME degenerate-barnes-hut-gpu COMMAND test-simulation degenerate "Barnes-Hut GPU")
endif()

# The scenario generator's --verify runs the simulator on a few scenarios and checks that their
# orbits hold and their energy is conserved. It needs Python 3 with numpy and scipy.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    execute_process(COMMAND ${Python3_EXECUTABLE} -c "import numpy, scipy"
            RESULT_VARIABLE python_modules_missing OUTPUT_QUIET ERROR_QUIET)
endif()
if(Python3_Interpreter_FOUND AND NOT python_modules_missing)
    set(generator ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/scenario-generator.py --verify
            --out-dir ${CMAKE_CURRENT_BINARY_DIR}/scenarios --simulator $<TARGET_FILE:n-body-2d>)
    set(scenarios three-body-figure-eight solar-system)
    # Unoptimized, the Solar System's 8 million steps take minutes
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(REMOVE_ITEM scenarios solar-system)
    endif()
    add_test(NAME scenarios COMMAND ${generator} --scale 0.1 ${scenarios})
    # Kepler-16 runs on the GPU at full scale (1.5k bodies), but on All Pairs at --scale 0.1
    if(NBODY_CUDA)
        add_test(NAME scenarios-gpu COMMAND ${generator} kepler-16)
    endif()
else()
    message(STATUS "Python 3 with numpy and scipy not found: the scenario tests are left out")
endif()
