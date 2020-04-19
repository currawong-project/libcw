#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwThread.h"
#include "cwThreadMach.h"


#include "cwSpScQueueTmpl.h"

namespace cw
{
  const int kDataByteN = 14;
    
  typedef struct msg_str
  {
    uint8_t dataByteN;
    uint8_t checksum;
    uint8_t data[ kDataByteN ];
  } msg_t;

  typedef spScQueueTmpl<msg_t> queue_t;
  
    
  typedef struct shared_str
  {
    queue_t*           q;       // Shared SPSC queue
    std::atomic<bool>  readyFl; // The consumer sets the readyFl at program startup when it is ready to start emptying the queue. 
  } shared_t;                   // This prevents the producer from immediately filling the queue before the consumer start.s
    
  typedef struct ctx_str
  {
    unsigned  id;             // thread id
    unsigned  iter;           // execution counter
    unsigned  msgN;           // count of msg's processed
    shared_t* share;          // shared variables
    uint8_t   prevId; 
  } ctx_t;


  void _producer( ctx_t* c )
  {
      
    bool readyFl = c->share->readyFl.load(std::memory_order_acquire);

    if( readyFl )
    {

      msg_t* m = c->share->q->get();
      
      m->dataByteN = kDataByteN;
      m->checksum  = 0;
      
      uint8_t d = (c->iter & 0xff);
      for(int i=0; i<kDataByteN; ++i)
      {
        m->data[i]   = d++;
        m->checksum += m->data[i];
      }

      c->share->q->publish();

      c->msgN++;

    }

    c->iter++;
      
  }

  void _consumer( ctx_t* c )
  {
    msg_t*         m            = nullptr;
    
    if( c->iter == 0 )
    {
      c->share->readyFl.store(true,std::memory_order_release);
    }
      
    if((m = c->share->q->pop()) != nullptr )
    {
      uint8_t curCheckSum = 0;
      for(unsigned i=0; i<kDataByteN; ++i)
        curCheckSum += m->data[i];
      
      if( curCheckSum != m->checksum )
        cwLogError(kOpFailRC,"Checksum mismatch.0x%x != 0x%x ",curCheckSum,m->checksum);
      else
      {
        uint8_t id = c->prevId + 1;
        if( id != m->data[0] )
          cwLogInfo("drop ");
        
        c->prevId = m->data[0];
      }

      c->msgN++;
    }

    c->iter++;
      
  }
    
  bool _threadFunc( void* arg )
  {
    ctx_t* c = static_cast<ctx_t*>(arg);

    switch( c->id )
    {
      case 0:
        _producer(c);
        break;
          
      case 1:
        _consumer(c);
        break;
          
      default:
        assert(0);
    }

    sleepMs( rand() & 0xf );
      
    return true;
  }

    
}

cw::rc_t cw::testSpScQueueTmpl()
{
  rc_t rc=kOkRC,rc0;
  
  thread_mach::handle_t h;
  const int             ctxArrayN = 2;
  ctx_t                 ctxArray[ctxArrayN];
  shared_t              share;
  const int             eleN  = 128;

  memset(&ctxArray,0,sizeof(ctxArray));
  
  // setup the thread context array
  ctxArray[0].id    = 0;
  ctxArray[0].share = &share;
  ctxArray[1].id    = 1;
  ctxArray[1].share = &share;
  
  share.readyFl.store(false,std::memory_order_release);

  share.q = new queue_t(eleN);
  
  // create the thread machine 
  if((rc = thread_mach::create( h, _threadFunc, ctxArray, sizeof(ctx_t), ctxArrayN )) != kOkRC )
  {
    rc = cwLogError(rc,"Thread machine create failed.");
    goto errLabel;
  }

  // start the thread machine
  if((rc = thread_mach::start(h)) != kOkRC )
  {
    cwLogError(rc,"Thread machine start failed.");
    goto errLabel;
  }

  sleepMs(5000);

 errLabel:
  if((rc0 = thread_mach::destroy(h)) != kOkRC )
    cwLogError(rc0,"Thread machine destroy failed.");

  delete share.q;
  
  printf("P:%i msgs:%i C:%i msgs:%i\n",ctxArray[0].iter, ctxArray[0].msgN, ctxArray[1].iter, ctxArray[1].msgN);

  return rcSelect(rc,rc0);    
}
