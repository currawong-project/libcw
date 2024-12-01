//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwSpScBuf_h
#define cwSpScBuf_h

namespace cw
{

  namespace spsc_buf
  {
    typedef handle<struct spsc_buf_str> handle_t;

    rc_t create( handle_t& hRef, unsigned bufByteN );
    rc_t destroy( handle_t& hRef );

    // Copy data into the filling buffer.
    rc_t copyIn( handle_t h, const void* buf, unsigned bufByteN );

    // Get a count of the bytes contained in the filling buffer.
    unsigned fullByteCount( handle_t h );

    // Swap the buffers and return the one containing data.
    rc_t copyOut( handle_t h, void* buf, unsigned bufByteN, unsigned& returnedByteN_Ref  );

    rc_t test();
    
  }

  
}


#endif

