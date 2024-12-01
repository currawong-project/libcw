//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwUiDecls.h"
#include "cwIo.h"
#include "cwIoSocketChat.h"
#include "cwSocketDecls.h"
#include "cwText.h"

namespace cw {
  namespace io {
    namespace sock_chat {

      enum
      {
        kUiDivAppId,
        kUiSendTextAppId,
        kUiRemoteAddrAppId,
        kUiRemotePortAppId,
        kUiSendBtnAppId,
        kUiRecvTextAppId,
        kMaxAppId
      };

      // Application values.
      typedef struct sock_chat_str
      {
        io::handle_t ioH;           // io framework handle
        unsigned     baseAppId;     // minimum app id
        unsigned     sockIdx;       // io socket associated with this application
        char*        sendText;      // current text to send to remote socket
        char*        remoteAddr;    // remote address to which this socket will send values
        unsigned     remotePort;    //     "   port   "    "     "     "    "     "   " 
        char*        recvText;      // last text recv'd from remote socket
        unsigned     recvTextUuId;  // Uuid associated with the 'recv' text control
      } sock_chat_t;

      sock_chat_t* _handleToPtr( handle_t h )
      {
        return handleToPtr<handle_t,sock_chat_t>(h);
      }

      bool _isAppId( sock_chat_t* p, unsigned appId )
      { return appId != kInvalidId && p->baseAppId <= appId && appId < p->baseAppId + kMaxAppId; }

      // Close the application
      rc_t _destroy( sock_chat_t* p )
      {
        rc_t rc = kOkRC;
        mem::release(p->sendText);
        mem::release(p->remoteAddr);
        mem::release(p->recvText);
        mem::release(p);
        return rc;
      }

      // Called when an new UI connects to the engine.
      rc_t _uiInit( sock_chat_t* p, const ui_msg_t& m )
      {
        rc_t     rc         = kOkRC;
        unsigned parentUuId = ui::kRootAppId;
        unsigned chanId = kInvalidId;
        unsigned divUuId;
        unsigned uuid;

        const int sn = 63;
        char s[sn+1];
        snprintf(s,sn,"Chat: %i", io::socketPort(p->ioH, p->sockIdx ));

        
        uiCreateDiv(   p->ioH, divUuId,          parentUuId, nullptr, p->baseAppId + kUiDivAppId,        chanId, "uiPanel",  s );
        uiCreateStr(   p->ioH, uuid,             divUuId,    nullptr, p->baseAppId + kUiSendTextAppId,   chanId, "uiText", "Send" );
        uiCreateStr(   p->ioH, uuid,             divUuId,    nullptr, p->baseAppId + kUiRemoteAddrAppId, chanId, "uiText", "Addr", "127.0.0.1" );
        uiCreateNumb(  p->ioH, uuid,             divUuId,    nullptr, p->baseAppId + kUiRemotePortAppId, chanId, "uiNumb", "Port", 0, 0xffff, 1, 0, 0 );
        uiCreateButton(p->ioH, uuid,             divUuId,    nullptr, p->baseAppId + kUiSendBtnAppId,    chanId, "uiBtn",  "Send" );
        uiCreateStr(   p->ioH, p->recvTextUuId,  divUuId,    nullptr, p->baseAppId + kUiRecvTextAppId,   chanId, "uiText", "Recv" );
        
        return rc;
      }

      // Messages from UI to engine.
      rc_t _uiValue( sock_chat_t* p, const ui_msg_t& m )
      {
        rc_t rc = kOkRC;

        // filter out callbacks not meant for this app        
        if( !_isAppId(p,m.appId) )
          return rc;

        switch( m.appId - p->baseAppId )
        {
          case kUiSendTextAppId:
            if( m.value->tid == ui::kStringTId )
              p->sendText = mem::duplStr(m.value->u.s);
            break;

          case kUiRemoteAddrAppId:
            if( m.value->tid == ui::kStringTId )
              p->remoteAddr = mem::duplStr(m.value->u.s);
            break;

          case kUiRemotePortAppId:
            switch( m.value->tid)
            {
              case ui::kIntTId:
                p->remotePort = m.value->u.i;
                break;
                
              case ui::kUIntTId:
                p->remotePort = m.value->u.u;
                break;
                
              default:
                break;
            }
            break;
            
          case kUiRecvTextAppId:
            if( m.value->tid == ui::kStringTId )
              p->recvText = mem::duplStr(m.value->u.s);              
            break;
            
          case kUiSendBtnAppId:
            if( p->sendText )
              io::socketSend( p->ioH, p->sockIdx, p->sendText, textLength(p->sendText)+1, p->remoteAddr, p->remotePort );
            break;
            
          default:
            break;
        }
        return rc;
      }

      // Request from UI for engine value.
      rc_t _uiEcho( sock_chat_t* p, const ui_msg_t& m )
      {
        rc_t rc = kOkRC;

        // filter out callbacks not meant for this app
        if( !_isAppId(p,m.appId) )
          return rc;

        switch( m.appId-p->baseAppId )
        {
          case kUiSendTextAppId:
            if( p->sendText )
              io::uiSendValue( p->ioH,  m.uuId, p->sendText );
            break;
            
          case kUiRemoteAddrAppId:
            if( p->remoteAddr)
              io::uiSendValue( p->ioH,  m.uuId, p->remoteAddr );
            break;
            
          case kUiRecvTextAppId:
            if( p->recvText )
              io::uiSendValue( p->ioH,  m.uuId, p->recvText );
            break;
            
          case kUiRemotePortAppId:
            io::uiSendValue( p->ioH,  m.uuId, p->remotePort );
            break;

          default:
            break;
        }
        return rc;
      }
      
      rc_t _uiCb( sock_chat_t* p, const ui_msg_t& m )
      {
        rc_t rc = kOkRC;
        switch( m.opId )
        {
          case ui::kConnectOpId:
            //cwLogInfo("IO Test Connect: wsSessId:%i.",m.wsSessId);
            break;
          
          case ui::kDisconnectOpId:
            //cwLogInfo("IO Test Disconnect: wsSessId:%i.",m.wsSessId);          
            break;
          
          case ui::kInitOpId:
            rc = _uiInit(p,m);
            break;

          case ui::kValueOpId:
            rc = _uiValue(p, m );
            break;

          case ui::kEchoOpId:
            rc = _uiEcho( p, m );
            break;

          case ui::kIdleOpId:
            break;
          
          case ui::kInvalidOpId:
            // fall through
          default:
            assert(0);
            break;
        
        }
        return rc;
      }

      rc_t _sockCb( sock_chat_t* p, const socket_msg_t& m )
      {
        rc_t rc = kOkRC;
        switch( m.cbId )
        {
          case sock::kConnectCbId:
            break;
            
          case sock::kReceiveCbId:
            if( m.byteA && p->sockIdx == m.sockIdx )
            {
              p->recvText = mem::duplStr((const char*)m.byteA);
              io::uiSendValue(p->ioH, p->recvTextUuId, (const char*)m.byteA );
            }
            
            break;
            
          case sock::kDisconnectCbId:
            break;
        }
        return rc;
      }
      
    }
  }
}


cw::rc_t cw::io::sock_chat::create(  handle_t& hRef, io::handle_t ioH, const char* socketLabel, unsigned baseAppId )
{
  rc_t rc = kOkRC;

  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  sock_chat_t* p = mem::allocZ<sock_chat_t>();

  p->ioH       = ioH;
  p->sockIdx   = io::socketLabelToIndex( ioH, socketLabel );
  p->baseAppId = baseAppId;

  hRef.set(p);

  return rc;
}

cw::rc_t cw::io::sock_chat::destroy( handle_t& hRef )
{
  rc_t         rc = kOkRC;
  sock_chat_t* p  = nullptr;
  
  if(!hRef.isValid())
    return rc;

  if((p = _handleToPtr(hRef)) == nullptr )
    return rc;

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  hRef.clear();

  return rc;
    
}

cw::rc_t cw::io::sock_chat::exec( handle_t h, const msg_t& m )
{
  rc_t         rc = kOkRC;
  sock_chat_t* p  = _handleToPtr(h);
  
  switch( m.tid )
  {
    case kSerialTId:
      break;
          
    case kMidiTId:
      break;
          
    case kAudioTId:
      break;

    case kAudioMeterTId:
      break;
          
    case kSockTId:
      if( m.u.sock != nullptr )
        rc = _sockCb(p,*m.u.sock);
      break;
          
    case kWebSockTId:
      break;
          
    case kUiTId:
      rc = _uiCb(p,m.u.ui);
      break;

    case kExecTId:
      break;

    default:
      assert(0);
        
  }

  return rc;
}

unsigned cw::io::sock_chat::maxAppId( handle_t h )
{
  sock_chat_t* p  = _handleToPtr(h);
  return p->baseAppId + kMaxAppId;
}

