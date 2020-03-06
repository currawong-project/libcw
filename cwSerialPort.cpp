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
      unsigned         _userId;
      char*            _deviceStr;
      int              _deviceH;
      unsigned         _baudRate;
      unsigned         _cfgFlags;
      callbackFunc_t   _cbFunc;
      void*            _cbArg;
      struct termios   _ttyAttrs;
      struct pollfd*   _pollfd;
      struct port_str* _link;
    } port_t;

    typedef struct device_str
    {
      unsigned       _recvBufByteN;
      void*          _recvBuf;
      port_t*        _portL;
      unsigned       _pollfdN;
      struct pollfd* _pollfd;      
    } device_t;

    inline device_t* _handleToPtr(handle_t h)
    { return handleToPtr<handle_t,device_t>(h); }

    bool _isPortOpen( port_t* p )
    { return p->_deviceH != -1; }

    // Given a 'userId' return the assoc'd port record.
    port_t* _idToPort( device_t* d, unsigned userId, bool errorFl = true )
    {
      port_t* p = d->_portL;
      
      while( p != nullptr )
      {
        if( userId == p->_userId )
          return p;
        
        p = p->_link;
      }

      if( errorFl )
        cwLogError(kInvalidIdRC,"No port was found with id:%i.",userId);
      
      return nullptr;
    }

    
    rc_t _getAttributes( port_t* p, struct termios& attr ) 
    {
      if( tcgetattr(p->_deviceH, &attr) == -1 )
        return cwLogSysError(kGetAttrFailRC,errno,"Error getting tty attributes from %s.",p->_deviceStr);

      return kOkRC;
    }

    // Called to read the serial device when data is known to be waiting.
    rc_t _receive( device_t* d, port_t* p, unsigned& readN_Ref, void* buf=nullptr, unsigned bufByteN=0 )
    {
      rc_t     rc = kOkRC;
      void*    b  = buf;
      unsigned bN = bufByteN;
      int      n  = 0;

      readN_Ref = 0;
     
      
      if( !_isPortOpen(p) )
        return cwLogWarningRC( kResourceNotAvailableRC, "An attempt was made to read from a closed serial port.");
      
      
      // if a buffer was not given
      if( b ==nullptr || bufByteN == 0 )
      {
        b  = d->_recvBuf;
        bN = d->_recvBufByteN;
      }

      // if attempt to read the port succeeded ...
      if((n =read( p->_deviceH, b, bN )) != -1 )
      {        
        readN_Ref += n;

        if( buf == nullptr || bufByteN == 0 )
          p->_cbFunc( p->_cbArg, p->_userId, b, n );
        
      }
      else
      {
        // ... or failed and it wasn't because the port was empty
        if( errno != EAGAIN)
          rc = cwLogSysError(kReadFailRC,errno,"An attempt to read the serial port '%s' failed.", p->_deviceStr );
      }

      return rc;
    }


    // Block devices waiting for data on a port. If userId is valid then wait for data on a specific port otherwise
    // wait for data on all ports.
    rc_t _poll( device_t* d, unsigned timeOutMs, unsigned& readN_Ref, unsigned userId=kInvalidId, void* buf=nullptr, unsigned bufByteN=0 )
    {
      rc_t rc = kOkRC;
      int sysRC;

      readN_Ref = 0;

      // if there are not ports then there is nothing to do
      if( d->_pollfdN == 0 )
        return rc;

      struct pollfd* pfd  = d->_pollfd;   // first struct pollfd
      unsigned       pfdN = d->_pollfdN;  // count of pollfd's
      port_t*        p    = d->_portL;    // port assoc'd with first pollfd

      // if only one port is to be read ...
      if( userId != kInvalidId )
      {
        // ... then locate it
        if((p    = _idToPort(d,userId)) == nullptr )
          return kInvalidArgRC;
        
        pfd  = p->_pollfd;
        pfdN = 1;
      }

      // A port to poll must exist
      if( p == nullptr )
        return cwLogError(kInvalidArgRC,"The port with id %i could not be found.",userId);
      

      // block waiting for data on one of the ports
      if((sysRC = ::poll(pfd,pfdN,timeOutMs)) == 0)
        rc = kTimeOutRC;
      else
      {
        // ::poll() encountered a system exception
        if( sysRC < 0 )
          return cwLogSysError(kReadFailRC,errno,"Poll failed on serial port.");

        // interate through the ports looking for the ones which have data waiting ...
        for(unsigned i=0; p!=nullptr; p=p->_link,++i)
          if( p->_pollfd->revents & POLLIN )
            // ... then read the data
            if((rc = _receive( d, p, readN_Ref, buf, bufByteN )) != kOkRC )
                return rc;
        
      }
  
      return rc;
      
    }

    rc_t _closePort( device_t* d, port_t* p )
    {
      rc_t rc = kOkRC;

      // if the port is already closed
      if( p->_deviceH != -1 )
      {
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
	
        if( ::close(p->_deviceH ) != 0 )
        {
          rc = cwLogSysError(kCloseFailRC,errno,"Port close failed on serial dvice '%s'.", p->_deviceStr);
          goto errLabel;
        }

      }


      // reset the state of the port record
      mem::release(p->_deviceStr);
      p->_userId         = kInvalidId;
      p->_deviceH        = -1;
      p->_baudRate       = 0;
      p->_cfgFlags       = 0;
      p->_cbFunc         = nullptr;
      p->_cbArg          = nullptr;
      p->_pollfd->events = 0;
      p->_pollfd->fd     = -1;
      

    errLabel:
      return rc;
            
    }

    // Destroy the manager object.
    rc_t _destroy( device_t* d )
    {
      rc_t    rc = kOkRC;
      port_t* p  = d->_portL;
      
      while( p != nullptr )
      {
        port_t* p0 = p->_link;
        
        if((rc = _closePort(d,p)) != kOkRC )
          return rc;

        mem::release(p);
        
        p = p0;
        
      }

      mem::release(d->_recvBuf);
      mem::release(d);
      
      return rc;      
    }
    
    
  }
}


cw::rc_t cw::serialPort::create( handle_t& h, unsigned recvBufByteN )
{
  rc_t rc;
  if((rc = destroy(h)) != kOkRC )
    return rc;

  device_t* d = mem::allocZ<device_t>();

  if( recvBufByteN > 0 )
  {
    d->_recvBuf      = mem::allocZ<uint8_t>( recvBufByteN );
    d->_recvBufByteN = recvBufByteN;
  }
  
  h.set(d);

  return rc;
}

cw::rc_t cw::serialPort::destroy( handle_t& h )
{
  rc_t rc = kOkRC;
  
  if( !h.isValid() )
    return rc;

  device_t* d = _handleToPtr(h);

  if((rc = _destroy(d)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;
}

cw::rc_t cw::serialPort::createPort( handle_t h, unsigned userId, const char* deviceStr, unsigned baudRate, unsigned cfgFlags, callbackFunc_t cbFunc, void* cbArg )
{
  rc_t           rc = kOkRC;
  device_t*      d  = _handleToPtr(h);
  struct termios options;
  port_t*        p;

  // locate the port with the given user id
  if((p = _idToPort(d,userId,false)) != nullptr )
    if((rc = _closePort(d,p)) != kOkRC )
      return rc;

  // if a new port record must be allocated
  if( p == nullptr )
  {
    unsigned portN = 0;
    
    // look for an available port and count the number of existing ports
    for(p=d->_portL; p!=nullptr; p=p->_link,++portN)
      if( !_isPortOpen(p) )
        break;

    if( p == nullptr )
    {
      // allocate and link in the new port desc. record
      p = mem::allocZ<port_t>();
      p->_deviceH = -1;

      // link in the new port as the first port
      p->_link    = d->_portL;
      d->_portL   = p;

      // A new port has been allocated reallocate the pollfd array
      mem::release(d->_pollfd);
      d->_pollfdN = portN + 1;
      d->_pollfd  = mem::allocZ<struct pollfd>(d->_pollfdN);

      // link the port records to the their assoc'd pollfd record in d->_pollfd[]
      port_t* pp = d->_portL;
      for(unsigned i=0; pp!=nullptr; ++i,pp=pp->_link)
      {
        pp->_pollfd         = d->_pollfd + i;
        pp->_pollfd->fd     = pp->_deviceH;
        pp->_pollfd->events = POLLIN;
      }
    } 
  }
  
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

  p->_userId         = userId;
  p->_deviceStr      = cw::mem::allocStr( deviceStr );
  p->_baudRate       = baudRate;
	p->_cfgFlags       = cfgFlags;
  p->_cbFunc         = cbFunc;
  p->_cbArg          = cbArg;
  p->_pollfd->fd     = p->_deviceH;
  p->_pollfd->events = POLLIN;
  
  h.set(d);
  
 errLabel:
  if( rc != kOkRC )
    _closePort(d,p);
				
  return rc;
  
}

cw::rc_t cw::serialPort::destroyPort(handle_t h, unsigned userId )
{
  rc_t rc = kOkRC; 
  
  if( !isopen(h,userId) )
    return rc;

  device_t* d = _handleToPtr(h);
  port_t*   p;

  // find the port to close
  if((p = _idToPort(d,userId)) == nullptr )
    return kInvalidArgRC;

  // Close the selected port
  // Note that closed ports are simply marked as closed (deviceH==-1) but not removed from the list.
  if((rc = _closePort(d,p)) != kOkRC )
    return rc;

  h.clear();
  
  return rc;
}

unsigned cw::serialPort::portCount( handle_t h )
{
  device_t* d = _handleToPtr(h);
  return d->_pollfdN;  
}

unsigned cw::serialPort::portIndexToId( handle_t h, unsigned index )
{
  device_t* d = _handleToPtr(h);
  unsigned i = 0;
  for(port_t* p=d->_portL; p!=nullptr; p=p->_link,++i)
    if( i == index )
      return p->_userId;

  return kInvalidId;
}


bool cw::serialPort::isopen( handle_t h, unsigned userId )
{
  device_t* d = _handleToPtr(h);
  port_t*   p;
  
  if((p = _idToPort(d,userId)) == nullptr )
    return false;

  return _isPortOpen(p);  
}

cw::rc_t cw::serialPort::send( handle_t h, unsigned userId, const void* byteA, unsigned byteN )
{
  rc_t      rc = kOkRC;
  device_t* d  = _handleToPtr(h);
  port_t*   p;

  if( byteN == 0 )
    return rc;
  
  if((p = _idToPort(d,userId)) == nullptr )
    return kInvalidArgRC;
      
        
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


// Non-blocking read
cw::rc_t cw::serialPort::receive_nb( handle_t h, unsigned userId, unsigned& readN_Ref, void* buf, unsigned bufN)
{
  rc_t      rc = kOkRC;
  device_t* d  = _handleToPtr(h);
  port_t*   p;

  if((p = _idToPort(d,userId)) == nullptr )
    return kInvalidArgRC;
  
  return _receive( d, p, readN_Ref, buf,bufN );
  
  return rc;
}

cw::rc_t cw::serialPort::receive( handle_t h, unsigned timeOutMs, unsigned& readN_Ref)
{
  device_t* d = _handleToPtr(h);

  return _poll(d,timeOutMs,readN_Ref);
  
}

cw::rc_t cw::serialPort::receive( handle_t h, unsigned timeOutMs, unsigned& readN_Ref, unsigned userId, void* buf, unsigned bufByteN )
{
  device_t* d  = _handleToPtr(h);
  port_t*   p;
  
  if((p = _idToPort(d,userId)) == nullptr )
    return kInvalidArgRC;
  
  return _poll(d,timeOutMs,readN_Ref,userId,buf,bufByteN);
}
  
  const char* cw::serialPort::device( handle_t h, unsigned userId)
{
  device_t* d = _handleToPtr(h);
  port_t*   p;
  if((p = _idToPort(d,userId)) == NULL)
    return NULL;
  
  return p->_deviceStr;
}
    
unsigned    cw::serialPort::baudRate( handle_t h, unsigned userId )
{
  device_t* d = _handleToPtr(h);
  port_t*   p;
  if((p = _idToPort(d,userId)) == NULL)
    return 0;
  
  return p->_baudRate;
}

unsigned    cw::serialPort::cfgFlags( handle_t h, unsigned userId )
{
  device_t* d = _handleToPtr(h);
  port_t*   p;
  if((p = _idToPort(d,userId)) == NULL)
    return 0;
  
  return p->_cfgFlags;
}

unsigned cw::serialPort::readInBaudRate( handle_t h, unsigned userId  )
{
	struct termios attr;
  device_t* d = _handleToPtr(h);
  port_t*   p;
  if((p = _idToPort(d,userId)) == NULL)
    return 0;
  
	if((_getAttributes(p,attr)) != kOkRC )
		return 0;

	return cfgetispeed(&attr);	
  
}

unsigned cw::serialPort::readOutBaudRate( handle_t h, unsigned userId )
{
	struct termios attr;
  device_t* d = _handleToPtr(h);
  port_t*   p;
  if((p = _idToPort(d,userId)) == NULL)
    return 0;
  
	if((_getAttributes(p,attr)) != kOkRC )
		return 0;
		
	return cfgetospeed(&attr);	
  
}

unsigned cw::serialPort::readCfgFlags( handle_t h, unsigned userId )
{
	struct termios attr;	
	unsigned       result = 0;
  device_t*      d      = _handleToPtr(h);
  port_t*        p;
  
  if((p = _idToPort(d,userId)) == NULL)
    return 0;
  
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
 
