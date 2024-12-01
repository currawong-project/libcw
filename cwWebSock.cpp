//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwFileSys.h"
#include "cwObject.h"
#include "cwWebSock.h"
#include "cwNbMpScQueue.h"
#include "cwTime.h"

#include <libwebsockets.h>
#include <memory>

namespace cw
{
  namespace websock
  {

    #define ALLOC_FD_CNT 16
    
    struct websock_str;

    static int  _custom_init_private(struct lws_context *cx, void *_loop, int tsi);
    static int  _custom_sock_accept(struct lws *wsi);
    static void _custom_io(struct lws *wsi, unsigned int flags);
    static int  _custom_wsi_logical_close(struct lws *wsi);

    struct pt_eventlibs_custom
    {
      struct websock_str* ws;
    };
    
    // Internal outgoing msg structure.
    typedef struct msg_str
    {
      unsigned        protocolId; //  0 Protocol associated with this msg.
      unsigned        sessionId;  //  4 Session Id that this msg should be sent to or kInvalidId if it should be sent to all sessions.
      unsigned char*  msg;        //  8 Msg data array.
      unsigned        msgByteN;   // 16 Count of bytes in msg[].
      unsigned        msgId;      // 20 The msgId assigned when this msg is addded to the protocol state msg queue.
      unsigned        sessionN;   // 24 Count of sessions to which this msg has been sent.
      struct msg_str* link;       // 28 Pointer to next message or nullptr if this is the last msg in the queue.
      unsigned        pad;        // 36-40 (make size of msg_t a multiple of 8)
    } msg_t;

    static_assert( sizeof(msg_t) % 8 == 0 ); 

    typedef struct websock_str
    {
      cbFunc_t               _cbFunc;                  //
      void*                  _cbArg;                   //
      struct lws_context*    _ctx           = nullptr; //  
      struct lws_protocols*  _protocolA     = nullptr; // Websocket internal protocol state array
      unsigned               _protocolN     = 0;       // Count of protocol records in _protocolA[].
      unsigned               _nextSessionId = 0;       // Next session id.
      unsigned               _connSessionN  = 0;       // Count of connected sessions.
      struct lws_http_mount* _mount         = nullptr; //
      nbmpscq::handle_t      _qH; // Thread safe, non-blocking, protocol independent msg queue.

      lws_pollfd* _pollfdA;     // socket handle array used by poll()
      int         _pollfdMaxN;
      int         _pollfdN;

      unsigned _sendMsgCnt;    // Count of msg's sent 
      unsigned _sendMaxByteN;  // Max size across all sent msg's

      unsigned _recvMsgCnt;    // Count of msg's recv'd
      unsigned _recvMaxByteN;  // Max size across all recv'd msg's

      unsigned _execN;
      unsigned _execSumMs;
      
      struct lws_event_loop_ops _event_loop_ops_custom;
      lws_plugin_evlib_t        _evlib_custom;
      
    } websock_t;

    
    inline websock_t* _handleToPtr(handle_t h)
    { return handleToPtr<handle_t,websock_t>(h); }


    // Internal session record.
    typedef struct session_str
    {
      unsigned id;          // This sessions id.
      unsigned protocolId;  // This sessions protocol.
      unsigned nextMsgId;   // Id of the next msg this session will receieve.
    } session_t;
  
    // Application protocol state record - each lws_protocols record in _protocolA[] points to one of these records.
    typedef struct protocolState_str
    {
      websock_t*         thisPtr;       // Pointer to this websocket.
      unsigned           nextNewMsgId;  // Id of the next message to add to this outgoing msg queue.
      msg_t*             endMsg;        // End of the protocol outgoing msg queue: next message to be written to the remote endpoint.
      msg_t*             begMsg;        // Begin of the protocol outgoing msg queue: last msg added to the outgoing queue by the application.
      unsigned           sessionN;      // Count of sessions using this protocol.
    } protocolState_t;

    // This callback is always from protocol 0 which receives messages when a system socket is created,deleted, or changed.
    int _httpCallback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
    {

      const struct lws_protocols* p = lws_get_protocol(wsi);

      if( p == nullptr || p->user == nullptr )
      {
        cwLogError(kInvalidArgRC,"Invalid protocol record on http websock callback.");
        return 0;               // TODO: issue a warning
      }
      
      protocolState_t* ps = static_cast<protocolState_t*>(p->user);

      if( ps == nullptr || ps->thisPtr == nullptr )
      {
        cwLogError(kInvalidArgRC,"Invalid protocol state record on http websock callback.");
        return 0;               // TODO: issue a warning
      }

      return lws_callback_http_dummy(wsi,reason,user,in,len);
    }
    
    int _internalCallback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
    {

      const struct lws_protocols* p = lws_get_protocol(wsi);
      
      if( p == nullptr || p->user == nullptr )
      {
        cwLogError(kInvalidArgRC,"Invalid protocol record on websock callback.");
        return 0;
      }
      
      protocolState_t* ps = static_cast<protocolState_t*>(p->user);

      if( ps == nullptr || ps->thisPtr == nullptr )
      {
        cwLogError(kInvalidArgRC,"Invalid protocol state record on websock callback.");
        return 0;
      }

      session_t*                  sess       = static_cast<session_t*>(user);
      const struct lws_protocols* proto      = lws_get_protocol(wsi);
      protocolState_t*            protoState = static_cast<protocolState_t*>(proto->user);
      websock_t*                  ws    = ps->thisPtr;
  
      //char buf[32];

      //printf("i: %i %i\n",reason,reason==LWS_CALLBACK_ADD_POLL_FD);

      
      switch( reason )
      {
        case LWS_CALLBACK_PROTOCOL_INIT:
          cwLogInfo("Websocket init");
          break;

        case LWS_CALLBACK_PROTOCOL_DESTROY:
          cwLogInfo("Websocket destroy");
          break;

        case LWS_CALLBACK_ESTABLISHED:
          cwLogInfo("Websocket session:%i opened",ws->_nextSessionId);

          sess->id                = ws->_nextSessionId++;
          sess->protocolId        = proto->id;
          protoState->sessionN   += 1;
          ws->_connSessionN += 1;
      
          if( ws->_cbFunc != nullptr)
            ws->_cbFunc(ws->_cbArg, proto->id, sess->id, kConnectTId, nullptr, 0);
      
          //if (lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_GET_URI) > 0)
          //  printf("conn:%p %s\n",user,buf);
          break;

        case LWS_CALLBACK_CLOSED:
          cwLogInfo("Websocket connection closed.");
      
          ws->_connSessionN -= 1;
          
          cwAssert( protoState->sessionN > 0 );

          protoState->sessionN   -= 1;

          if( ws->_cbFunc != nullptr)
            ws->_cbFunc(ws->_cbArg,proto->id,sess->id,kDisconnectTId,nullptr,0);
      
          break;

        case LWS_CALLBACK_SERVER_WRITEABLE:
          {
            msg_t* m = protoState->endMsg;

            // for each possible outgoing msg in this protocol state record
            while( m != nullptr )
            {
              bool msg_written_fl = false;
              
              // if this msg has not already been sent to this session
              if( m->msgId >= sess->nextMsgId )
              {

                // if the msg sessiond id is not valid or matches this session id ...
                if( m->sessionId == kInvalidId || m->sessionId == sess->id )
                {
                  // ... then send the msg to  this session
                  
                  // Note: msgByteN should not include LWS_PRE
                  int lws_result = lws_write(wsi, m->msg + LWS_PRE , m->msgByteN, LWS_WRITE_TEXT);
            
                  // if the write failed
                  if(lws_result < (int)m->msgByteN)
                  {
                    cwLogError(kWriteFailRC,"Websocket error: %d on write", lws_result);
                    return -1;
                  }

                  msg_written_fl = true;
                }
              
                // at this point the write  succeeded or this session was skipped
                sess->nextMsgId  = m->msgId + 1;

                // incr the msg session count - once m->sessionN >= protoState->sessionN this msg will be deleted in _cleanProtocoStateList()
                m->sessionN     += 1;  

                // If a record was actually sent then we are done since only one
                // call to lws_write() per LWS_CALLBACK_SERVER_WRITEABLE callback
                // is permitted
                if(msg_written_fl)
                  break;   
              }
          

              m = m->link;
            }

          }
          break;
      
        case LWS_CALLBACK_RECEIVE:
          //printf("recv: sess:%i proto:%s : %p : len:%li\n",sess->id,proto->name,ws->_cbFunc,len);
        
          if( ws->_cbFunc != nullptr && len>0)
          {
            ws->_cbFunc(ws->_cbArg,proto->id,sess->id,kMessageTId,in,len);
            ws->_recvMsgCnt += 1;
            ws->_recvMaxByteN = std::max(ws->_recvMaxByteN,(unsigned)len);
          }

          break;

        default:
          break;      
      
      }

      return 0;
    }

    struct lws_protocols* _idToProtocol( websock_t* p, unsigned protocolId )
    {
      for(unsigned i=0; i<p->_protocolN; ++i)
        if( p->_protocolA[i].id == protocolId )
          return p->_protocolA + i;
    
      cwAssert(0);
      return nullptr;
    }

    // Remove message from the send queue (p->_qH), and the protocol msg list,
    //  which have already been sent to all relevant sessions.
    rc_t _cleanProtocolStateList( websock_t* p, protocolState_t* ps )
    {
      rc_t rc  = kOkRC;
      nbmpscq::blob_t b = get(p->_qH);

      msg_t* oldest_msg = (msg_t*)b.blob;

      // if the last protocol end msg has been sent to all sessions ...
      while( oldest_msg != nullptr && ps->endMsg != nullptr && ps->endMsg->sessionN >= ps->sessionN )
      {
        // ... and this msg is also the oldest msg in the send queue
        if( oldest_msg->msgId == ps->endMsg->msgId )
        {
          ps->endMsg = ps->endMsg->link;
          b = advance(p->_qH);  // ... then pop the msg off the send queue
          oldest_msg = (msg_t*)b.blob;
        }
        else // ... otherwise the no further progress can be made
        {
          break;
        }
      }

      if( ps->endMsg == nullptr )
        ps->begMsg = nullptr;

      return rc;
    }
  
    rc_t _destroy( websock_t* p )
    {
      rc_t rc = kOkRC;
      //msg_t* m;

      cwLogInfo("Websock: sent: msgs:%i largest msg:%i - recv: msgs:%i largest msg:%i - LWS_PRE:%i",p->_sendMsgCnt,p->_sendMaxByteN,p->_recvMsgCnt,p->_recvMaxByteN,LWS_PRE);
      cwLogInfo("Exec Time: %i avg ms, %i total ms, %i cnt", p->_execN==0 ? 0 : p->_execSumMs/p->_execN, p->_execSumMs, p->_execN );
      
      if( p->_ctx != nullptr )
      {
        lws_context_destroy(p->_ctx);
        p->_ctx = nullptr;
      }
      
      for(int i=0; p->_protocolA!=nullptr and p->_protocolA[i].callback != nullptr; ++i)
      {
        mem::free(const_cast<char*>(p->_protocolA[i].name));
    
        // TODO: delete any msgs in the protocol state here
        auto ps = static_cast<protocolState_t*>(p->_protocolA[i].user);

        mem::free(ps); 
      }

      if((rc = nbmpscq::destroy(p->_qH)) != kOkRC )
        cwLogError(rc,"Websock queue destroy failed.");
      
      mem::release(p->_protocolA);
      p->_protocolN = 0;

      if( p->_mount != nullptr )
      {
        mem::free(const_cast<char*>(p->_mount->origin));
        mem::free(const_cast<char*>(p->_mount->def));
        mem::release(p->_mount);
      }

      mem::release(p->_pollfdA);
      p->_pollfdMaxN = 0;
      p->_pollfdN = 0;

      p->_nextSessionId = 0;
      p->_connSessionN  = 0;

      mem::release(p);

      return kOkRC;

    }

    static struct lws_pollfd *_custom_poll_find_fd(websock_t *p, lws_sockfd_type fd)
    {
      for (int i = 0; i < p->_pollfdN; i++)
        if (p->_pollfdA[i].fd == fd)
          return &p->_pollfdA[i];

      return nullptr;
    }
    
    // During lws context creation, we get called with the foreign loop pointer
    // that was passed in the creation info struct.  Stash it in our private part
    // of the pt, so we can reference it in the other callbacks subsequently.
    static int _custom_init_private(struct lws_context *cx, void *_loop, int tsi)
    {
      struct pt_eventlibs_custom *priv = (struct pt_eventlibs_custom *)lws_evlib_tsi_to_evlib_pt(cx, tsi);

      // store the loop we are bound to in our private part of the pt

      priv->ws = (websock_t *)_loop;

      return 0;
    }

    static int _custom_sock_accept(struct lws *wsi)
    {
      struct pt_eventlibs_custom *priv   = (struct pt_eventlibs_custom *)lws_evlib_wsi_to_evlib_pt(wsi);
      websock_t                  *p      = priv->ws;
      lws_sockfd_type             fd     = lws_get_socket_fd(wsi);
      int                         events = POLLIN;      
      struct lws_pollfd          *pfd;

      lwsl_info("%s: ADD fd %d, ev %d\n", __func__, fd, events);

      if((pfd = _custom_poll_find_fd(p, fd)) != nullptr )
      {
        lwsl_err("%s: ADD fd %d already in ext table\n", __func__, fd);
        return 1;
      }

      if (p->_pollfdN == p->_pollfdMaxN /*LWS_ARRAY_SIZE(cpcx->pollfds)*/ )
      {
        lwsl_err("%s: no room left\n", __func__);
        return 1;
      }

      pfd = &p->_pollfdA[p->_pollfdN++];
      pfd->fd = fd;
      pfd->events = (short)events;
      pfd->revents = 0;

      return 0;
      //return custom_poll_add_fd(priv->ws, lws_get_socket_fd(wsi), POLLIN);
    }

    static void _custom_io(struct lws *wsi, unsigned int flags)
    {
      struct pt_eventlibs_custom *priv          = (struct pt_eventlibs_custom *)lws_evlib_wsi_to_evlib_pt(wsi);
      websock_t                  *p             = priv->ws;
      int                         events_add    = 0;
      int                         events_remove = 0;
      lws_sockfd_type             fd            = lws_get_socket_fd(wsi);
      struct lws_pollfd          *pfd;

      if (flags & LWS_EV_START)
      {
        if (flags & LWS_EV_WRITE)
          events_add |= POLLOUT;

        if (flags & LWS_EV_READ)
          events_add |= POLLIN;
      }
      else
      {
        if (flags & LWS_EV_WRITE)
          events_remove |= POLLOUT;

        if (flags & LWS_EV_READ)
          events_remove |= POLLIN;
      }

      lwsl_info("%s: CHG fd %d, ev_add %d, ev_rem %d\n", __func__, fd, events_add, events_remove);

      if((pfd = _custom_poll_find_fd(p, fd)) != nullptr )
        pfd->events = (short)((pfd->events & (~events_remove)) | events_add);
    }

    static int _custom_wsi_logical_close(struct lws *wsi)
    {
       struct pt_eventlibs_custom *priv = (struct pt_eventlibs_custom *)lws_evlib_wsi_to_evlib_pt(wsi);
      websock_t                   *p = priv->ws;
      lws_sockfd_type              fd   = lws_get_socket_fd(wsi);      
      struct lws_pollfd           *pfd;

      lwsl_info("%s: DEL fd %d\n", __func__, fd);

      if((pfd = _custom_poll_find_fd(p, fd)) == nullptr)
      {
        lwsl_err("%s: DEL fd %d missing in ext table\n", __func__, fd);
        return 1;
      }

      if (p->_pollfdN > 1)
        *pfd = p->_pollfdA[p->_pollfdN - 1];

      p->_pollfdN--;

      return 0;
    }


    rc_t _exec( websock_t* p, unsigned timeOutMs, bool* msg_fl_ref=nullptr )
    {
      rc_t rc         = kOkRC;  
      int  adjTimeOut = lws_service_adjust_timeout(p->_ctx, timeOutMs, 0);
      int  sysRC      = 0;

      if( msg_fl_ref != nullptr )
        *msg_fl_ref = false;

      if( p->_pollfdN > 0 )
        sysRC = poll(p->_pollfdA, p->_pollfdN, adjTimeOut);

      // if poll timed-out
      if( sysRC == 0 )
        return rc;

      // if error from poll()
      if (sysRC < 0)
      {
        cwLogSysError(kOpFailRC,sysRC,"Websocket poll failed.");
        goto errLabel;
      }
  
      for(int i = 0; i < p->_pollfdN; i++)
      {
        lws_sockfd_type fd = p->_pollfdA[i].fd;
        int ws_rc;

    
        if (!p->_pollfdA[i].revents)
          continue;

        ws_rc = lws_service_fd(p->_ctx, &p->_pollfdA[i]);

        // if something closed, retry this slot since may have been swapped with end 
        if(ws_rc && p->_pollfdA[i].fd != fd)
          i--;

        // if an error occurred
        if(ws_rc < 0)
        {
          // lws feels something bad happened, but the outer application may not care
          cwLogError(kOpFailRC,"libwebsocket lws_service_fd() failed with error %i.", ws_rc);
          goto errLabel;
        }
    
        if(!ws_rc)
        {
          // check if it is an fd owned by the application 
        }

        if( msg_fl_ref != nullptr )
          *msg_fl_ref = true;

      }
      
    errLabel:
      return rc;
    }

    // Call _exec() repeatedly for at least timeOutMs-5 milliseconds.
    rc_t _timed_exec_0( websock_t* p, unsigned timeOutMs )
    {
      rc_t rc = kOkRC;
      time::spec_t t0 = time::current_time();
      unsigned dMs = 0;
      unsigned adjTimeOutMs;

      do {

        adjTimeOutMs = timeOutMs - dMs;

        if((rc = _exec(p,adjTimeOutMs)) != kOkRC )
          break;
        
        dMs = time::elapsedMs(t0);
        
      }while( dMs+5 < timeOutMs );

      return rc;
    }

    // Call _exec() as long as incoming messages are received.
    rc_t _timed_exec_1( websock_t* p, unsigned timeOutMs )
    {
      rc_t rc = kOkRC;
      bool fl = false;
      
      do {

        fl = false;
        if((rc = _exec(p,5,&fl)) != kOkRC )
          break;
        
      }while( fl );

      return rc;
    }

    // Call _exec() as long as incoming messages are received.
    // or 'timeOutMs' time elapses.
    rc_t _timed_exec_2( websock_t* p, unsigned timeOutMs )
    {
      rc_t         rc  = kOkRC;
      bool         fl  = false;
      time::spec_t t0  = time::current_time();
      unsigned     dMs = 0;
      
      do {

        fl = false;
        if((rc = _exec(p,5,&fl)) != kOkRC )
          break;

        dMs = time::elapsedMs(t0);
        
      }while( fl && (dMs+5 < timeOutMs) );

      return rc;
    }
    
  }
}

cw::rc_t cw::websock::create(
  handle_t&         h,
  cbFunc_t          cbFunc,
  void*             cbArg,
  const char*       physRootDir,
  const char*       dfltHtmlPageFn,
  int               port,
  const protocol_t* protocolArgA,
  unsigned          protocolN,
  unsigned          queueBlkCnt,
  unsigned          queueBlkByteCnt,
  bool              extraLogsFl )
{
  rc_t                             rc;
	struct lws_context_creation_info info;
	void *foreign_loops[1];

  if((rc = destroy(h)) != kOkRC )
    return rc;
  
  websock_t* p = mem::allocZ<websock_t>();
 
  int logs = LLL_USER | LLL_ERR | LLL_WARN;
  if( extraLogsFl )
    logs |= LLL_NOTICE;
  
	lws_set_log_level(logs, nullptr);

  p->_event_loop_ops_custom = {};
  p->_event_loop_ops_custom.name                  = "custom";
  p->_event_loop_ops_custom.init_vhost_listen_wsi = _custom_sock_accept;
  p->_event_loop_ops_custom.init_pt               = _custom_init_private;
  p->_event_loop_ops_custom.wsi_logical_close     = _custom_wsi_logical_close;
  p->_event_loop_ops_custom.sock_accept           = _custom_sock_accept;
  p->_event_loop_ops_custom.io                    = _custom_io;
  p->_event_loop_ops_custom.evlib_size_pt         = sizeof(struct pt_eventlibs_custom);

    
  p->_evlib_custom = {
    .hdr = {
      "custom event loop",
      "lws_evlib_plugin",
      LWS_BUILD_HASH,
      LWS_PLUGIN_API_MAGIC
    },
    
    .ops	= &p->_event_loop_ops_custom
  };
    

  // Allocate one extra record to act as the end-of-list sentinel.
  p->_protocolN = protocolN + 1;
  p->_protocolA = mem::allocZ<struct lws_protocols>(p->_protocolN);
  
  // Setup the websocket internal protocol state array
  for(unsigned i=0; i<protocolN; ++i)
  {
    // Allocate the application protocol state array where this application can keep protocol related info
    auto protocolState = mem::allocZ<protocolState_t>(1);
    
    protocolState->thisPtr = p;
    protocolState->begMsg  = nullptr;
    protocolState->endMsg  = nullptr;
    
    // Setup the interal lws_protocols record 
    struct lws_protocols* pr  = p->_protocolA + i;
    pr->name                  = mem::allocStr(protocolArgA[i].label); 
    pr->id                    = protocolArgA[i].id;
    pr->rx_buffer_size        = protocolArgA[i].rcvBufByteN;
    pr->tx_packet_size        = 0; //protocolArgA[i].xmtBufByteN;
    pr->per_session_data_size = sizeof(session_t);
    pr->callback              = strcmp(pr->name,"http")==0 ? _httpCallback : _internalCallback;
    pr->user                  = protocolState;  // maintain a ptr to the application protocol state
  }

  static const char* slash = {"/"};
  p->_mount = mem::allocZ<struct lws_http_mount>(1);
  p->_mount->mountpoint     = slash;
  p->_mount->mountpoint_len = strlen(slash);
  p->_mount->origin         = filesys::expandPath(physRootDir); // physical directory assoc'd with http "/"
  p->_mount->def            = mem::allocStr(dfltHtmlPageFn);
  p->_mount->origin_protocol= LWSMPRO_FILE;

  memset(&info,0,sizeof(info));
  info.port      = port;
  info.mounts    = p->_mount;
  info.protocols = p->_protocolA;

  info.event_lib_custom = &p->_evlib_custom; // bind lws to our custom event  lib implementation above 
	foreign_loops[0] = p;                  // pass in the custom poll object as the foreign loop object we will bind to
	info.foreign_loops = foreign_loops;

  if((rc = nbmpscq::create(p->_qH,queueBlkCnt,queueBlkByteCnt)) != kOkRC )
  {
    rc = cwLogError(rc,"Websock queue create failed.");
    goto errLabel;
  }
  
  p->_cbFunc = cbFunc;
  p->_cbArg  = cbArg;

  p->_pollfdMaxN = ALLOC_FD_CNT;
  p->_pollfdA    = mem::allocZ<lws_pollfd>(p->_pollfdMaxN);
  p->_pollfdN    = 0;
  for(int i=0; i<p->_pollfdMaxN; ++i)
    p->_pollfdA[i].fd = LWS_SOCK_INVALID;
    
	if((p->_ctx = lws_create_context(&info)) == 0)
  {
    rc =  cwLogError(kObjAllocFailRC,"Unable to create the websocket context.");
    goto errLabel;
  }

 errLabel:
  if( rc != kOkRC )
    _destroy(p);
  else
    h.set(p);
    
  return rc;
}

cw::rc_t cw::websock::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
  if(!h.isValid())
    return rc;

  websock_t* p = _handleToPtr(h);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;
}

cw::rc_t cw::websock::send(handle_t h,  unsigned protocolId, unsigned sessionId, const void* msg, unsigned byteN )
{
  rc_t rc = kOkRC;
  websock_t* p = _handleToPtr(h);

  unsigned dataByteN = sizeof(msg_t) + LWS_PRE + byteN;
  uint8_t mem[ dataByteN ];
  msg_t* m = (msg_t*)mem;
  
  memcpy(mem + sizeof(msg_t) + LWS_PRE,msg,byteN);
  m->protocolId = protocolId;
  m->sessionId  = sessionId;
  m->msg        = nullptr;
  m->msgByteN   = byteN;     // length of msg w/o LWS_PRE
  m->msgId      = kInvalidId;
  m->sessionN   = 0;
  m->link       = nullptr;
    

  // put the outgoing msgs on the queue 
  if((rc = nbmpscq::push(p->_qH,mem,dataByteN)) != kOkRC )
  {
    rc = cwLogError(rc,"Websock queue push failed.");
    goto errLabel;
  }

  p->_sendMsgCnt += 1;
  p->_sendMaxByteN = std::max(p->_sendMaxByteN,byteN);
  
errLabel:
  return rc;
}

cw::rc_t cw::websock::sendV( handle_t h, unsigned protocolId, unsigned sessionId, const char* fmt, va_list vl0 )
{
  rc_t rc = kOkRC;
  va_list vl1;
  va_copy(vl1,vl0);
  
  unsigned bufN = vsnprintf(nullptr,0,fmt,vl0);
  char buf[bufN+1];

  unsigned n = vsnprintf(buf,bufN+1,fmt,vl1);
 
  rc = send(h,protocolId,sessionId,buf,n);
  
  va_end(vl1);
 return rc;
}

cw::rc_t cw::websock::sendF( handle_t h, unsigned protocolId, unsigned sessionId, const char* fmt, ... )
{
  va_list vl;
  va_start(vl,fmt);
  rc_t rc = sendV(h,protocolId,sessionId,fmt,vl);
  va_end(vl);
  return rc;
}

cw::rc_t cw::websock::exec( handle_t h, unsigned timeOutMs )
{
  rc_t       rc = kOkRC;
  websock_t* p  = _handleToPtr(h);

  time::spec_t t0 = time::current_time();

  // service any pending websocket activity - with no timeout
  //_timed_exec_1(p,timeOutMs/2);
  _exec(p,0);

  
  // clean already sent messages from each protocol outgoing msg list
  for(unsigned i=0; i<p->_protocolN-1; ++i)
  {
    protocolState_t* ps = static_cast<protocolState_t*>(p->_protocolA[i].user);
    _cleanProtocolStateList( p, ps );
  }

  nbmpscq::peek_reset(p->_qH);

  // Get the next pending outgoing message.
  while(1)
  {
    nbmpscq::blob_t b = nbmpscq::peek(p->_qH);

    // if the outgoing queue is empty
    if( b.blob == nullptr )
      break;
    
    msg_t* m = (msg_t*)b.blob;

    // if msg data ptr is not null then this msg has been seen in an earlier call to this function.
    if( m->msg != nullptr )
      break;
    
    m->msg = (unsigned char*)(m+1); 

    // Get the protocol record for this msg.
    struct lws_protocols* protocol = _idToProtocol(p,m->protocolId);

    // Get the application protcol state record from the protocol 'user' field 
    protocolState_t* ps = static_cast<protocolState_t*>(protocol->user);

    // remove messages from the protocol message queue which have already been sent
    _cleanProtocolStateList( p, ps );

    // Put the msg in the front of the protocal state list (msg's are removed from the back of the list)
    m->msgId          = ps->nextNewMsgId;  // set the msg id
    m->link           = nullptr;
    ps->nextNewMsgId += 1;

    if( ps->begMsg == nullptr )
    {
      ps->begMsg = m;
      ps->endMsg = m;
    }
    else
    {
      ps->begMsg->link = m;
      ps->begMsg       = m;
    }

    // we want one callback for each session
    for(unsigned i=0; i<ps->sessionN; ++i)
    {
      lws_callback_on_writable_all_protocol(p->_ctx,protocol);

      _exec(p,0);

    }
  }

  // block waiting for incoming messages
  _timed_exec_2(p,timeOutMs);
  
  p->_execSumMs += time::elapsedMs(t0);
  p->_execN += 1;

  
  return rc;
}

void cw::websock::report( handle_t h )
{
  websock_t* p  = _handleToPtr(h);

  printf("Websock: msgs sent:%i recvd:%i que count:%i\n",p->_sendMsgCnt,p->_recvMsgCnt,count(p->_qH));

  // clean already sent messages from each protocol outgoing msg list
  for(unsigned i=0; i<p->_protocolN-1; ++i)
  {
    protocolState_t* ps = static_cast<protocolState_t*>(p->_protocolA[i].user);

    const msg_t* m;
    unsigned     cnt = 0;
    for(m=ps->endMsg; m!=nullptr; m=m->link)
      cnt += 1;

    printf("Protocol:%i %i\n",i,cnt);
  }  
}
