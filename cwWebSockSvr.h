//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwWebsockSvr_H
#define cwWebsockSvr_H

namespace cw {

  namespace websockSrv
  {
    typedef handle<struct websockSrv_str> handle_t;

    rc_t create(
      handle_t&                  h, 
      websock::cbFunc_t          cbFunc, // This callback is made from the thread identified by websockSrv::threadHandle().
      void*                      cbArg,   
      const char*                physRootDir,
      const char*                dfltHtmlPageFn,
      int                        port,
      const websock::protocol_t* protocolA,
      unsigned                   protocolN,
      unsigned                   websockTimeOutMs,
      unsigned                   queueBlkCnt,
      unsigned                   queueBlkByteCnt,
      bool                       extraLogsFl );

    rc_t destroy( handle_t& h );

    thread::handle_t  threadHandle( handle_t h );
    websock::handle_t websockHandle( handle_t h );

    // Start or unpause the server.
    rc_t start( handle_t h );

    // Put the server into a pause state.
    rc_t pause( handle_t h );

  }

  rc_t websockSrvTest( const object_t* cfg );
}

#endif
