add_executable(io_test)

set_target_properties(io_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

target_sources(io_test PRIVATE main.cpp)

target_link_libraries(io_test PRIVATE cw)

install( TARGETS io_test DESTINATION bin )


