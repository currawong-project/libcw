#ifndef cwNbMpScQueue_h
#define cwNbMpScQueue_h

namespace cw
{
  namespace nbmpscq
  {
    typedef handle<struct nbmpscq_str> handle_t;

    rc_t create( handle_t& hRef, unsigned initBlkN, unsigned blkByteN );
    rc_t destroy( handle_t& hRef );

    rc_t push( handle_t h, const void* blob, unsigned blobByteN );

    typedef struct blob_str
    {
      const void* blob;
      unsigned blobByteN;
    } blob_t;
    
    blob_t  next( handle_t h );
    rc_t advance( handle_t h );

    rc_t test( object_t* cfg );
    
  }
}


#endif
