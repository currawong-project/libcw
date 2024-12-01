//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwIoSocketChat_h
#define cwIoSocketChat_h

namespace cw
{
  namespace io
  {
    namespace sock_chat
    {
      typedef handle<struct sock_chat_str> handle_t;

      rc_t create(  handle_t& hRef, io::handle_t ioH, const char* socketLabel, unsigned baseAppId );      
      rc_t destroy( handle_t& hRef );

      rc_t exec( handle_t h, const msg_t& msg );

      unsigned maxAppId( handle_t h );
    }
  }
}


#endif
