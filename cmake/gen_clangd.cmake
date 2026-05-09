function( gen_clangd )

  # Generate .clangd at the project root with machine-correct absolute paths
  set(CLANGD_CONFIG_PATH "${CMAKE_SOURCE_DIR}/.clangd")

  file(WRITE "${CLANGD_CONFIG_PATH}"
"CompileFlags:
  CompilationDatabase: ${CMAKE_BINARY_DIR}
  Add:
    - -I${CMAKE_SOURCE_DIR}/src/libcw/src/core
    - -I${CMAKE_SOURCE_DIR}/src/libcw/src/cw
    - -I${CMAKE_SOURCE_DIR}/src/libcw/src/flow
    - -I${CMAKE_SOURCE_DIR}/src/libcw/src/io
    - -I${CMAKE_SOURCE_DIR}/src/libcw/src/io_flow
    - -I${CMAKE_SOURCE_DIR}/src/libcw/src/io_components
    - -Wno-unused-parameter
    - -Wno-vla-cxx-extension
Diagnostics:
  Suppress:
    - unused-includes
Index:
  Background: Build
InlayHints:
  Enabled: Yes
")
  
endfunction()  
