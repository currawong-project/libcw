add_executable(cli)

set_target_properties(cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

target_sources(cli PRIVATE main.cpp)

if(UNIX)
  target_compile_definitions(cli PRIVATE OS_LINUX)
endif()

target_link_libraries(cli PRIVATE cw)

install( TARGETS cli DESTINATION bin )


