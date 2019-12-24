#ifndef cwSerialPortSrv_H
#define cwSerialPortSrv_H

namespace cw
{
  namespace serialPortSrv
  {
    typedef handle<struct this_str> handle_t;

    rc_t create( handle_t& h, const char* device, unsigned baudRate, unsigned cfgFlags, serialPort::callbackFunc_t cbFunc, void* cbArg, unsigned pollPeriodMs );
    rc_t destroy(handle_t& h );

    serialPort::handle_t  portHandle( handle_t h );
    thread::handle_t threadHandle( handle_t h );

    rc_t start( handle_t h );
    rc_t pause( handle_t h );

    rc_t send( handle_t h, const void* byteA, unsigned byteN );
  }


  rc_t serialPortSrvTest();

  
}

#endif
