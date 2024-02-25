#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwObject.h"

#include "cwNbMpScQueue.h"

#include "cwThread.h"
#include "cwThreadMach.h"

namespace cw
{
  namespace nbmpscq
  {
    typedef struct block_str
    {
      uint8_t*          buf;       // buf[ bufByteN ] 
      unsigned          bufByteN;

      std::atomic<bool>     full_flag;
      std::atomic<unsigned> index; // offset to next avail byte in mem[]
      std::atomic<int>      eleN;  // count of elements in block

      struct block_str* link;
      
    } block_t;

    typedef struct node_str
    {
      std::atomic<struct node_str*> next;
      block_t*                      block;
      unsigned                      blobByteN;
      // blob data follows
    } node_t;

    typedef struct nbmpscq_str
    {
      unsigned blkN;      // count of blocks in blockL
      unsigned blkByteN;  // size of each block_t.mem[] buffer
      
      block_t* blockL;    // linked list of blocks
      std::atomic<int>  clean_cnt; // count of blocks that need to be cleaned

      node_t*              stub; // dummy node
      std::atomic<node_t*> head; // last-in
      node_t*              tail; // first-out
      
    } nbmpscq_t;

    nbmpscq_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,nbmpscq_t>(h); }
    
    rc_t _destroy( nbmpscq_t* p )
    {
      rc_t rc = kOkRC;
      if( p != nullptr )
      {
        mem::release(p->stub);
        mem::release(p->blockL);
        mem::release(p);
      }
      return rc;
    }

    void _clean( nbmpscq_t* p )
    {
      block_t* b = p->blockL;
      for(; b!=nullptr; b=b->link)
      {
        if( b->full_flag.load(std::memory_order_acquire) )
        {
          if( b->eleN.load(std::memory_order_acquire) <= 0 )
          {
            unsigned cc = p->clean_cnt.fetch_add(-1,std::memory_order_relaxed);
            assert(cc>=1);

            // Note: b->full_flag==true and p->eleN==0 so it is safe to reset the block
            // because all elements have been removed (eleN==0) and
            // no other threads will be accessing it (full_flag==true)
            b->eleN.store(0,std::memory_order_relaxed);
            b->index.store(0,std::memory_order_relaxed);
            b->full_flag.store(false,std::memory_order_release);
          }
        }
      }
    }


    typedef struct shared_str
    {
      handle_t              qH;
      std::atomic<unsigned> cnt;
      
    } test_share_t;
    
    typedef struct test_str
    {
      unsigned      id;         // thread id
      unsigned      iter;       // execution counter
      unsigned      value;
      test_share_t* share;
    } test_t;

    
    
    bool _threadFunc( void* arg )
    {
      test_t* t = (test_t*)arg;

      t->value = t->share->cnt.fetch_add(1,std::memory_order_acq_rel);

      push(t->share->qH,t,sizeof(t));
      
      t->iter += 1;
      
      sleepMs( rand() & 0xf );
      
      return true;
    }
    
          
  }
}

cw::rc_t cw::nbmpscq::create( handle_t& hRef, unsigned initBlkN, unsigned blkByteN )
{
  rc_t       rc    = kOkRC;
  nbmpscq_t* p     = nullptr;
  unsigned   byteN = 0;
  uint8_t*   mem   = nullptr;
  
  if((rc = destroy(hRef)) != kOkRC )
    goto errLabel;

  p = mem::allocZ<nbmpscq_t>();
  
  p->stub = mem::allocZ<node_t>();
  p->head = p->stub;   // last-in
  p->tail = p->stub;   // first-out 
  p->clean_cnt = 0;
  
  p->blkN     = initBlkN;
  p->blkByteN = blkByteN;
  byteN       = initBlkN * (sizeof(block_t) + blkByteN );
  mem         = mem::allocZ<uint8_t>(byteN);
  
  for(unsigned i=0; i<byteN; i+=(sizeof(block_t) + blkByteN))
  {
    block_t* b = (block_t*)(mem+i);
    b->buf = (uint8_t*)(b + 1);
    b->bufByteN = blkByteN;
    
    b->full_flag.store(false);
    b->index.store(0);
    b->eleN.store(0);
    
    b->link = p->blockL;
    p->blockL = b;
  }

  hRef.set(p);
  
errLabel:
  if(rc != kOkRC )
  {
    rc = cwLogError(rc,"NbMpScQueue destroy failed.");
    _destroy(p);
  }

  return rc;
}

cw::rc_t cw::nbmpscq::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if(!hRef.isValid())
    return rc;

  nbmpscq_t* p = _handleToPtr(hRef);
  
  if((rc = _destroy(p)) != kOkRC )
    goto errLabel;

  hRef.clear();
errLabel:

  if( rc != kOkRC )
    rc = cwLogError(rc,"NbMpScQueue destroy failed.");
  return rc;
  
}

cw::rc_t cw::nbmpscq::push( handle_t h, const void* blob, unsigned blobByteN )
{
  rc_t       rc = kOkRC;
  nbmpscq_t* p  = _handleToPtr(h);  
  block_t*   b  = p->blockL;

  unsigned nodeByteN = blobByteN + sizeof(node_t);
  
  for(; b!=nullptr; b=b->link)
  {
    if( !b->full_flag.load(std::memory_order_acquire) )
    {
      unsigned idx = b->index.fetch_add(nodeByteN, std::memory_order_acq_rel);

      if( idx >= b->bufByteN || idx+nodeByteN > b->bufByteN )
      {
        p->clean_cnt.fetch_add(1,std::memory_order_relaxed);
        b->full_flag.store(true,std::memory_order_release);
      }
      else
      {
        node_t* n    = (node_t*)(b->buf + idx);
        n->blobByteN = blobByteN;
        n->block     = b;

        b->eleN.fetch_add(1,std::memory_order_release);
        
        memcpy(b->buf+idx+sizeof(node_t),blob,blobByteN);

        n->next.store(nullptr);
        // Note that the elements of the queue are only accessed from the end of the queue (tail).
        // New nodes can therefore safely be updated in two steps:

        // 1. Atomically set _head to the new node and return 'old-head'
        node_t* prev   = p->head.exchange(n,std::memory_order_acq_rel);  

        // Note that at this point only the new node may have the 'old-head' as it's predecssor.
        // Other threads may therefore safely interrupt at this point.
      
        // 2. Set the old-head next pointer to the new node (thereby adding the new node to the list)
        prev->next.store(n,std::memory_order_release); // RELEASE 'next' to consumer            
        
      }
    }
  }
      
  if( b == nullptr )    
    rc = cwLogError(kBufTooSmallRC,"NbMpScQueue overflow.");
  
  return rc;
  
}

cw::nbmpscq::blob_t  cw::nbmpscq::next( handle_t h )
{
  blob_t blob;
  nbmpscq_t* p = _handleToPtr(h);
  
  node_t* t    = p->tail;
  node_t* n    = t->next.load(std::memory_order_acquire);  //  ACQUIRE 'next' from producer
  
  if( n == nullptr )
  {
    blob.blob      = nullptr;
    blob.blobByteN = 0;
  }
  else
  {
    blob.blob      = (uint8_t*)(n+1);
    blob.blobByteN = n->blobByteN;
  }
  
  return blob;
  
}

cw::rc_t cw::nbmpscq::advance( handle_t h )
{  
  nbmpscq_t* p = _handleToPtr(h);
  rc_t    rc   = kOkRC;
  node_t* t    = p->tail;
  node_t* next = t->next.load(std::memory_order_acquire); //  ACQUIRE 'next' from producer
  
  if( next != nullptr )
  {    
    p->tail = next;    

    int eleN = next->block->eleN.fetch_add(-1,std::memory_order_acq_rel);
    
    // next was valid and so eleN must be >= 1
    assert( eleN >= 1 );
    
  }

  if( p->clean_cnt.load(std::memory_order_relaxed) > 0 )
    _clean(p);

  return rc;
}

cw::rc_t cw::nbmpscq::test( object_t* cfg )
{
  rc_t rc=kOkRC,rc0,rc1;

  const int             testArrayN = 2;
  test_t                testArray[testArrayN];
  const unsigned        blkN       = 2;
  const unsigned        blkByteN   = 1024;
  time::spec_t          t0         = time::current_time();
  test_share_t          share;
  handle_t              qH;
  thread_mach::handle_t tmH;
  
  memset(&testArray,0,sizeof(testArray));

  // create the queue
  if((rc = create( qH, blkN, blkByteN )) != kOkRC )
  {
    rc = cwLogError(rc,"nbmpsc create failed.");
    goto errLabel;
  }

  share.qH = qH;
  share.cnt = 0;
  
  for(unsigned i=0; i<testArrayN; ++i)
  {
    testArray[i].id    = i;
    testArray[i].share = &share;
  }
 
  
  // create the thread machine 
  if((rc = thread_mach::create( tmH, _threadFunc, testArray, sizeof(test_t), testArrayN )) != kOkRC )
  {
    rc = cwLogError(rc,"Thread machine create failed.");
    goto errLabel;
  }

  // start the thread machine
  if((rc = thread_mach::start(tmH)) != kOkRC )
  {
    cwLogError(rc,"Thread machine start failed.");
    goto errLabel;
  }

  while( time::elapsedMs(t0) < 1000*10 )
  {
    blob_t b = next(qH);
    if( b.blob != nullptr )
    {
      test_t* t =  (test_t*)b.blob;
      printf("%i %i %i\n",t->id,t->iter,t->value);
      advance(qH);
    }
  }
 
 errLabel:
  if((rc0 = thread_mach::destroy(tmH)) != kOkRC )
    cwLogError(rc0,"Thread machine destroy failed.");

  if((rc1 = destroy(qH)) != kOkRC )
    cwLogError(rc1,"nbmpsc queue destroy failed.");

  printf("P:%i %i\n",testArray[0].iter, testArray[1].iter);

  return rcSelect(rc,rc0,rc1);  
  
}

