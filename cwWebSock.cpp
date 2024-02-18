#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFileSys.h"
#include "cwWebSock.h"
#include "cwMpScNbQueue.h"

#include <libwebsockets.h>

namespace cw
{
  namespace websock
  {
    
    // Internal outgoing msg structure.
    typedef struct msg_str
    {
      unsigned        protocolId; // Protocol associated with this msg.
      unsigned        sessionId;  // Session Id that this msg should be sent to or kInvalidId if it should be sent to all sessions.
      unsigned char*  msg;        // Msg data array.
      unsigned        msgByteN;   // Count of bytes in msg[].
      unsigned        msgId;      // The msgId assigned when this msg is addded to the protocol state msg queue.
      unsigned        sessionN;   // Count of sessions to which this msg has been sent.
      struct msg_str* link;       // Pointer to next message or nullptr if this is the last msg in the queue.
    } msg_t;

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
      MpScNbQueue<msg_t>*    _q; // Thread safe, non-blocking, protocol independent msg queue.

      lws_pollfd* _pollfdA;     // socket handle array used by poll()
      int         _pollfdMaxN;
      int         _pollfdN;

      unsigned _sendMsgCnt;    // Count of msg's sent 
      unsigned _sendMaxByteN;  // Max size across all sent msg's

      unsigned _recvMsgCnt;    // Count of msg's recv'd
      unsigned _recvMaxByteN;  // Max size across all recv'd msg's
      
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
    
      websock_t*                  ws    = ps->thisPtr;
      
      switch( reason )
      {
        // called when libwebsocket opens a new socket
        case LWS_CALLBACK_ADD_POLL_FD:
          {
            lws_pollargs* a = static_cast<lws_pollargs*>(in);
            int i = 0;

            // find an open slot in the polling array
            for(; i < ws->_pollfdN; ++i )
              if( ws->_pollfdA[i].fd == LWS_SOCK_INVALID )
                break;

            // if an open socket was found
            if( i == ws->_pollfdMaxN )
            {
              cwLogError(kResourceNotAvailableRC,"All websocket poll slots are alreadry in use. Proper socket polling will not occur.");
            }
            else
            {
              // setup the poll array to be notified of incoming (browser->server) messages.
              ws->_pollfdA[ i ].fd     = a->fd;
              ws->_pollfdA[ i ].events = LWS_POLLIN;

              if( i == ws->_pollfdN )
                ws->_pollfdN += 1;

            }
          }
          break;

          // called when libwebsocket closes a socket
        case LWS_CALLBACK_DEL_POLL_FD:
          {
            lws_pollargs* a = static_cast<lws_pollargs*>(in);
            int i = 0;

            // locate the socket that is being closed
            for(; i<ws->_pollfdN; ++i)
            {
              if( ws->_pollfdA[i].fd == a->fd )
              {
                ws->_pollfdA[i].fd = LWS_SOCK_INVALID;
                ws->_pollfdA[ i ].events = 0;
                break;
              }
            }
            // Note that the libwebsock semms to send this mesg twice for every closed socket.
            // This means that the socket has already been removed from  pollfdA[] on the second call.
            // We therefore don't warn when the socket is not found since it
            // will happen on every socket.
          }
          break;
          
        case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
          break;

        default:
          break;
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
          cwLogInfo("Websocket session:%i opened: \n",ws->_nextSessionId);

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
          cwLogInfo("Websocket connection closed.\n");
      
          ws->_connSessionN -= 1;
          protoState->sessionN   -= 1;
          cwAssert( protoState->sessionN >= 0 );

          if( ws->_cbFunc != nullptr)
            ws->_cbFunc(ws->_cbArg,proto->id,sess->id,kDisconnectTId,nullptr,0);
      
          break;

        case LWS_CALLBACK_SERVER_WRITEABLE:
          {
            msg_t* m1 = protoState->endMsg;
            cwAssert(m1 != nullptr);

            // for each possible msg
            while( m1->link != nullptr )
            {
              msg_t* m = m1->link;
          
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
                }
              
                // at this point the write  succeeded or this session was skipped
                sess->nextMsgId  = m->msgId + 1;
                m->sessionN     += 1;
                            
                break;
              }
          

              m1 = m1->link;
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

  
    void _cleanProtocolStateList( protocolState_t* ps )
    {
      msg_t* m0 = nullptr;
      msg_t* m1 = ps->endMsg;

      while( m1->link != nullptr )
      {
        if( m1->link->sessionN >= ps->sessionN )
        {
          if( m0 == nullptr )
            ps->endMsg = m1->link;
          else
            m0->link   = m1->link;

          msg_t* t = m1->link;
      
          //mem::free(m1->msg);
          mem::free(m1);

          m1 = t;

          continue;
        }
    
        m0 = m1;
        m1 = m1->link;
      }
    }
  
  
    rc_t _destroy( websock_t* p )
    {
      msg_t* m;

      cwLogInfo("Websock: sent: msgs:%i largest msg:%i - recv: msgs:%i largest msg:%i - LWS_PRE:%i",p->_sendMsgCnt,p->_sendMaxByteN,p->_recvMsgCnt,p->_recvMaxByteN,LWS_PRE);
    
      if( p->_ctx != nullptr )
      {
        lws_context_destroy(p->_ctx);
        p->_ctx = nullptr;
      }

      if( p->_q != nullptr )
      {
      
        while((m = p->_q->pop()) != nullptr)
        {
          //mem::free(m->msg);
          mem::free(m);
        }
      
        delete p->_q;
      }
    
      for(int i=0; p->_protocolA!=nullptr and p->_protocolA[i].callback != nullptr; ++i)
      {
        mem::free(const_cast<char*>(p->_protocolA[i].name));
    
        // TODO: delete any msgs in the protocol state here
        auto ps = static_cast<protocolState_t*>(p->_protocolA[i].user);
    
        m = ps->endMsg;
        while( m != nullptr )
        {
          msg_t* tmp = m->link;
      
          //mem::free(m->msg);
          mem::free(m);
          m = tmp;
        }
    
        mem::free(ps); 
      }
  
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
  unsigned          protocolN )
{
  rc_t                             rc;
	struct lws_context_creation_info info;

  if((rc = destroy(h)) != kOkRC )
    return rc;

  websock_t* p = mem::allocZ<websock_t>();

  int logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE;
	lws_set_log_level(logs, NULL);

  // Allocate one extra record to act as the end-of-list sentinel.
  p->_protocolN = protocolN + 1;
  p->_protocolA = mem::allocZ<struct lws_protocols>(p->_protocolN);
  
  // Setup the websocket internal protocol state array
  for(unsigned i=0; i<protocolN; ++i)
  {
    // Allocate the application protocol state array where this application can keep protocol related info
    auto protocolState = mem::allocZ<protocolState_t>(1);
    auto dummy         = mem::allocZ<msg_t>(1);
    
    protocolState->thisPtr = p;
    protocolState->begMsg  = dummy;
    protocolState->endMsg  = dummy;
    
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

  p->_q = new MpScNbQueue<msg_t>();
  p->_cbFunc = cbFunc;
  p->_cbArg  = cbArg;

  p->_pollfdMaxN = sysconf(_SC_OPEN_MAX);
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

  uint8_t* mem = mem::allocZ<uint8_t>( sizeof(msg_t) + LWS_PRE + byteN  );
  //msg_t* m = mem::allocZ<msg_t>(1);
  //m->msg   = mem::allocZ<unsigned char>(byteN);

  msg_t* m = (msg_t*)mem;
  m->msg = mem + sizeof(msg_t);
  
  memcpy(m->msg+LWS_PRE,msg,byteN);
  m->msgByteN   = byteN;
  m->protocolId = protocolId;
  m->sessionId = sessionId;

  websock_t* p = _handleToPtr(h);
  p->_q->push(m);

  p->_sendMsgCnt += 1;
  p->_sendMaxByteN = std::max(p->_sendMaxByteN,byteN);
  
  return rc;
}

cw::rc_t cw::websock::sendV( handle_t h, unsigned protocolId, unsigned sessionId, const char* fmt, va_list vl0 )
{
  rc_t rc = kOkRC;
  va_list vl1;
  va_copy(vl1,vl0);
  
  unsigned bufN = vsnprintf(NULL,0,fmt,vl0);
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

  // TODO: implement the external polling version of lws_service_fd().
  // See this: LWS_CALLBACK_ADD_POLL_FD to get the fd of the websocket.
  // As of 11/20 this callback is never made - even when the websock library
  // is explicitely built with -DLWS_WITH_EXTERNAL_POLL=ON
  // Note the libwebsocket/test_apps/test-server.c also is incomplete and does
  // not apparently represent a working version of an externally polled server.
  
  // Also see: https://stackoverflow.com/questions/27192071/libwebsockets-for-c-can-i-use-websocket-file-descriptor-with-select
  int sysRC = 0;

  if( p->_pollfdN > 0 )
  {
    sysRC = poll(p->_pollfdA, p->_pollfdN, timeOutMs );
  }
  
  if( sysRC < 0 )
    return cwLogSysError(kReadFailRC,errno,"Poll failed on socket.");

  if(sysRC)
  {
    for(int i = 0; i < p->_pollfdN; i++)
      if(p->_pollfdA[i].revents)
        lws_service_fd(p->_ctx, p->_pollfdA + i);
  }

  lws_service_tsi(p->_ctx, -1, 0 );


  msg_t* m; 
  
  // Get the next pending message.
  while((m = p->_q->pop()) != nullptr )
  {
    auto protocol = _idToProtocol(p,m->protocolId);

    // Get the application protcol record for this message
    protocolState_t* ps = static_cast<protocolState_t*>(protocol->user);

    // remove messages from the protocol message queue which have already been sent
    _cleanProtocolStateList( ps );


    // add the pre-padding bytes to the msg
    //unsigned char* msg = mem::allocZ<unsigned char>(LWS_PRE + m->msgByteN);
    //memcpy( msg+LWS_PRE, m->msg, m->msgByteN );

    //mem::free(m->msg); // free the original msg buffer
    
    //m->msg            = msg;
    
    m->msgId          = ps->nextNewMsgId;  // set the msg id
    ps->begMsg->link  = m;                 // put the msg on the front of the outgoing  queue
    ps->begMsg        = m;                 // 
    ps->nextNewMsgId += 1;
    
    
    lws_callback_on_writable_all_protocol(p->_ctx,protocol);

    lws_service_tsi(p->_ctx, -1, 0 );

  }
  
  
  return rc;
}
