add_executable(runtime_test runtime_test.c)
set_target_properties(runtime_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_link_libraries(runtime_test PRIVATE mexnya::engine)
