add_executable(io_test)

target_sources(io_test PRIVATE main.cpp)

target_link_libraries(io_test PRIVATE cw)

install( TARGETS io_test DESTINATION bin )


