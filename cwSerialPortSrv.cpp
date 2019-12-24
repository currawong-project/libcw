#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwThread.h"
#include "cwSerialPort.h"
#include "cwSerialPortSrv.h"


namespace cw
{
  namespace serialPortSrv
  {
    typedef struct this_str
    {
      serialPort::handle_t portH;
      thread::handle_t     threadH;
      unsigned            _pollPeriodMs;
    } this_t;

    inline this_t* _handleToPtr( handle_t h ) { return handleToPtr<handle_t,this_t>(h); }

    rc_t _destroy( this_t* p )
    {
      rc_t rc = kOkRC;
      
      if((rc = serialPort::destroy(p->portH)) != kOkRC )
        return rc;

      if((rc = thread::destroy(p->threadH)) != kOkRC )
        return rc;

      memRelease(p);

      return rc;
    }
    
    bool threadCallback( void* arg )
    {
      this_t* p = static_cast<this_t*>(arg);

      unsigned readN;
      if( serialPort::isopen(p->portH) )
        serialPort::receive(p->portH,p->_pollPeriodMs,readN);
      
      
      return true;
    }
  }  
}



cw::rc_t cw::serialPortSrv::create( handle_t& h, const char* deviceStr, unsigned baudRate, unsigned cfgFlags, serialPort::callbackFunc_t cbFunc, void* cbArg, unsigned pollPeriodMs )
{
  rc_t rc = kOkRC;

  this_t* p = memAllocZ<this_t>();
      
  if((rc = serialPort::create( p->portH, deviceStr, baudRate, cfgFlags, cbFunc, cbArg )) != kOkRC )
    goto errLabel;

  if((rc = thread::create( p->threadH, threadCallback, p)) != kOkRC )
    goto errLabel;

  p->_pollPeriodMs = pollPeriodMs;

 errLabel:
  if( rc != kOkRC )
    _destroy(p);
  else
    h.set(p);
  
  return rc; 
}

cw::rc_t cw::serialPortSrv::destroy(handle_t& h )
{
  rc_t rc = kOkRC;

  if( !h.isValid() )
    return rc;
  
  this_t* p = _handleToPtr(h);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
  return rc; 
}


cw::serialPort::handle_t cw::serialPortSrv::portHandle( handle_t h )
{
  this_t* p = _handleToPtr(h);
  return p->portH;
}

cw::thread::handle_t           cw::serialPortSrv::threadHandle( handle_t h )
{
  this_t* p = _handleToPtr(h);
  return p->threadH;
}

cw::rc_t cw::serialPortSrv::start( handle_t h )
{
  this_t* p = _handleToPtr(h);
  return cw::thread::pause(p->threadH, thread::kWaitFl );
}

cw::rc_t cw::serialPortSrv::pause( handle_t h )
{
  this_t* p = _handleToPtr(h);
  return cw::thread::pause(p->threadH, thread::kPauseFl | thread::kWaitFl );
}


cw::rc_t cw::serialPortSrv::send( handle_t h, const void* byteA, unsigned byteN )
{
  this_t* p = _handleToPtr(h);
  return cw::serialPort::send(p->portH,byteA,byteN);
}


namespace cw
{

  void serialPortSrvTestCb( void* arg, const void* byteA, unsigned byteN )
  {
    const char* text = static_cast<const char*>(byteA);
      
    for(unsigned i=0; i<byteN; ++i)
      printf("%c:%i ",text[i],(int)text[i]);

    if( byteN )
      fflush(stdout);      
  }
}

cw::rc_t cw::serialPortSrvTest()
{
  // Use this test an Arduino running study/serial/arduino_xmt_rcv/main.c  
  rc_t                      rc             = kOkRC;
  const char*               device         = "/dev/ttyACM0";
  unsigned                  baud           = 38400;
  unsigned                  serialCfgFlags = serialPort::kDefaultCfgFlags;
  unsigned                  pollPeriodMs   = 50;
  serialPortSrv::handle_t   h;
  
  rc = serialPortSrv::create(h,device,baud,serialCfgFlags,&serialPortSrvTestCb,nullptr,pollPeriodMs);

  serialPortSrv::start(h);
  
  bool quitFl = false;
  printf("q=quit\n");
  while(!quitFl)
  {
    char c = getchar();
    
    if( c == 'q')
      quitFl = true;
    else
      if( '0' <= c and c <= 'z' )
        serialPortSrv::send(h,&c,1);
      
    
  }

  serialPortSrv::destroy(h);
  return rc;
}



