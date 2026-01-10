add_executable(preset_sel)

set_target_properties(preset_sel PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

target_sources(preset_sel
  
  PRIVATE
  main.cpp
  cwIoPresetSelApp.cpp
  cwVelTableTuner.cpp

  cwIoPresetSelApp.h
  cwVelTableTuner.h
  
)



target_link_libraries(preset_sel PRIVATE cw)

install( TARGETS preset_sel DESTINATION bin )


