# MEXNya Editor, written on the engine it edits (native resolution, OpenGL).
file(GLOB MX_EDITOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.c)
add_executable(MexnyaEditor ${MX_EDITOR_SOURCES})
set_target_properties(MexnyaEditor PROPERTIES C_STANDARD 99 RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(MexnyaEditor PRIVATE mexnya::engine)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(MexnyaEditor PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
endif()
mexnya_add_resources(MexnyaEditor)

# Sample native plugin: proves the plugin ABI. Not linked to the engine.
add_library(mx_plugin_hello MODULE ${CMAKE_CURRENT_SOURCE_DIR}/samples/hello_plugin.c)
set_target_properties(mx_plugin_hello PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden
                      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins)
