function( cwFFT_Func target_name )
  
  # Locate the library binary (e.g., libfftw3.a or libfft23.lib)
  find_library(FFTW3D_LIBRARY_PATH NAMES fftw3 HINTS ${CW_FFTW_LIB_HINTS} )
  find_library(FFTW3F_LIBRARY_PATH NAMES fftw3f HINTS  ${CW_FFTW_LIB_HINTS} )

  # Locate the directory containing the header files
  find_path(FFTW_INCLUDE_DIR NAMES fftw3.h HINTS ${CW_FFTW_INCLUDE_HINTS} )

  # Verify that the library was found.
  if(NOT FFTW3D_LIBRARY_PATH)
    message(FATAL_ERROR "Could not find the library FFTW3d.")
  else()
    if(NOT FFTW3F_LIBRARY_PATH)
      message(FATAL_ERROR "Could not find the library FFTW3f.")
    else()
      if( NOT FFTW_INCLUDE_DIR )
	message(FATAL_ERROR "Could not find FFTW3 include path.")
      else()
	# applications linking against this library will now automatically define cwFFTW
	target_compile_definitions(${target_name} PUBLIC cwFFTW )
      endif()
    endif()    
  endif()

  # Create an IMPORTED target named 'FFTw3::FFTw3'
  add_library(FFTw3d::FFTw3d UNKNOWN IMPORTED)
  add_library(FFTw3f::FFTw3f UNKNOWN IMPORTED)

  # Set the location property for the imported library
  set_target_properties(FFTw3d::FFTw3d PROPERTIES
    IMPORTED_LOCATION "${FFTW3D_LIBRARY_PATH}"
    INTERFACE_INCLUDE_DIRECTORIES "${FFTW_INCLUDE_DIR}"
  )

  set_target_properties(FFTw3f::FFTw3f PROPERTIES
    IMPORTED_LOCATION "${FFTW3F_LIBRARY_PATH}"
    INTERFACE_INCLUDE_DIRECTORIES "${FFTW_INCLUDE_DIR}"
  )  
  
  # Link libcw internal library to the external library
  target_link_libraries(${target_name} PRIVATE FFTw3d::FFTw3d FFTw3f::FFTw3f)
endfunction()
