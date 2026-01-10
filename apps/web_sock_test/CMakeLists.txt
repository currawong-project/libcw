add_executable(web_sock_test)

set_target_properties(web_sock_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

target_sources(web_sock_test PRIVATE main.cpp)

target_link_libraries(web_sock_test PRIVATE cw)

install( TARGETS web_sock_test DESTINATION bin )


