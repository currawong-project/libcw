#ifndef cwNbMpScQueue_h
#define cwNbMpScQueue_h

/*
Non-blocking, Lock-free Queue: 
=================================

Push
----
0. Produceers go down the a list of blocks (nbmpscq.blockL)
if a block is not already full then it atomically
fetch-add's block->write_idx by the size of the the element
to be inserted.

1. If after the fetch-add the write_idx is <= block->byteN then 
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

    // push() is called by multiple producer threads to insert
    // an element in the queue.  Note that the 'blob' is copied into
    // the queue and therefore can be released by the caller.
    rc_t push( handle_t h, const void* blob, unsigned blobByteN );

    typedef struct blob_str
    {
      const void* blob;
      unsigned blobByteN;
    } blob_t;

    // get() is called by the single consumer thread to access the
    // current blob at the front of the queue.  Note that this call
    // does not change the state of the queue.
    blob_t  get( handle_t h );

    // advance() disposes of the blob at the front of the
    // queue and makes the next blob current.
    rc_t advance( handle_t h );

    rc_t test( const object_t* cfg );
    
  }
}


#endif
