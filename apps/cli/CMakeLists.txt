add_executable(cli)

set_target_properties(cli PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

target_sources(cli PRIVATE main.cpp)

target_compile_options(cli PRIVATE
    # Add specific flags only for the Debug configuration (e.g., Address Sanitizer)
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:-fsanitize=undefined -Wall -Wextra -Wno-unused>    
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>,$<BOOL:${CW_ADDRESS_SANITIZER_FL}>>:-fsanitize=address>    
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>,$<BOOL:${CW_THREAD_SANITIZER_FL}>>:-fsanitize=thread>    
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${CW_COVERAGE_FL}>>:--coverage>    
)

if(UNIX)
  target_compile_definitions(cli PRIVATE OS_LINUX)
endif()

target_link_libraries(cli PRIVATE cw)

install( TARGETS cli DESTINATION bin )


