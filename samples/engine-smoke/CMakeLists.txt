# Engine smoke test: window, input, a triangle, a test tone, a console command.
add_executable(engine-smoke main.c)
set_target_properties(engine-smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(engine-smoke PRIVATE mexnya::engine)
