enable_testing()




add_executable(test_main)

target_sources(test_main
  PRIVATE
  test_main.cpp
  test_flow.cpp
  test_filesys.cpp
  test_lex.cpp
  test_object.cpp
  test_text.cpp
  test_time.cpp
  test_vops.cpp
  test_file.cpp
  test_mem.cpp
  test_numeric_convert.cpp
  test_log.cpp
)

target_compile_options(test_main PRIVATE
    # Add specific flags only for the Debug configuration (e.g., Address Sanitizer)
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:-fsanitize=undefined -Wall -Wextra -Wno-unused>    
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>,$<BOOL:${CW_ADDRESS_SANITIZER_FL}>>:-fsanitize=address>    
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>,$<BOOL:${CW_THREAD_SANITIZER_FL}>>:-fsanitize=thread>    
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${CW_COVERAGE_FL}>>:--coverage>    
)

  

target_link_libraries(
  test_main
  PRIVATE
  GTest::gtest_main
  cw
  $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:-fsanitize=undefined>    
  $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>,$<BOOL:${CW_ADDRESS_SANITIZER_FL}>>:-lasan>    
  $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>,$<BOOL:${CW_THREAD_SANITIZER_FL}>>:-fsanitize=thread>    
  $<$<AND:$<CONFIG:Debug>,$<BOOL:${CW_COVERAGE_FL}>>:--coverage>    
)

include(GoogleTest)
gtest_discover_tests(test_main)
