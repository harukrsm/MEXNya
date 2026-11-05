# hello (a MEXNya game)
#     cmake -S . -B build && cmake --build build -j && ./build/hello
cmake_minimum_required(VERSION 3.16)
project(hello C)
set(CMAKE_C_STANDARD 99)

# Where the MEXNya engine repository is (set by `mexnya new`; override with -DMEXNYA_DIR=...).
set(MEXNYA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "MEXNya engine repository")
if(NOT TARGET mexnya_engine)
    add_subdirectory(${MEXNYA_DIR}/engine ${CMAKE_BINARY_DIR}/mexnya)
endif()

file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
add_executable(hello ${GAME_SOURCES})
set_target_properties(hello PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(hello PRIVATE mexnya::engine)
mexnya_add_resources(hello)
# The editor's project and the game's assets travel with the executable.
add_custom_command(TARGET hello POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/mexnya $<TARGET_FILE_DIR:hello>/mexnya
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:hello>/assets
    COMMENT "Copying the project and assets")
