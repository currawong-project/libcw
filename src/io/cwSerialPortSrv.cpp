//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
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
      serialPort::handle_t mgrH;
      thread::handle_t     threadH;
      unsigned             pollPeriodMs;
    } this_t;

    inline this_t* _handleToPtr( handle_t h ) { return handleToPtr<handle_t,this_t>(h); }

    rc_t _destroy( this_t* p )
    {
      rc_t rc = kOkRC;

      if((rc = thread::destroy(p->threadH)) != kOkRC )
        return rc;
      
      if((rc = serialPort::destroy(p->mgrH)) != kOkRC )
        return rc;


      mem::release(p);

      return rc;
    }

    // Do periodic non-blocking reads of each serial port and sleep between reads.
    bool threadCallbackAlt( void* arg )
    {
      this_t*  p     = static_cast<this_t*>(arg);
      unsigned portN = serialPort::portCount( p->mgrH );

      unsigned readN;
      
      for(unsigned i=0; i<portN; ++i)
      {
        
        rc_t rc = serialPort::receive_nb( p->mgrH, serialPort::portIndexToId(p->mgrH,i), readN );
        
        if( rc != kOkRC && rc != kTimeOutRC )
        {
          cwLogError(rc,"Serial server receive failed.");
          return false;
        }
      }

      if( readN == 0)
        sleepMs(20);

      return true;
    }

    // Wait for data to arrive on any port.
    bool threadCallback( void* arg )
    {
      this_t* p = static_cast<this_t*>(arg);

      unsigned readN;
      rc_t rc = serialPort::receive(p->mgrH,p->pollPeriodMs,readN);
      if( rc != kOkRC && rc != kTimeOutRC )
      {
        cwLogError(rc,"Serial server receive failed.");
        return false;
      }
      
      
      return true;
    }
  }  
}



cw::rc_t cw::serialPortSrv::create( handle_t& h, unsigned pollPeriodMs, unsigned recvBufByteN )
{
  rc_t rc = kOkRC;

  this_t* p = mem::allocZ<this_t>();

  if((rc = serialPort::create( p->mgrH, recvBufByteN)) != kOkRC )
      goto errLabel;

  if((rc = thread::create( p->threadH, threadCallback, p, "serial_srv")) != kOkRC )
    goto errLabel;

  p->pollPeriodMs = pollPeriodMs;

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


cw::serialPort::handle_t cw::serialPortSrv::serialHandle( handle_t h )
{
  this_t* p = _handleToPtr(h);
  return p->mgrH;
}

cw::thread::handle_t cw::serialPortSrv::threadHandle( handle_t h )
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


cw::rc_t cw::serialPortSrv::send( handle_t h, unsigned portId, const void* byteA, unsigned byteN )
{
  this_t* p = _handleToPtr(h);
  return cw::serialPort::send(p->mgrH,portId,byteA,byteN);
}


namespace cw
{

  void serialPortSrvTestCb( void* arg, unsigned userId, const void* byteA, unsigned byteN )
  {
    const char* text = static_cast<const char*>(byteA);
      
    for(unsigned i=0; i<byteN; ++i)
      printf("id:%i %c:%i\n",userId,text[i],(int)text[i]);

    if( byteN )
      fflush(stdout);      
  }
}

cw::rc_t cw::serialPortSrvTest()
{
  // Use this test an Arduino running study/serial/arduino_xmt_rcv/main.c
  
  rc_t                    rc               = kOkRC;
  bool                    quitFl           = false;
  unsigned                pollPeriodMs     = 50;
  unsigned                portId[]         = {0,1};
  const char*             device[]         = {"/dev/ttyACM1","/dev/ttyACM0"};
  unsigned                baud[]           = {38400,38400};
  unsigned                serialCfgFlags[] = {serialPort::kDefaultCfgFlags,serialPort::kDefaultCfgFlags};
  unsigned                portN            = 2; //sizeof(portId)/sizeof(portId[0]);
  unsigned                portIdx          = 0;  
  serialPortSrv::handle_t h;

  // open the serial port mgr
  if((rc = serialPortSrv::create(h,pollPeriodMs)) != kOkRC )
    return rc;

  // open the serial ports
  for(unsigned i=0; i<portN; ++i)
    if((rc = serialPort::createPort( serialPortSrv::serialHandle(h), portId[i], device[i], baud[i], serialCfgFlags[i], &serialPortSrvTestCb, nullptr)) != kOkRC )
      goto errLabel;

  // start the server
  serialPortSrv::start(h);
  
  printf("q=quit\n");
  while(!quitFl)
  {
    char c = getchar();
    
    if( c == 'q')
      quitFl = true;
    else
      if( '0' <= c and c <= 'z' )
      {
        // send the output to consecutive ports
        serialPortSrv::send(h,portId[portIdx],&c,1);
        portIdx = (portIdx+1) % portN;
      }
      
    
  }

 errLabel:
  
  serialPortSrv::destroy(h);
  return rc;
}



