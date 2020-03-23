#ifndef cwWebSockDecls_H
#define cwWebSockDecls_H

namespace cw
{
  namespace websock
  {
    typedef struct protocol_str
    {
      const char* label;       // unique label identifying this protocol
      unsigned    id;          // unique id identifying this protocol
      unsigned    rcvBufByteN; // larger rcv'd packages will be broken into multiple parts
      unsigned    xmtBufByteN; // larger xmt'd packages are broken into multiple parts
    } protocol_t;

    typedef enum
    {
     kConnectTId,
     kDisconnectTId,
     kMessageTId
    } msgTypeId_t;

  }
}

#endif
