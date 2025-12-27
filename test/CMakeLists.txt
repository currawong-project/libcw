enable_testing()

add_executable(test_main)

target_sources(test_main
  PRIVATE
  test_main.cpp
  test_filesys.cpp
  test_lex.cpp
  test_object.cpp
)


target_link_libraries(
  test_main
  PRIVATE
  GTest::gtest_main
  cw
)

include(GoogleTest)
gtest_discover_tests(test_main)
