add_executable(audio_midi)

target_sources(audio_midi PRIVATE main.cpp)

target_link_libraries(audio_midi PRIVATE cw)

install( TARGETS audio_midi DESTINATION bin )


