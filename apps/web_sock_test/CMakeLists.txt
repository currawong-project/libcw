add_executable(web_sock_test)

target_sources(web_sock_test PRIVATE main.cpp)

target_link_libraries(web_sock_test PRIVATE cw)

install( TARGETS web_sock_test DESTINATION bin )


