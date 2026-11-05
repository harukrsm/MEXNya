# MEXNya: engine library, editor and samples.
#     cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#     cmake --build build -j
cmake_minimum_required(VERSION 3.16)
project(MEXNya VERSION 0.1.0 LANGUAGES C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

option(MEXNYA_BUILD_EDITOR "Build the MEXNya Editor" ON)
option(MEXNYA_BUILD_SAMPLES "Build the samples" ON)

add_subdirectory(engine)
if(MEXNYA_BUILD_EDITOR)
    add_subdirectory(editor)
endif()
if(MEXNYA_BUILD_SAMPLES)
    add_subdirectory(samples/engine-smoke)
    add_subdirectory(samples/hello)
    add_subdirectory(tests)
endif()
