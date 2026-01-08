add_executable(mt_queue)

target_sources(mt_queue PRIVATE main.cpp)


target_link_libraries(mt_queue PRIVATE cw)

install( TARGETS mt_queue DESTINATION bin )


