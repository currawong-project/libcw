#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwTime.h"
#include "cwObject.h"

#include "cwNbMpScQueue.h"

#include "cwThread.h"
#include "cwThreadMach.h"
#include "cwFile.h"

namespace cw
{
  namespace nbmpscq
  {
    typedef struct block_str
    {
      uint8_t*          buf;       // buf[ bufByteN ] 
      unsigned          bufByteN;  

      std::atomic<bool>     full_flag; // Set if this block is full (i.e. index >= bufByteN)
      std::atomic<unsigned> index;     // Offset to next avail byte in buf[]
      std::atomic<int>      eleN;      // Current count of elements stored in buf[]

      struct block_str* link;          // 
      
    } block_t;

    typedef struct node_str
    {
      std::atomic<struct node_str*> next;       // 0
      block_t*                      block;      // 8
      unsigned                      blobByteN;  // 16
      unsigned                      pad;        // 20-24 (mult. of 8)
      // blob data follows
    } node_t;

    static_assert( sizeof(node_t) % 8 == 0 );
    
    typedef struct nbmpscq_str
    {
      uint8_t* mem;       // Pointer to a single area of memory which holds all blocks.
      unsigned blkN;      // Count of blocks in blockL
      unsigned blkByteN;  // Size of each block_t.mem[] buffer
      
      block_t* blockL;    // Linked list of blocks
      
      std::atomic<int>  cleanBlkN;  // count of blocks that need to be cleaned
      unsigned          cleanProcN; // count of times the clear process has been run

      node_t*              stub; // dummy node
      std::atomic<node_t*> head; // last-in
      
      node_t*              tail; // first-out

      node_t*              peek;
      
    } nbmpscq_t;

    nbmpscq_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,nbmpscq_t>(h); }
    
    rc_t _destroy( nbmpscq_t* p )
    {
      rc_t rc = kOkRC;
      if( p != nullptr )
      {

        block_t* b = p->blockL;
        while( b != nullptr )
        {
          block_t* b0 = b->link;
          mem::release(b->buf);
          mem::release(b);
          b=b0;
        }
        
        
        mem::release(p->stub);
        mem::release(p);
      }
      return rc;
    }

    // _clean() is run by the consumer thread to make empty blocks available.
    void _clean( nbmpscq_t* p )
    {
      block_t* b = p->blockL;
      // for each block
      for(; b!=nullptr; b=b->link)
      {
        // if this block is full ...
        if( b->full_flag.load(std::memory_order_acquire) )
        {
          // ... and there are no more elements to be read from the block
          if( b->eleN.load(std::memory_order_acquire) <= 0 )
          {
            // decr. the cleanBlkN count
            unsigned cc = p->cleanBlkN.fetch_add(-1,std::memory_order_relaxed);
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
      p->cleanProcN += 1;
    }

    void _init_blob( blob_t& b, node_t* node )
    {
      if( node == nullptr )
      {
        b.blob      = nullptr;
        b.blobByteN = 0;
      }
      else
      {
        b.blob      = (uint8_t*)(node+1);
        b.blobByteN = node->blobByteN;
      }

      b.rc = kOkRC;
      
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
      unsigned      value;      // 
      test_share_t* share;      // pointer to global shared data
    } test_t;

    
    
    bool _test_threadFunc( void* arg )
    {
      test_t* t = (test_t*)arg;

      // get and increment a global shared counter
      t->value = t->share->cnt.fetch_add(1,std::memory_order_acq_rel);

      // push the current thread instance record
      push(t->share->qH,t,sizeof(test_t));

      // incrmemen this threads exec. counter
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
  
  if((rc = destroy(hRef)) != kOkRC )
    goto errLabel;

  p = mem::allocZ<nbmpscq_t>();
  
  p->stub = mem::allocZ<node_t>();
  p->head = p->stub;   // last-in
  p->tail = p->stub;   // first-out
  p->peek = nullptr;
  p->cleanBlkN = 0;
  
  p->blkN     = initBlkN;
  p->blkByteN = blkByteN;

  for(unsigned i=0; i<initBlkN; ++i)
  {
    block_t* b = mem::allocZ<block_t>();
    b->buf = mem::allocZ<uint8_t>(blkByteN);

    b->bufByteN = blkByteN;
    
    b->full_flag.store(false);
    b->index.store(0);
    b->eleN.store(0);
    
    b->link = p->blockL;
    p->blockL = b;
    
  }


  /*
  byteN       = initBlkN * (sizeof(block_t) + blkByteN );
  p->mem      = mem::allocZ<uint8_t>(byteN);
  
  for(unsigned i=0; i<byteN; i+=(sizeof(block_t) + blkByteN))
  {
    block_t* b = (block_t*)(p->mem+i);
    b->buf = (uint8_t*)(b + 1);
    b->bufByteN = blkByteN;
    
    b->full_flag.store(false);
    b->index.store(0);
    b->eleN.store(0);
    
    b->link = p->blockL;
    p->blockL = b;
  }
  */
  
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

  // TODO: handle case where blobByteN is greater than p->blkByteN
  // Note that this case will immediately overflow the queue.

  unsigned nodeByteN = blobByteN + sizeof(node_t);

  // force the size of the node to be a multiple of 8
  nodeByteN = ((nodeByteN-1) & 0xfffffff8) + 8;

  // We will eventually be addressing node_t records stored in pre-allocated blocks
  // of memory - be sure that they always begin on 8 byte alignment to conform
  // to Intel standard.
  assert( nodeByteN % 8 == 0 );

  
  for(; b!=nullptr; b=b->link)
  {
    if( b->full_flag.load(std::memory_order_acquire) == false )
    {
      // attempt to allocate nodeByteN bytes starting at b->index
      unsigned idx = b->index.fetch_add(nodeByteN, std::memory_order_acq_rel);

      // if the allocation was not valid
      if( idx >= b->bufByteN || idx+nodeByteN > b->bufByteN )
      {
        // incr the 'clean count' and mark the block as full
        p->cleanBlkN.fetch_add(1,std::memory_order_relaxed);
        b->full_flag.store(true,std::memory_order_release);
      }
      else
      {
        // otherwise this thread owns the allocated block
        node_t* n    = (node_t*)(b->buf + idx);
        n->blobByteN = blobByteN;
        n->block     = b;
        
        memcpy(b->buf+idx+sizeof(node_t),blob,blobByteN);

        // incr the block element count
        b->eleN.fetch_add(1,std::memory_order_release);
        
        n->next.store(nullptr);
        // Note that the elements of the queue are only accessed from the end of the queue (tail).
        // New nodes can therefore safely be updated in two steps:

        // 1. Atomically set _head to the new node and return 'old-head'
        node_t* prev   = p->head.exchange(n,std::memory_order_acq_rel);  

        // Note that at this point only the new node may have the 'old-head' as it's predecssor.
        // Other threads may therefore safely interrupt at this point.
      
        // 2. Set the old-head next pointer to the new node (thereby adding the new node to the list)
        prev->next.store(n,std::memory_order_release); // RELEASE 'next' to consumer            

        break;
      }
    }
  }

  // if there is no space left in the queue to store the incoming blob
  if( b == nullptr )
  {
    // TODO: continue to iterate through the blocks waiting for the consumer
    // to make more space available.
    rc = cwLogError(kBufTooSmallRC,"NbMpScQueue overflow.");
  }
  
  return rc;
  
}

cw::nbmpscq::blob_t  cw::nbmpscq::get( handle_t h )
{
  blob_t     blob;
  nbmpscq_t* p    = _handleToPtr(h);  
  node_t*    t    = p->tail;
  node_t*    node = t->next.load(std::memory_order_acquire); //  ACQUIRE 'next' from producer

  _init_blob( blob, node );
  
  return blob;  
}

cw::nbmpscq::blob_t cw::nbmpscq::advance( handle_t h )
{
  blob_t     blob;  
  nbmpscq_t* p    = _handleToPtr(h);
  node_t*    t    = p->tail;
  node_t*    next = t->next.load(std::memory_order_acquire); //  ACQUIRE 'next' from producer
  
  if( next != nullptr )
  {    
    p->tail = next;    

    block_t* b = next->block;
    int eleN = b->eleN.fetch_add(-1,std::memory_order_acq_rel);
    
    // next was valid and so eleN must be >= 1
    assert( eleN >= 1 );
    
  }

  if( p->cleanBlkN.load(std::memory_order_relaxed) > 0 )
    _clean(p);


  _init_blob(blob,next);
  
  return blob;
}


cw::nbmpscq::blob_t cw::nbmpscq::peek( handle_t h )
{
  blob_t blob;
  nbmpscq_t* p = _handleToPtr(h);
  node_t*    n = p->peek;

  // if p->peek is not set ... 
  if( n == nullptr )
  {
    // ... then set it to the tail
    n = p->tail->next.load(std::memory_order_acquire);  //  ACQUIRE 'next' from producer

  }

  _init_blob(blob,n);

  if( n != nullptr )
    p->peek = n->next.load(std::memory_order_acquire);
  
  return blob;
  
}



bool cw::nbmpscq::is_empty( handle_t h )
{
  nbmpscq_t* p = _handleToPtr(h);
  
  node_t* t    = p->tail;
  node_t* next = t->next.load(std::memory_order_acquire); //  ACQUIRE 'next' from producer

  return next == nullptr;
}

unsigned cw::nbmpscq::count( handle_t h )
{
  nbmpscq_t* p = _handleToPtr(h);

  block_t* b = p->blockL;
  int eleN = 0;
  for(; b!=nullptr; b=b->link)
    eleN += b->eleN.load(std::memory_order_acquire);

  return eleN;
}

cw::rc_t cw::nbmpscq::test( const object_t* cfg )
{
  rc_t rc=kOkRC,rc0,rc1;

  unsigned              testArrayN = 2;
  test_t*               testArray  = nullptr;
  unsigned              blkN       = 2;
  unsigned              blkByteN   = 1024;
  const char*           out_fname  = nullptr;
  time::spec_t          t0         = time::current_time();
  unsigned              testDurMs  = 0;
  test_share_t          share;
  handle_t              qH;
  thread_mach::handle_t tmH;
  file::handle_t        fH;

  if((rc = cfg->getv("blkN",blkN,
                     "blkByteN",blkByteN,
                     "testDurMs",testDurMs,
                     "threadN",testArrayN,
                     "out_fname",out_fname)) != kOkRC )
  {
    rc = cwLogError(rc,"Test params parse failed.");
    goto errLabel;
  }

  if((rc = file::open(fH,out_fname,file::kWriteFl)) != kOkRC )
  {
    rc = cwLogError(rc,"Error creating the output file:%s",cwStringNullGuard(out_fname));
    goto errLabel;
  }
  
  if( testArrayN == 0 )
  {
    rc = cwLogError(kInvalidArgRC,"The 'threadN' parameter must be greater than 0.");
    goto errLabel;
  }

  // create the thread intance records
  testArray = mem::allocZ<test_t>(testArrayN);

  // create the queue
  if((rc = create( qH, blkN, blkByteN )) != kOkRC )
  {
    rc = cwLogError(rc,"nbmpsc create failed.");
    goto errLabel;
  }

  share.qH = qH;
  share.cnt.store(0);
  
  for(unsigned i=0; i<testArrayN; ++i)
  {
    testArray[i].id    = i;
    testArray[i].share = &share;
  }
  
  // create the thread machine 
  if((rc = thread_mach::create( tmH, _test_threadFunc, testArray, sizeof(test_t), testArrayN )) != kOkRC )
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

  // run the test for 'testDurMs' milliseconds
  while( time::elapsedMs(t0) < testDurMs )
  {
    blob_t b = get(qH);
    if( b.blob != nullptr )
    {
      test_t* t =  (test_t*)b.blob;
      printf(fH,"%i %i %i %i\n",t->id,t->iter,t->value,b.blobByteN);
      advance(qH);
    }
  }
 
 errLabel:
  file::close(fH);
  
  if((rc0 = thread_mach::destroy(tmH)) != kOkRC )
    cwLogError(rc0,"Thread machine destroy failed.");

  if((rc1 = destroy(qH)) != kOkRC )
    cwLogError(rc1,"nbmpsc queue destroy failed.");

  if( testArray != nullptr )
    printf("P:%i %i\n",testArray[0].iter, testArray[1].iter);

  // TODO: read back the file and verify that none of the
  // global incrment values were dropped.  

  mem::release(testArray);
  
  return rcSelect(rc,rc0,rc1);  
  
}

