#ifndef cwWebSock_H
#define cwWebSock_H

/*

Each Websocket represents multiple datastreams referred to as 'protocols'.
Each protocol may be connected to by multiple remote endpoints referred to as 'sessions'.

When a session connects/disconnects to/from a protocol datastream the Websocket listener is called with
a kConnected

Use the Websocket.send() function to send a message to all sessions connected to a given protocol.

Websocket.send() places messages into the thread-safe,non-blocking WebSocket._q.
Messages are then transferred to a protocolState_t queue inside the exec() function
based on their protocolId. 

These messages are then sent to the remote endpoint on the next LWS_CALLBACK_SERVER_WRITEABLE
message to the internal websockets callback function.

Note that messages are not removed from the protocol message queue immediately after they are
written.  Instead the protocol state 'nextMsgId' is advanced to indicate the next message to
write.  Sent messages are removed from the protocol state queue inside exec() - the same
place they are added. Since exec() is only called from a single thread this eliminates the
need to make the protocol state queue thread-safe.

 */

#include "cwWebSockDecls.h"

namespace cw
{

  namespace websock
  {
    typedef handle<struct websock_str> handle_t;    

    typedef void (*cbFunc_t)( void* cbArg, unsigned protocolId, unsigned sessionId, msgTypeId_t msg_type, const void* msg, unsigned byteN );

    rc_t create(
      handle_t&         h,
      cbFunc_t          cbFunc,
      void*             cbArg,
      const char*       physRootDir,
      const char*       dfltHtmlPageFn,
      int               port,
      const protocol_t* protocolA,
      unsigned          protocolN,
      unsigned          queueBlkCnt,
      unsigned          queueBlkByteCnt );

    rc_t destroy( handle_t& h );

    // Set 'sessionId' to kInvalid 
    rc_t send(  handle_t h, unsigned protocolId, unsigned sessionId, const void* msg, unsigned byteN );
    rc_t sendV( handle_t h, unsigned protocolId, unsigned sessionId, const char* fmt, va_list vl );
    rc_t sendF( handle_t h, unsigned protocolId, unsigned sessionId, const char* fmt, ... );

    // Call periodically from the same thread to send/recv messages.
    rc_t exec( handle_t h, unsigned timeOutMs );

    void report( handle_t h );
    
  }  

}

#endif
