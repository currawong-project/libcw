add_executable(ui_test)

set_target_properties(ui_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

target_sources(ui_test PRIVATE main.cpp)

target_link_libraries(ui_test PRIVATE cw)

install( TARGETS ui_test DESTINATION bin )


