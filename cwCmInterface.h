#ifndef cwCmInterface_h
#define cwCmInterface_h

namespace cw
{
  namespace cm {

    extern "C" { struct cmCtx_str; }

    typedef handle< struct cm_str > handle_t; 
    
    rc_t create( handle_t& hRef );
    rc_t destroy( handle_t& hRef );
    ::cmCtx_t* context( handle_t h );
  }
}


#endif
