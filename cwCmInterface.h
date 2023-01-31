#ifndef cwCmInterface_h
#define cwCmInterface_h

extern "C" {
  struct cmCtx_str;
  struct cmProcCtx_str;
}

namespace cw
{
  namespace cm {


    typedef handle< struct cm_str > handle_t; 
    
    rc_t       create( handle_t& hRef );
    rc_t       destroy( handle_t& hRef );

   
    extern "C" struct cmCtx_str*     context( handle_t h );
    extern "C" struct cmProcCtx_str* proc_context( handle_t h );
  }
}


#endif
