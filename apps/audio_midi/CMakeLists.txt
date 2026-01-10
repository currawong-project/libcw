add_executable(audio_midi)

set_target_properties(audio_midi PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

target_sources(audio_midi PRIVATE main.cpp)

target_link_libraries(audio_midi PRIVATE cw)

install( TARGETS audio_midi DESTINATION bin )


