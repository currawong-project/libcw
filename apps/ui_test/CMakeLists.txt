add_executable(ui_test)

target_sources(ui_test PRIVATE main.cpp)

target_link_libraries(ui_test PRIVATE cw)

install( TARGETS ui_test DESTINATION bin )


