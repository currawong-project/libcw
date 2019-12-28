#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwSerialPort.h"

#include <poll.h>
#include <termios.h>
#include <unistd.h>    // ::close()
#include <fcntl.h>     // O_RDWR
#include <sys/ioctl.h> // TIOCEXCL


namespace cw
{
  namespace serialPort
  {
    typedef struct port_str
    {
      const char*           _deviceStr;
      int                   _deviceH;
      unsigned              _baudRate;
      unsigned              _cfgFlags;
      callbackFunc_t        _cbFunc;
      void*                 _cbArg;
      struct termios        _ttyAttrs;
      struct pollfd         _pollfd;      
    } port_t;

    inline port_t* _portHandleToPtr(handle_t h){ return handleToPtr<handle_t,port_t>(h); }

    void   _setClosedState( port_t* p )
    {
      if( p->_deviceStr != nullptr )
        cw::mem::free(const_cast<char*>(p->_deviceStr));
     
      p->_deviceH = -1;
      p->_deviceStr = nullptr;
      p->_baudRate  = 0;
      p->_cfgFlags  = 0;
      p->_cbFunc    = nullptr;
      p->_cbArg     = nullptr;
      
    }
    
    rc_t _getAttributes( port_t* p, struct termios& attr ) 
    {
      if( tcgetattr(p->_deviceH, &attr) == -1 )
        return cwLogSysError(kGetAttrFailRC,errno,"Error getting tty attributes from %s.",p->_deviceStr);

      return kOkRC;
    }
    
    rc_t _poll( port_t* p, unsigned timeOutMs )
    {
      rc_t rc = kOkRC;
      int sysRC;

      if((sysRC = ::poll(&p->_pollfd,1,timeOutMs)) == 0)
        rc = kTimeOutRC;
      else
      {
        if( sysRC < 0 )
          rc = cwLogSysError(kReadFailRC,errno,"Poll failed on serial port.");
      }
  
      return rc;
      
    }
    
    rc_t _destroy( port_t* p )
    {
      rc_t rc = kOkRC;
      
      // Block until all written output has been sent from the device.
      // Note that this call is simply passed on to the serial device driver.
      // See tcsendbreak(3) ("man 3 tcsendbreak") for details.
      if (tcdrain(p->_deviceH) == -1)
      {
        rc = cwLogSysError(kFlushFailRC,errno,"Error waiting for serial device '%s' to drain.", p->_deviceStr );
        goto errLabel;
      }

      // It is good practice to reset a serial port back to the state in
      // which you found it. This is why we saved the original termios struct
      // The constant TCSANOW (defined in termios.h) indicates that
      // the change should take effect immediately.

      if (tcsetattr(p->_deviceH, TCSANOW, &p->_ttyAttrs) ==  -1)
      {
        rc = cwLogSysError(kSetAttrFailRC,errno,"Error resetting tty attributes on serial device '%s'.",p->_deviceStr);
        goto errLabel;
      }
	
      if( p->_deviceH != -1 )
      {
        if( ::close(p->_deviceH ) != 0 )
        {
          rc = cwLogSysError(kCloseFailRC,errno,"Port close failed on serial dvice '%s'.", p->_deviceStr);
          goto errLabel;
        }
		
        _setClosedState(p);
      }

      mem::release(p);

    errLabel:
      return rc;      
    }
    
    
  }
}


cw::rc_t cw::serialPort::create( handle_t& h, const char* deviceStr, unsigned baudRate, unsigned cfgFlags, callbackFunc_t cbFunc, void* cbArg )
{
  rc_t           rc = kOkRC;
  struct termios options;

  // if the port is already open then close it
  if((rc = destroy(h)) != kOkRC )
    return rc;

  port_t* p = mem::allocZ<port_t>();

  p->_deviceH = -1;
    
	// open the port		
	if( (p->_deviceH = ::open(deviceStr, O_RDWR | O_NOCTTY | O_NONBLOCK)) == -1 )
	{
		rc = cwLogSysError(kOpenFailRC,errno,"Error opening serial '%s'",cwStringNullGuard(deviceStr));
		goto errLabel;;
	}

  // Note that open() follows POSIX semantics: multiple open() calls to 
  // the same file will succeed unless the TIOCEXCL ioctl is issued.
  // This will prevent additional opens except by root-owned processes.
  // See tty(4) ("man 4 tty") and ioctl(2) ("man 2 ioctl") for details.
  
  if( ioctl(p->_deviceH, TIOCEXCL) == -1 )
  {
    rc = cwLogSysError(kResourceNotAvailableRC,errno,"The serial device '%s' is already in use.", cwStringNullGuard(deviceStr));
    goto errLabel;
  }


  // Now that the device is open, clear the O_NONBLOCK flag so 
  // subsequent I/O will block.
  // See fcntl(2) ("man 2 fcntl") for details.
 	/*
    if (fcntl(_deviceH, F_SETFL, 0) == -1)

    {
    _error("Error clearing O_NONBLOCK %s - %s(%d).", pr.devFilePath.c_str(), strerror(errno), errno);
    goto errLabel;
    }
	*/
	
  // Get the current options and save them so we can restore the 
  // default settings later.
  if (tcgetattr(p->_deviceH, &p->_ttyAttrs) == -1)
  {
    rc = cwLogSysError(kGetAttrFailRC,errno,"Error getting tty attributes from the device '%s'.",deviceStr);
    goto errLabel;
  }


  // The serial port attributes such as timeouts and baud rate are set by 
  // modifying the termios structure and then calling tcsetattr to
  // cause the changes to take effect. Note that the
  // changes will not take effect without the tcsetattr() call.
  // See tcsetattr(4) ("man 4 tcsetattr") for details.
  options = p->_ttyAttrs;


  // Set raw input (non-canonical) mode, with reads blocking until either 
  // a single character has been received or a 100ms timeout expires.
  // See tcsetattr(4) ("man 4 tcsetattr") and termios(4) ("man 4 termios") 
  // for details.
  cfmakeraw(&options);
  options.c_cc[VMIN] = 1;
  options.c_cc[VTIME] = 1;


  // The baud rate, word length, and handshake options can be set as follows:

 
	// set baud rate
  cfsetspeed(&options, baudRate);
  
  options.c_cflag |=  CREAD | CLOCAL; // ignore modem controls

  // set data word size
  cwClrBits(options.c_cflag, CSIZE); // clear the word size bits
  cwEnaBits(options.c_cflag,	CS5,			cwIsFlag(cfgFlags, kDataBits5Fl));
  cwEnaBits(options.c_cflag,	CS6,			cwIsFlag(cfgFlags, kDataBits6Fl));
  cwEnaBits(options.c_cflag,	CS7,			cwIsFlag(cfgFlags, kDataBits7Fl));
  cwEnaBits(options.c_cflag,	CS8,			cwIsFlag(cfgFlags, kDataBits8Fl));

  cwClrBits(options.c_cflag, PARENB); // assume no-parity

  // if the odd or even parity flag is set
  if( cwIsFlag( cfgFlags, kEvenParityFl) || cwIsFlag( cfgFlags, kOddParityFl ) )
  {
    cwSetBits(options.c_cflag,	PARENB);
    	
    if( cwIsFlag(cfgFlags, kOddParityFl ) )
      cwSetBits( options.c_cflag,	PARODD);
  }

	// set two stop bits    
  cwEnaBits( options.c_cflag, CSTOPB, cwIsFlag(cfgFlags, k2StopBitFl));
    
    		    
  // set hardware flow control
  //cwEnaBits(options.c_cflag,		CCTS_OFLOW, 	cwIsFlag(cfgFlags, kCTS_OutFlowCtlFl)); 
	//cwEnaBits(options.c_cflag, 	CRTS_IFLOW, 	cwIsFlag(cfgFlags, kRTS_InFlowCtlFl));
	//cwEnaBits(options.c_cflag, 	CDTR_IFLOW, 	cwIsFlag(cfgFlags, kDTR_InFlowCtlFl));
	//cwEnaBits(options.c_cflag, 	CDSR_OFLOW, 	cwIsFlag(cfgFlags, kDSR_OutFlowCtlFl));
	//cwEnaBits(options.c_cflag, 	CCAR_OFLOW, 	cwIsFlag(cfgFlags, kDCD_OutFlowCtlFl));
    
	cwClrBits(options.c_cflag,CRTSCTS); // turn-off hardware flow control

	// 7 bit words, enable even parity, CTS out ctl flow, RTS in ctl flow
	// note: set PARODD and PARENB to enable odd parity)
	//options.c_cflag |= (CS7 | PARENB | CCTS_OFLOW | CRTS_IFLOW );

  // Cause the new options to take effect immediately.
  if (tcsetattr(p->_deviceH, TCSANOW, &options) == -1)
  {

    rc = cwLogSysError(kSetAttrFailRC,errno,"Error setting tty attributes on serial device %.", deviceStr);
    goto errLabel;
  }

  memset(&p->_pollfd,0,sizeof(p->_pollfd));
  p->_pollfd.fd     = p->_deviceH;
  p->_pollfd.events = POLLIN;
  
  p->_deviceStr = cw::mem::allocStr( deviceStr );
  p->_baudRate  = baudRate;
	p->_cfgFlags  = cfgFlags;
  p->_cbFunc    = cbFunc;
  p->_cbArg     = cbArg;

  h.set(p);
  
 errLabel:
  if( rc != kOkRC )
    _destroy(p);
				
  return rc;
  
}

cw::rc_t cw::serialPort::destroy(handle_t& h )
{
  rc_t rc = kOkRC; 
  
  if( !isopen(h) )
    return rc;

  port_t* p = _portHandleToPtr(h);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;
}

bool cw::serialPort::isopen( handle_t h)
{
  if( !h.isValid() )
    return false;
  
  port_t* p = _portHandleToPtr(h);
  return p->_deviceH != -1;
}

cw::rc_t cw::serialPort::send( handle_t h, const void* byteA, unsigned byteN )
{
  rc_t rc = kOkRC;

  port_t* p = _portHandleToPtr(h);
      
  if( !isopen(h)  )
    return cwLogWarningRC( kResourceNotAvailableRC, "An attempt was made to transmit from a closed serial port.");
        
  if( byteN == 0 )
    return rc;
        
  // implement a non blocking write - if less than all the bytes were written then iterate
  unsigned i = 0;
  do
  {
    int n = 0;
    if((n = write( p->_deviceH, ((char*)byteA)+i, byteN-i )) == -1 )
    {
      rc = cwLogSysError(kWriteFailRC,errno,"Write failed on serial port '%s'.", p->_deviceStr );
      break;
    }

    i += n;

      
  }while( i<byteN );

  return rc;
  
}


cw::rc_t cw::serialPort::receive( handle_t h, unsigned& readN_Ref)
{
  rc_t         rc   = kOkRC;
  port_t*        p    = _portHandleToPtr(h);
  const unsigned bufN = 512;
  char           buf[ bufN ];
  
  readN_Ref = 0;

  if((rc = receive(h,buf,bufN,readN_Ref)) == kOkRC )
    if( readN_Ref > 0 && p->_cbFunc != nullptr )
      p->_cbFunc( p->_cbArg, buf, readN_Ref );

  return rc;
  
}
  
cw::rc_t cw::serialPort::receive( handle_t h, unsigned timeOutMs, unsigned& readN_Ref)
{
  rc_t rc;
  port_t* p = _portHandleToPtr(h);

  if((rc = _poll(p,timeOutMs)) == kOkRC )
    rc = receive(h,readN_Ref);
  return rc;
  
}

cw::rc_t cw::serialPort::receive( handle_t h, void* buf, unsigned bufN, unsigned& readN_Ref)
{
 rc_t rc = kOkRC;
  port_t* p = _portHandleToPtr(h);
  
  readN_Ref = 0;
  
  if( !isopen(h)  )
    return cwLogWarningRC( kResourceNotAvailableRC, "An attempt was made to read from a closed serial port.");
  
  int       n    = 0;
  
  // if attempt to read the port succeeded ...
  if((n =read( p->_deviceH, buf, bufN )) != -1 )
    readN_Ref = n;
  else
  {
    // ... or failed and it wasn't because the port was empty
    if( errno != EAGAIN)
      rc = cwLogSysError(kReadFailRC,errno,"An attempt to read the serial port '%s' failed.", p->_deviceStr );
  }
    
  return rc;
}
    
cw::rc_t cw::serialPort::receive( handle_t h, void* buf, unsigned bufByteN, unsigned timeOutMs, unsigned& readN_Ref )
{
  rc_t rc = kOkRC;
  port_t* p = _portHandleToPtr(h);
  if((rc = _poll(p,timeOutMs)) == kOkRC )
    rc = receive(h,buf,bufByteN,readN_Ref);
  
  return rc;  
}

const char* cw::serialPort::device( handle_t h)
{
  port_t* p = _portHandleToPtr(h);
  return p->_deviceStr;
}
    
unsigned    cw::serialPort::baudRate( handle_t h)
{
  port_t* p = _portHandleToPtr(h);
  return p->_baudRate;
}

unsigned    cw::serialPort::cfgFlags( handle_t h)
{
  port_t* p = _portHandleToPtr(h);
  return p->_cfgFlags;
}

unsigned cw::serialPort::readInBaudRate( handle_t h )
{
	struct termios attr;
  port_t* p = _portHandleToPtr(h);
	
	if((_getAttributes(p,attr)) != kOkRC )
		return 0;

	return cfgetispeed(&attr);	
  
}

unsigned cw::serialPort::readOutBaudRate( handle_t h)
{
	struct termios attr;
  port_t* p = _portHandleToPtr(h);
	
	if((_getAttributes(p,attr)) != kOkRC )
		return 0;
		
	return cfgetospeed(&attr);	
  
}

unsigned cw::serialPort::readCfgFlags( handle_t h)
{
	struct termios attr;	
	unsigned result = 0;
  port_t* p = _portHandleToPtr(h);

	if((_getAttributes(p,attr)) == false )
		return 0;

	switch( attr.c_cflag & CSIZE )
	{
		case CS5:
			cwSetBits( result, kDataBits5Fl);
			break;
			
		case CS6:
			cwSetBits( result, kDataBits6Fl );
			break;
			
		case CS7:
			cwSetBits( result, kDataBits7Fl);
			break;
			
		case CS8:
			cwSetBits( result, kDataBits8Fl);
			break;
	}
	
	cwEnaBits( result, k2StopBitFl, cwIsFlag(  attr.c_cflag, CSTOPB ));
	cwEnaBits( result, k1StopBitFl, !cwIsFlag( attr.c_cflag, CSTOPB ));

	if( cwIsFlag( attr.c_cflag, PARENB ) )
	{
		cwEnaBits( result, kOddParityFl, 	cwIsFlag( attr.c_cflag, PARODD ));
		cwEnaBits( result, kEvenParityFl, 	!cwIsFlag( attr.c_cflag, PARODD ));
	}		
	
	return result;
  
}
 
