
cmake_minimum_required(VERSION 3.31)

project(libcw VERSION 1.0)

# Global settings
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED On)
set(CMAKE_VERBOSE_MAKEFILE ON CACHE BOOL "Enable verbose output" FORCE)

include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
  #URL_HASH SHA1=52eb8108c5bdec04579160ae17225d66034bd723
                  
)

FetchContent_MakeAvailable(googletest)


add_subdirectory(src)
add_subdirectory(apps)
add_subdirectory(test)
