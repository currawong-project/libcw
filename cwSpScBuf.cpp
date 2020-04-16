#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwSpScBuf.h"
#include "cwThread.h"
#include "cwThreadMach.h"

namespace cw
{
  namespace spsc_buf
  {
    typedef struct spsc_buf_str
    {
      uint8_t*              buf;
      unsigned              bufByteN;
      std::atomic<uint8_t*> w;    // write ptr
      std::atomic<uint8_t*> r;    // read ptr 
    } spsc_buf_t;

    // Note: r==w indicates an empty buffer.
    // Therefore 'w' may never be advanced such that it equals 'r',
    // however, 'r' may be advanced such that it equals 'w'.

    spsc_buf_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,spsc_buf_t>(h); }

    rc_t _destroy( spsc_buf_t* p )
    {
      mem::release(p->buf);
      mem::release(p);
      return kOkRC;
    }

    unsigned _fullByteCount( spsc_buf_t* p, uint8_t* r, uint8_t* w )
    {
      if( r == w )
        return 0;

      if( r < w )
        return w - r;

      return p->bufByteN - (r - w);
    }

    unsigned _emptyByteCount( spsc_buf_t* p, uint8_t* r, uint8_t* w )
    { return p->bufByteN - _fullByteCount(p,r,w); }
    
   
  }
}

cw::rc_t cw::spsc_buf::create( handle_t& hRef, unsigned bufByteN )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  spsc_buf_t* p = mem::allocZ<spsc_buf_t>();
  p->buf        = mem::allocZ<uint8_t>(bufByteN);
  p->bufByteN   = bufByteN;
  p->w          = p->buf;
  p->r          = p->buf;

  hRef.set(p);
    
  return rc;
}

cw::rc_t cw::spsc_buf::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
    return kOkRC;

  spsc_buf_t* p = _handleToPtr(hRef);
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;
}


cw::rc_t cw::spsc_buf::copyIn( handle_t h, const void* iBuf, unsigned iN )
{
  rc_t        rc = kOkRC;
  spsc_buf_t* p  = _handleToPtr(h);
  uint8_t*    w  = p->w.load(std::memory_order_acquire);
  uint8_t*    r  = p->r.load(std::memory_order_acquire);
  uint8_t*    e  = p->buf + p->bufByteN;
  uint8_t*    w1;
  unsigned    n0;
  unsigned    n1 = 0;

  // if r is behind w (then the write may split into two parts)
  if( r <= w )
  {

    // if there is space between w and the EOB to accept the write  ...
    if( iN <= (unsigned)(e-w) )
    {
      n0 = iN;  // fill the space after w

      if( w + iN == r )
      {
        rc = kBufTooSmallRC;
      }
    }
    else // ... otherwise the write must wrap
    {
      n0 = e-w;   // fill the space between w and EOB
      n1 = iN-n0; // then begin writing at the beginning of the buffer
      
      if( p->buf + n1 >= r )
      {
        rc = kBufTooSmallRC;
      }
      
    }    
  }
  else // r > w  : r is in front of w (the write will not split)
  {
    if(  iN < (unsigned)(r - w) )
    {
      n0 = iN; 
    }
    else
    {
      rc = kBufTooSmallRC;
    }
    
  }

    
  if( rc != kOkRC )
    rc = cwLogError(rc,"spsc_buf overflowed.");
  else
  {
    const uint8_t* src = static_cast<const uint8_t*>(iBuf);
    memcpy(w,src,n0);
    w1 = w + n0;
    if( n1 )
    {
      memcpy(p->buf,src+n0,n1);
      w1 = p->buf + n1;
    }

    p->w.store(w1,std::memory_order_release);

  }


  return rc;
  
}

unsigned cw::spsc_buf::fullByteCount( handle_t h )
{
  spsc_buf_t* p = _handleToPtr(h);
  uint8_t*    r = p->r.load(std::memory_order_acquire);
  uint8_t*    w = p->w.load(std::memory_order_acquire);

  return _fullByteCount(p,r,w);
}

cw::rc_t cw::spsc_buf::copyOut( handle_t h, void* buf, unsigned bufByteN, unsigned& returnedByteN_Ref  )
{  
  spsc_buf_t* p    = _handleToPtr(h);
  uint8_t*    r    = p->r.load(std::memory_order_acquire);
  uint8_t*    w    = p->w.load(std::memory_order_acquire);
  uint8_t*    e    = p->buf + p->bufByteN;
  uint8_t*    oBuf = static_cast<uint8_t*>(buf);
  uint8_t*    r1   = nullptr;
  unsigned    n0   = 0;
  unsigned    n1   = 0;

  returnedByteN_Ref = 0;

  if( r == w )
  {
    return kOkRC;
  }
  
  // if the 'w' is in front of 'r' - then only one segment needs to be copied out
  if( r < w )
  {
    n0 = w-r;
    r1 = r + n0;
  }
  else // otherwise two segments need to be copied out
  {
    n0 = e-r;
    n1 = w-p->buf;
    r1 = p->buf + n1;
  }

  // check that the return buffer is large enough
  if( n0+n1 > bufByteN )
    return cwLogError(kBufTooSmallRC,"The return buffer is too small.");

  memcpy(oBuf, r, n0);
  if( n1 )
    memcpy(oBuf+n0, p->buf, n1);

  returnedByteN_Ref = n0 + n1;
    
  p->r.store(r1,std::memory_order_release);
    
  return kOkRC;
}



namespace cw
{
  namespace spsc_buf
  {
    const int kDataByteN = 14;
    
#pragma pack(push, 1)    
    typedef struct msg_str
    {
      uint8_t dataByteN;
      uint8_t checksum;
      uint8_t data[ kDataByteN ];
    } msg_t;
#pragma pack(pop)
    
    typedef struct shared_str
    {
      spsc_buf::handle_t h;       // Shared SPSC queue
      std::atomic<bool>  readyFl; // The consumer sets the readyFl at program startup when it is ready to start emptying the queue. 
    } shared_t;                   // This prevents the producer from immediately filling the queue before the consumer start.s
    
    typedef struct ctx_str
    {
      unsigned  id;             // thread id
      unsigned  iter;           // execution counter
      unsigned  msgN;           // count of msg's processed
      unsigned  state;          // used by consumer to hold the parser state
      shared_t* share;          // shared variables
    } ctx_t;


    void _producer( ctx_t* c )
    {
      msg_t m;
      
      bool readyFl = c->share->readyFl.load(std::memory_order_acquire);

      if( readyFl )
      {

        m.dataByteN = kDataByteN;
        m.checksum  = 0;
      
        uint8_t d = (c->iter & 0xff);
        for(int i=0; i<kDataByteN; ++i)
        {
          m.data[i]   = d++;
          m.checksum += m.data[i];
        }

        spsc_buf::copyIn(c->share->h,&m,sizeof(m));

        c->msgN++;

      }

      c->iter++;
      
    }

    void _consumer( ctx_t* c )
    {
      // message parser state values
      enum
      {
       kBegin,
       kChecksum,
       kData
      };

      const unsigned kBufByteN    = 128;
      uint8_t        buf[ kBufByteN ];
      unsigned       retBytesRead = 0;
      uint8_t        msgByteN     = 0; // Count of bytes in this msg data array
      uint8_t        msgCheckSum  = 0; // Checksum of this msg
      unsigned       curMsgIdx    = 0; // The parser location (0<=curMsgIdx < msgByteN)
      uint8_t        curCheckSum  = 0; // The accumulating checksum
      
      if( c->iter == 0 )
      {
        c->share->readyFl.store(true,std::memory_order_release);
        c->state = kBegin;
      }
      
      if(spsc_buf::copyOut( c->share->h, buf, kBufByteN, retBytesRead  ) == kOkRC && retBytesRead > 0)
      {

        uint8_t* b    = buf;
        uint8_t* bend = b + retBytesRead;
        
        
        for(; b < bend; ++b)
        {
          switch( c->state )
          {
            case kBegin:
              msgByteN = *b;
              c->state = kChecksum;
              break;
              
            case kChecksum:
              msgCheckSum = *b;
              curCheckSum = 0;
              curMsgIdx   = 0;
              c->state    = kData;
              break;
              
            case kData:
              curCheckSum += (*b);
              curMsgIdx   += 1;
              if( curMsgIdx == msgByteN )
              {
                if( curCheckSum != msgCheckSum )
                  cwLogError(kOpFailRC,"Checksum mismatch.0x%x != 0x%x ",curCheckSum,msgCheckSum);
                c->state = kBegin;
                c->msgN++;
              }
              break;
              
            default:
              assert(0);
          }
        }
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
}


cw::rc_t cw::spsc_buf::test()
{
  rc_t rc=kOkRC,rc0,rc1;
  
  thread_mach::handle_t h;
  const int             ctxArrayN = 2;
  ctx_t                 ctxArray[ctxArrayN];
  shared_t              share;
  const int             bufByteN  = 1024;

  memset(&ctxArray,0,sizeof(ctxArray));
  
  // setup the thread context array
  ctxArray[0].id    = 0;
  ctxArray[0].share = &share;
  ctxArray[1].id    = 1;
  ctxArray[1].share = &share;
  
  share.readyFl.store(false,std::memory_order_release);

  // create the SPSC buffer
  if((rc = create( share.h, bufByteN )) != kOkRC )
    return cwLogError(rc,"spsc_buf create failed.");

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

  if((rc1 = spsc_buf::destroy(share.h)) != kOkRC )
    cwLogError(rc1,"spsc_buf destroy failed.");

  printf("P:%i msgs:%i C:%i msgs:%i\n",ctxArray[0].iter, ctxArray[0].msgN, ctxArray[1].iter, ctxArray[1].msgN);

  return rcSelect(rc,rc0,rc1);  
}



