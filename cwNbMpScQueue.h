#ifndef cwNbMpScQueue_h
#define cwNbMpScQueue_h

/*
Non-blocking, Lock-free Queue: 
=================================

Push
----
0. Produceers go to next block, if the write-position is valid,
then fetch-add the write-position forward to allocate space.

1. If after the fetch-add the area is valid then 
   - atomically incr ele-count, 
   - copy in ele 
   - place the block,ele-offset,ele-byte-cnt onto the NbMpScQueue().

2. else (the area is invalid) goto 0.

Pop
----
1. copy out next ele.
2. decr. block->ele_count
3. if the ele-count is 0 and write-offset is invalid
reset the write-offset to 0.
*/



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

    rc_t test( const object_t* cfg );
    
  }
}


#endif
