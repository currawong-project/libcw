#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
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
      unsigned char*  msg;        // Msg data array.
      unsigned        msgByteN;   // Count of bytes in msg[].
      unsigned        msgId;      // The msgId assigned when this msg is addded to the protocol state msg queue.
      unsigned        sessionN;   // Count of sessions to which this msg has been sent.
      struct msg_str* link;       // Pointer to next message or nullptr if this is the last msg in the queue.
    } msg_t;

    typedef struct websock_str
    {
      cbFunc_t               _cbFunc;
      void*                  _cbArg;
      struct lws_context*    _ctx           = nullptr; //  
      struct lws_protocols*  _protocolA     = nullptr; // Websocket internal protocol state array
      unsigned               _protocolN     = 0; // Count of protocol records in _protocolA[].
      unsigned               _nextSessionId = 0; // Next session id.
      unsigned               _connSessionN  = 0; // Count of connected sessions.
      struct lws_http_mount* _mount         = nullptr; //
      MpScNbQueue<msg_t>*    _q; // Thread safe, non-blocking, protocol independent msg queue.
      
    } websock_t;

    inline websock_t* _handleToPtr(handle_t h){ return handleToPtr<handle_t,websock_t>(h); }


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

    
    int _internalCallback(struct lws* wsi, enum lws_callback_reasons reason, void* user, void* in, size_t len)
    {

      const struct lws_protocols* p = lws_get_protocol(wsi);

      if( p == nullptr || p->user == nullptr )
        return 0;               // TODO: issue a warning
    
      protocolState_t* ps = static_cast<protocolState_t*>(p->user);

      if( ps == nullptr || ps->thisPtr == nullptr )
        return 0;               // TODO: issue a warning
    
      session_t*                  sess       = static_cast<session_t*>(user);
      const struct lws_protocols* proto      = lws_get_protocol(wsi);
      protocolState_t*            protoState = static_cast<protocolState_t*>(proto->user);
      websock_t*                  thisPtr    = ps->thisPtr;
  
      //char buf[32];
  
      switch( reason )
      {
        case LWS_CALLBACK_PROTOCOL_INIT:
          cwLogInfo("Websocket init");
          break;

        case LWS_CALLBACK_PROTOCOL_DESTROY:
          cwLogInfo("Websocket destroy");
          break;

        case LWS_CALLBACK_ESTABLISHED:
          cwLogInfo("Websocket connection opened\n");

          sess->id                = thisPtr->_nextSessionId++;
          sess->protocolId        = proto->id;
          protoState->sessionN   += 1;
          thisPtr->_connSessionN += 1;
      
          if( thisPtr->_cbFunc != nullptr)
            thisPtr->_cbFunc(thisPtr->_cbArg, proto->id, sess->id, kConnectTId, nullptr, 0);
      
          //if (lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_GET_URI) > 0)
          //  printf("conn:%p %s\n",user,buf);
          break;

        case LWS_CALLBACK_CLOSED:
          cwLogInfo("Websocket connection closed.\n");
      
          thisPtr->_connSessionN -= 1;
          protoState->sessionN   -= 1;
          cwAssert( protoState->sessionN >= 0 );

          if( thisPtr->_cbFunc != nullptr)
            thisPtr->_cbFunc(thisPtr->_cbArg,proto->id,sess->id,kDisconnectTId,nullptr,0);
      
          break;

        case LWS_CALLBACK_SERVER_WRITEABLE:
          {
            //printf("writable: sess:%i proto:%s\n",sess->id,proto->name);

            msg_t* m1 = protoState->endMsg;
            cwAssert(m1 != nullptr);

            // for each possible msg
            while( m1->link != nullptr )
            {
              msg_t* m = m1->link;
          
              //printf("writing: %i %i : %i %i\n",m->msgId,sess->nextMsgId,m->sessionN,protoState->sessionN);

              // if this msg has not already been sent to this session
              if( m->msgId >= sess->nextMsgId )
              {

                // Send the msg to  this session            
                // Note: msgByteN should not include LWS_PRE
                int lws_result = lws_write(wsi, m->msg + LWS_PRE , m->msgByteN, LWS_WRITE_TEXT);
            
                // if the write failed
                if(lws_result < (int)m->msgByteN)
                {
                  cwLogError(kWriteFailRC,"Websocket error: %d on write", lws_result);
                  return -1;
                }
                else            // otherwise the write succeeded
                {
                  sess->nextMsgId  = m->msgId + 1;
                  m->sessionN     += 1;
                }
            
                break;
              }
          

              m1 = m1->link;
            }

          }
          break;
      
        case LWS_CALLBACK_RECEIVE:
          //printf("recv: sess:%i proto:%s : %p : len:%li\n",sess->id,proto->name,thisPtr->_cbFunc,len);
        
          if( thisPtr->_cbFunc != nullptr && len>0)
            thisPtr->_cbFunc(thisPtr->_cbArg,proto->id,sess->id,kMessageTId,in,len);

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
      
          mem::free(m1->msg);
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
    
      if( p->_ctx != nullptr )
      {
        lws_context_destroy(p->_ctx);
        p->_ctx = nullptr;
      }

      if( p->_q != nullptr )
      {
      
        while((m = p->_q->pop()) != nullptr)
        {
          mem::free(m->msg);
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
      
          mem::free(m->msg);
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
    pr->callback              = strcmp(pr->name,"http")==0 ? lws_callback_http_dummy : _internalCallback;
    pr->user                  = protocolState;  // maintain a ptr to the application protocol state
  }

  static const char* slash = {"/"};
  p->_mount = mem::allocZ<struct lws_http_mount>(1);
  p->_mount->mountpoint     = slash;
  p->_mount->mountpoint_len = strlen(slash);
  p->_mount->origin         = mem::allocStr(physRootDir); // physical directory assoc'd with http "/"
  p->_mount->def            = mem::allocStr(dfltHtmlPageFn);
  p->_mount->origin_protocol= LWSMPRO_FILE;
  
  memset(&info,0,sizeof(info));
  info.port      = port;
  info.mounts    = p->_mount;
  info.protocols = p->_protocolA;

  p->_q = new MpScNbQueue<msg_t>();
  p->_cbFunc = cbFunc;
  p->_cbArg  = cbArg;
    
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


cw::rc_t cw::websock::send(handle_t h,  unsigned protocolId, const void* msg, unsigned byteN )
{
  rc_t rc = kOkRC;
  
  msg_t* m = mem::allocZ<msg_t>(1);
  m->msg   = mem::allocZ<unsigned char>(byteN);
  memcpy(m->msg,msg,byteN);
  m->msgByteN   = byteN;
  m->protocolId = protocolId;

  websock_t* p = _handleToPtr(h);
  p->_q->push(m);
  
  return rc;
}

cw::rc_t cw::websock::sendV( handle_t h, unsigned protocolId, const char* fmt, va_list vl0 )
{
  rc_t rc = kOkRC;
  va_list vl1;
  va_copy(vl1,vl0);
  
  unsigned bufN = vsnprintf(NULL,0,fmt,vl0);
  char buf[bufN+1];

  unsigned n = vsnprintf(buf,bufN+1,fmt,vl1);
 
  rc = send(h,protocolId,buf,n);
  
  va_end(vl1);
 return rc;
}

cw::rc_t cw::websock::sendF( handle_t h, unsigned protocolId, const char* fmt, ... )
{
  va_list vl;
  va_start(vl,fmt);
  rc_t rc = sendV(h,protocolId,fmt,vl);
  va_end(vl);
  return rc;
}

cw::rc_t cw::websock::exec( handle_t h, unsigned timeOutMs )
{
  rc_t       rc = kOkRC;
  websock_t* p  = _handleToPtr(h);

  // TODO: The return value of lws_service() is undocumented - look at the source code.
  lws_service(p->_ctx, timeOutMs );

  msg_t* m; 
  
  // Get the next pending message.
  if((m = p->_q->pop()) != nullptr )
  {
    auto protocol = _idToProtocol(p,m->protocolId);

    // Get the application protcol record for this message
    protocolState_t* ps = static_cast<protocolState_t*>(protocol->user);

    // remove messages from the protocol message queue which have already been sent
    _cleanProtocolStateList( ps );


    // add the pre-padding bytes to the msg
    unsigned char* msg = mem::allocZ<unsigned char>(LWS_PRE + m->msgByteN);
    memcpy( msg+LWS_PRE, m->msg, m->msgByteN );

    mem::free(m->msg); // free the original msg buffer
    
    m->msg            = msg;               
    m->msgId          = ps->nextNewMsgId;  // set the msg id
    ps->begMsg->link  = m;                 // put the msg on the front of the outgoing  queue
    ps->begMsg        = m;                 // 
    ps->nextNewMsgId += 1;
    
    
    lws_callback_on_writable_all_protocol(p->_ctx,protocol);
    
  }
  
  
  return rc;
}
