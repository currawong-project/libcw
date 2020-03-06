#ifndef cwSerialPort_H
#define cwSerialPort_H

namespace cw
{
  namespace serialPort
  {
    enum
    {
     kDataBits5Fl 	= 0x0001,
     kDataBits6Fl 	= 0x0002,
     kDataBits7Fl 	= 0x0004,
     kDataBits8Fl 	= 0x0008,
     kDataBitsMask	= 0x000f,
   
     k1StopBitFl		= 0x0010,
     k2StopBitFl 	  = 0x0020,
   
     kEvenParityFl	= 0x0040,
     kOddParityFl	  = 0x0080,
     kNoParityFl		= 0x0000,
     /*
       kCTS_OutFlowCtlFl	= 0x0100,
       kRTS_InFlowCtlFl	= 0x0200,
       kDTR_InFlowCtlFl	= 0x0400,
       kDSR_OutFlowCtlFl	= 0x0800,
       kDCD_OutFlowCtlFl	= 0x1000
     */

     kDefaultCfgFlags = kDataBits8Fl | k1StopBitFl | kNoParityFl
    };


    typedef handle<struct device_str> handle_t;
    typedef void (*callbackFunc_t)( void* cbArg, unsigned userId, const void* byteA, unsigned byteN );

    rc_t create(  handle_t& h, unsigned recvBufByteN=512 );
    rc_t destroy( handle_t& h );
    
    rc_t createPort(  handle_t h, unsigned userId, const char* device, unsigned baudRate, unsigned cfgFlags, callbackFunc_t cbFunc, void* cbArg );
    rc_t destroyPort( handle_t h, unsigned userId );

    unsigned portCount( handle_t h );
    unsigned portIndexToId( handle_t h, unsigned index );

    bool isopen( handle_t h, unsigned userId );
    
    rc_t send( handle_t h, unsigned userId, const void* byteA, unsigned byteN );


    // Non-blocking - Receive data from a specific port if data is available.
    // If buf==nullptr then use the ports callback function to deliver the received data,
    // otherwise return the data in buf[bufByteN].
    rc_t receive_nb( handle_t h, unsigned userId, unsigned& readN_Ref, void* buf=nullptr, unsigned bufByteN=0);
  
    
    // Blocking - Wait up to timeOutMs milliseconds for data to be available on any of the ports.
    // Deliver received data via the port callback function.
    // readN_Ref returns the total count of bytes read across all ports.
    rc_t receive( handle_t h, unsigned timeOutMs, unsigned& readN_Ref); 

    // Blocking - Wait up to timeOutMs milliseconds for data to be available on a specific port. Return the received data in buf[bufByteN].
    rc_t receive( handle_t h, unsigned timeOutMs, unsigned& readN_Ref, unsigned userId, void* buf, unsigned bufByteN );

    const char* device( handle_t h, unsigned userId );
    
    // Get the baud rate and cfgFlags used to initialize the port
    unsigned    baudRate( handle_t h, unsigned userId );
    unsigned    cfgFlags( handle_t h, unsigned userId );

    // Get the baud rate and cfg flags by reading the device.
    // Note the the returned buad rate is a system id rather than the actual baud rate,
    // however the cfgFlags are converted to the same kXXXFl defined in this class.
    unsigned readInBaudRate(  handle_t h, unsigned userId );
    unsigned readOutBaudRate( handle_t h, unsigned userId );
    unsigned readCfgFlags(    handle_t h, unsigned userId );
  }
}

#endif
