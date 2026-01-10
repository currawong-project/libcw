add_executable(mt_queue)

set_target_properties(mt_queue PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

target_sources(mt_queue PRIVATE main.cpp)


target_link_libraries(mt_queue PRIVATE cw)

install( TARGETS mt_queue DESTINATION bin )


