add_library(cw STATIC)

include("../cmake/source_files.cmake")
include("../cmake/fftw.cmake")


option(CW_FFTW_FL    "Use FFTW for FFT functions." ON)
option(CW_MKL_FL     "Use Intel Math Kernel Library." OFF)
option(CW_WEBSOCK_FL "Use libwebsockets." ON)
option(CW_ALSA_FL    "Use ALSA." ON)
option(CW_ADDRESS_SANITIZER_FL "Enable the address sanitizer." OFF)
option(CW_THREAD_SANITIZER_FL "Enable the address sanitizer." OFF)
option(CW_COVERAGE_FL "Enable testing coverage analysis." OFF)

target_sources(cw
  PRIVATE
  ${CORE_SRC_FILES}
  ${IO_SRC_FILES}
  ${FLOW_SRC_FILES}
  ${CW_SRC_FILES}
  ${IO_COMP_SRC_FILES}
  ${IO_FLOW_SRC_FILES}
  
  PUBLIC
  FILE_SET hdr_files
  TYPE HEADERS
  BASE_DIRS core io flow cw io_components io_flow
  FILES

  ${CORE_HDR_FILES}
  ${IO_HDR_FILES}
  ${FLOW_HDR_FILES}
  ${CW_HDR_FILES}
  ${IO_COMP_HDR_FILES}
  ${IO_FLOW_HDR_FILES}
)



target_compile_options(cw PRIVATE
    # Add extra warnings in all configurations
    -Wno-multichar
    
    # Add specific flags only for the Release configuration (e.g., specific CPU optimizations)
    $<$<CONFIG:Release>:-march=native>

    # Add specific flags only for the Debug configuration (e.g., Address Sanitizer)
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:-fsanitize=undefined -Wall -Wextra -Wno-unused>    
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>,$<BOOL:${CW_ADDRESS_SANITIZER_FL}>>:-fsanitize=address>    
    $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>,$<BOOL:${CW_THREAD_SANITIZER_FL}>>:-fsanitize=thread>    
    $<$<AND:$<CONFIG:Debug>,$<BOOL:${CW_COVERAGE_FL}>>:--coverage>    
)

# BUILD_INTERFACE are include dir's searched when the library is built
# INSTALL_INTERFACE are include dir's searched when the library is used 
target_include_directories(cw PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
  )
  
find_package(Threads REQUIRED)

target_link_libraries(cw
  PRIVATE
  Threads::Threads
  ${CMAKE_DL_LIBS}
  $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>>:-fsanitize=undefined>    
  $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>,$<BOOL:${CW_ADDRESS_SANITIZER_FL}>>:-lasan>    
  $<$<AND:$<CONFIG:Debug>,$<CXX_COMPILER_ID:GNU>,$<BOOL:${CW_THREAD_SANITIZER_FL}>>:-fsanitize=thread>    
  $<$<AND:$<CONFIG:Debug>,$<BOOL:${CW_COVERAGE_FL}>>:--coverage>    
)
  
if(UNIX)
  target_compile_definitions(cw PRIVATE OS_LINUX)
endif()

if( CW_FFTW_FL )
  cwFFT_Func(cw)
else()
  message(STATUS "FFTW is not being used.")
endif()

if( CW_WEBSOCK_FL )
  find_package(libwebsockets ${CW_WEBSOCK_VERSION} REQUIRED)
  target_link_libraries(cw PRIVATE websockets)
  target_compile_definitions(cw PUBLIC cwWEBSOCK)
else()
  message(STATUS "libwebsocket is not being used.")
endif()

if( CW_ALSA_FL )
  find_package(ALSA REQUIRED)
  target_link_libraries(cw PRIVATE ALSA::ALSA)
  target_compile_definitions(cw PUBLIC cwALSA)
else()
  message(STATUS "ALSA is not being used.")
endif()

if( CW_MKL_FL )
  find_package(MKL CONFIG REQUIRED)
  target_link_libraries(cw PRIVATE MKL::MKL )
  target_compile_definitions(cw PUBLIC cwMKL)
else()
  message(STATUS "Intel Math Kernel Library is not being used.")
endif()



#
# Install the various library artficacts
#
install(
    TARGETS cw
    EXPORT CwTargets
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
    FILE_SET hdr_files DESTINATION include
)


#
# Create and install the package config files
#
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/CwConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY ExactVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CwConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/CwConfig.cmake"
    INSTALL_DESTINATION lib/cmake/Cw
)

install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/CwConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/CwConfigVersion.cmake"
    DESTINATION lib/cmake/Cw
)

install(
    EXPORT CwTargets
    FILE CwTargets.cmake
    NAMESPACE Cw::
    DESTINATION lib/cmake/Cw
)

