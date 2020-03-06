#ifndef cwSerialPortSrv_H
#define cwSerialPortSrv_H

namespace cw
{
  namespace serialPortSrv
  {
    typedef handle<struct this_str> handle_t;

    rc_t create( handle_t& h, unsigned pollPeriodMs=50, unsigned recvBufByteN=512 );
    rc_t destroy(handle_t& h );

    serialPort::handle_t  serialHandle(   handle_t h );
    thread::handle_t      threadHandle( handle_t h );

    rc_t start( handle_t h );
    rc_t pause( handle_t h );

    rc_t send( handle_t h, unsigned portId, const void* byteA, unsigned byteN );
  }


  rc_t serialPortSrvTest();

  
}

#endif
