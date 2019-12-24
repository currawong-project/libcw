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


    typedef handle<struct port_str> handle_t;
    typedef void (*callbackFunc_t)( void* cbArg, const void* byteA, unsigned byteN );

    
    rc_t create( handle_t& h, const char* device, unsigned baudRate, unsigned cfgFlags, callbackFunc_t cbFunc, void* cbArg );
    rc_t destroy(handle_t& h );

    bool isopen( handle_t h);
    
    rc_t send( handle_t h, const void* byteA, unsigned byteN );


    // Make callback to listener with result of read - Non-blocking
    rc_t receive( handle_t h, unsigned& readN_Ref);
  
    // Make callback to listener with result of read - Block for up to timeOutMs.
    rc_t receive( handle_t h, unsigned timeOutMs, unsigned& readN_Ref); 

    // Return result of read in buf[bufByteN] - Non-blocking.
    rc_t receive( handle_t h, void* buf, unsigned bufByteN, unsigned& readN_Ref);
    
    // Return result of read in buf[bufByteN] - Block for up to timeOutMs.
    rc_t receive( handle_t h, void* buf, unsigned bufByteN, unsigned timeOutMs, unsigned& readN_Ref );

    const char* device( handle_t h);
    
    // Get the baud rate and cfgFlags used to initialize the port
    unsigned    baudRate( handle_t h);
    unsigned    cfgFlags( handle_t h);

    // Get the baud rate and cfg flags by reading the device.
    // Note the the returned buad rate is a system id rather than the actual baud rate,
    // however the cfgFlags are converted to the same kXXXFl defined in this class.
    unsigned readInBaudRate( handle_t h );
    unsigned readOutBaudRate( handle_t h);
    unsigned readCfgFlags( handle_t h);
  }
}

#endif
