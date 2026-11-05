# MEXNya engine as a static library: target mexnya_engine (alias mexnya::engine).
#
# Use it from a game:
#     add_subdirectory(path/to/git-MEXNya/engine mexnya)
#     target_link_libraries(my_game PRIVATE mexnya::engine)
#     mexnya_add_resources(my_game)          # font and UI text next to the executable
#
# SDL3 comes from (in order): MEXNYA_SDL3_DIR (a source tree), an installed SDL3
# (find_package), or a download of the pinned release by FetchContent.
cmake_minimum_required(VERSION 3.16)
if(TARGET mexnya_engine)
    return()
endif()

set(MEXNYA_ROOT ${CMAKE_CURRENT_LIST_DIR}/.. CACHE INTERNAL "")
option(MEXNYA_OPENGL "Build the OpenGL 3.3 renderer" ON)
set(MEXNYA_SDL3_DIR "" CACHE PATH "SDL3 source tree to use instead of an installed or downloaded SDL3")
set(MEXNYA_SDL3_TAG "release-3.2.28" CACHE STRING "SDL3 release FetchContent downloads")

# ---------------------------------------------------------------- SDL3
if(NOT TARGET SDL3::SDL3-static AND NOT TARGET SDL3::SDL3)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    if(MEXNYA_SDL3_DIR)
        add_subdirectory(${MEXNYA_SDL3_DIR} ${CMAKE_BINARY_DIR}/SDL3 EXCLUDE_FROM_ALL)
    elseif(EXISTS ${MEXNYA_ROOT}/third_party/SDL3/CMakeLists.txt)
        add_subdirectory(${MEXNYA_ROOT}/third_party/SDL3 ${CMAKE_BINARY_DIR}/SDL3 EXCLUDE_FROM_ALL)
    else()
        find_package(SDL3 CONFIG QUIET)
        if(NOT SDL3_FOUND)
            include(FetchContent)
            message(STATUS "MEXNya: downloading SDL3 ${MEXNYA_SDL3_TAG}")
            FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
                                 GIT_TAG ${MEXNYA_SDL3_TAG} GIT_SHALLOW TRUE)
            FetchContent_GetProperties(SDL3)
            if(NOT sdl3_POPULATED)
                FetchContent_Populate(SDL3)
                add_subdirectory(${sdl3_SOURCE_DIR} ${sdl3_BINARY_DIR} EXCLUDE_FROM_ALL)
            endif()
        endif()
    endif()
endif()
if(TARGET SDL3::SDL3-static)
    set(MEXNYA_SDL SDL3::SDL3-static)
else()
    set(MEXNYA_SDL SDL3::SDL3)
endif()

# ---------------------------------------------------------------- Lua 5.4 (vendored, MIT)
if(NOT TARGET mexnya_lua)
    file(GLOB MEXNYA_LUA_SOURCES ${MEXNYA_ROOT}/third_party/lua/src/*.c)
    list(FILTER MEXNYA_LUA_SOURCES EXCLUDE REGEX "/(lua|luac)\\.c$")
    add_library(mexnya_lua STATIC ${MEXNYA_LUA_SOURCES})
    target_include_directories(mexnya_lua PUBLIC ${MEXNYA_ROOT}/third_party/lua/src)
    if(WIN32)
        # luaconf.h selects LUA_USE_WINDOWS itself.
    elseif(APPLE)
        target_compile_definitions(mexnya_lua PRIVATE LUA_USE_MACOSX)
    else()
        target_compile_definitions(mexnya_lua PRIVATE LUA_USE_LINUX)
    endif()
    set_target_properties(mexnya_lua PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
endif()

# ---------------------------------------------------------------- engine
file(GLOB_RECURSE MEXNYA_ENGINE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.c)
add_library(mexnya_engine STATIC ${MEXNYA_ENGINE_SOURCES})
add_library(mexnya::engine ALIAS mexnya_engine)
set_target_properties(mexnya_engine PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
target_compile_definitions(mexnya_engine PUBLIC MC_DESKTOP=1 MC_RENDER_SCALE=1)
if(NOT MEXNYA_OPENGL)
    target_compile_definitions(mexnya_engine PUBLIC MC_NO_OPENGL=1)
endif()
if(APPLE)
    target_compile_definitions(mexnya_engine PRIVATE GL_SILENCE_DEPRECATION)
endif()
# Engine headers are included as "engine/...", from the repository root.
target_include_directories(mexnya_engine PUBLIC ${MEXNYA_ROOT}
    PRIVATE ${CMAKE_CURRENT_LIST_DIR}/render ${CMAKE_CURRENT_LIST_DIR}/platform
            ${CMAKE_CURRENT_LIST_DIR}/console ${CMAKE_CURRENT_LIST_DIR}/audio)
target_link_libraries(mexnya_engine PUBLIC ${MEXNYA_SDL} mexnya_lua)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(mexnya_engine PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()
if(APPLE)
    target_link_libraries(mexnya_engine PUBLIC "-framework Foundation" "-framework CoreFoundation"
                          "-framework CoreText" "-framework CoreGraphics")
elseif(WIN32)
    target_link_libraries(mexnya_engine PUBLIC gdi32)
else()
    target_link_libraries(mexnya_engine PUBLIC m dl pthread)
endif()
if(MEXNYA_OPENGL)
    if(WIN32)
        target_link_libraries(mexnya_engine PUBLIC opengl32)
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(mexnya_engine PUBLIC OpenGL::GL)
    endif()
endif()

# Copies the engine font and UI text next to a target's executable (engine-resources/).
function(mexnya_add_resources target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${MEXNYA_ROOT}/engine/resources
                $<TARGET_FILE_DIR:${target}>/engine-resources
        COMMENT "MEXNya: engine resources for ${target}")
endfunction()
