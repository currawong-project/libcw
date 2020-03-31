#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwWebSock.h"
#include "cwThread.h"
#include "cwWebSockSvr.h"
#include "cwText.h"

namespace cw
{
  namespace websockSrv
  {
    typedef struct websockSrv_str    
    {
      websock::handle_t _websockH;
      thread::handle_t  _thread;
      unsigned          _timeOutMs;
    } websockSrv_t;

    websockSrv_t* _handleToPtr(handle_t h) { return handleToPtr<handle_t,websockSrv_t>(h); }

    rc_t _destroy( websockSrv_t* p )
    {
      rc_t rc;

      if((rc = thread::destroy(p->_thread)) != kOkRC )
        return rc;

      if((rc = websock::destroy(p->_websockH)) != kOkRC )
        return rc;

      mem::release(p);

      return rc;
    }

    bool _websockSrvThreadCb( void* arg )
    {
      websockSrv_t* p = static_cast<websockSrv_t*>(arg);
      websock::exec( p->_websockH, p->_timeOutMs );
      return true;
    }
  }

}

cw::rc_t cw::websockSrv::create(
    handle_t&                  h, 
    websock::cbFunc_t          cbFunc,
    void*                      cbArg,
    const char*                physRootDir,
    const char*                dfltHtmlPageFn,
    int                        port,
    const websock::protocol_t* protocolA,
    unsigned                   protocolN,
    unsigned                   timeOutMs )
{
  rc_t rc;
  if((rc = destroy(h)) != kOkRC )
    return rc;

  websockSrv_t* p = mem::allocZ<websockSrv_t>();

  if((rc = websock::create( p->_websockH, cbFunc, cbArg, physRootDir, dfltHtmlPageFn, port, protocolA, protocolN )) != kOkRC )
    goto errLabel;
  

  if((rc = thread::create(p->_thread,_websockSrvThreadCb,p)) != kOkRC )
    goto errLabel;
  
  p->_timeOutMs = timeOutMs;

  h.set(p);
    
 errLabel:
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;
}

cw::rc_t cw::websockSrv::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
  
  if( !h.isValid() )
    return rc;

  websockSrv_t* p = _handleToPtr(h);
  
  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();  // the instance was released in _websockSrvDestroy()
  
  return rc;
}

cw::thread::handle_t cw::websockSrv::threadHandle( handle_t h )
{
  websockSrv_t* p = _handleToPtr(h);
  return p->_thread;
}

cw::websock::handle_t cw::websockSrv::websockHandle( handle_t h )
{
  websockSrv_t* p = _handleToPtr(h);
  return p->_websockH;
  
}


cw::rc_t cw::websockSrv::start( handle_t h )
{
  websockSrv_t* p = _handleToPtr(h);
  return thread::pause( p->_thread, thread::kWaitFl);
}

cw::rc_t cw::websockSrv::pause( handle_t h )
{
  websockSrv_t* p = _handleToPtr(h);
  return thread::pause( p->_thread, thread::kPauseFl | thread::kWaitFl);
}

namespace cw
{
  typedef struct appCtx_str
  {
    bool              quitFl = false;
    websock::handle_t wsH;
    unsigned          protocolId;
  } appCtx_t;
  
  // Note that this function is called from context of the websockSrv internal thread
  // and from within the websockExec() call.
  void websockCb( void* cbArg, unsigned protocolId, unsigned sessionId, websock::msgTypeId_t msg_type, const void* vmsg, unsigned byteN )
  {
    appCtx_t*   app = static_cast<appCtx_t*>(cbArg);
    const char* msg = static_cast<const char*>(vmsg);
    
    cwLogInfo("protcol:%i connection:%i type:%i bytes:%i %.*s ",protocolId,sessionId, msg_type, byteN, byteN, msg);


    if( msg_type == websock::kMessageTId )
    {
      if( textCompare(msg,"quit",4) == 0)
        app->quitFl              = true;
      else
        if( textCompare(msg,"bcast",5) == 0 )
        {
          sessionId = kInvalidId;           // send msg to all sessions
          vmsg = ((const char*)(vmsg)) + 6; // remove the 'bcast' prefix
          byteN -=6;
        }
      
      websock::send(app->wsH, app->protocolId, sessionId, vmsg, byteN );
    }
    
    
  }
 
}

cw::rc_t cw::websockSrvTest()
{
  rc_t                 rc;
  websockSrv::handle_t h;
  const char*          physRootDir    = "/home/kevin/src/cwtest/src/libcw/html/websockSrvTest";
  const char*          dfltHtmlPageFn = "test_websocket.html";
  unsigned             timeOutMs      = 50;
  int                  port           = 5687;
  unsigned             rcvBufByteN    = 128;
  unsigned             xmtBufByteN    = 128;
  appCtx_t             appCtx;

  enum
  {
   kHttpProtocolId       = 1,
   kWebsockSrvProtocolId = 2
  };
  
  websock::protocol_t protocolA[] =
  {
   { "http",                    kHttpProtocolId,      0,          0},
   { "websocksrv_test_protocol",kWebsockSrvProtocolId,rcvBufByteN,xmtBufByteN}
  };

  unsigned protocolN = sizeof(protocolA)/sizeof(protocolA[0]);

  
  if((rc = websockSrv::create( h, websockCb, &appCtx, physRootDir, dfltHtmlPageFn, port, protocolA, protocolN, timeOutMs )) != kOkRC )
    return rc;

  appCtx.wsH        = websockSrv::websockHandle(h);
  appCtx.protocolId = kWebsockSrvProtocolId;
  
  if((rc = websockSrv::start(h)) != kOkRC )
    goto errLabel;
  else
  {    
    while( !appCtx.quitFl )
    {      
      sleepMs(500);
    }
  }

 errLabel:

  websockSrv::destroy(h);

  return rc;
}
