#ifndef cwLib_H
#define cwLib_H


namespace cw
{
  namespace lib
  {
    typedef handle<struct lib_str> handle_t;

    rc_t  initialize( handle_t& h, const char* dirStr );
    rc_t  finalize(   handle_t& h );
    // libIdRef is set to kInvalidId if rc != kOkRC
    rc_t  open(       handle_t h, const char* fn, unsigned& libIdRef );
    rc_t  close(      handle_t h, unsigned libId );
    void* symbol(     handle_t h, unsigned libId, const char* symName );

    // open all the libraries in a directory
    rc_t  scan(       handle_t h, const char* dir );

    // Return the count of open libraries,
    unsigned    count(handle_t h );
    unsigned    indexToId( handle_t h, unsigned idx );

    // Return the name associated with the i'th library
    const char* name( handle_t h, unsigned id );
  }
}


#endif
