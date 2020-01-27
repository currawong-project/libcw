#ifndef cwMidiPort_H
#define cwMidiPort_H


namespace cw
{
  namespace midi
  {

    //(  { file_desc:"Device independent MIDI port related code." kw:[midi]}

    // Flags used to identify input and output ports on MIDI devices
    enum 
    { 
     kInMpFl  = 0x01, 
     kOutMpFl = 0x02 
    };

    typedef void (*cbFunc_t)( const packet_t* pktArray, unsigned pktCnt );

    //)
    //(  { label:parser file_desc:"MIDI event parser converts raw MIDI events into packet_t messages." kw:[midi]}

    //===============================================================================================
    // MIDI Parser
    //
    
    namespace parser
    {
      typedef handle<struct parser_str> handle_t;
      
      // 'cbFunc' and 'cbDataPtr' are optional.  If 'cbFunc' is not supplied in the call to
      // create() it may be supplied later by installCallback().
      // 'bufByteCnt' defines is the largest complete system-exclusive message the parser will 
      // by able to transmit. System-exclusive messages larger than this will be broken into 
      // multiple sequential callbacks. 
      rc_t     create(  handle_t& hRef, unsigned devIdx, unsigned portIdx, cbFunc_t cbFunc, void* cbArg, unsigned bufByteCnt );
      rc_t     destroy( handle_t& hRef );
      unsigned errorCount( handle_t h );
      void     parseMidiData(    handle_t h, const time::spec_t* timestamp, const byte_t* buf, unsigned bufByteCnt );

      // The following two functions are intended to be used togetther.
      // Use midiTriple() to insert pre-parsed msg's to the output buffer,
      // and then use transmit() to send the buffer via the parsers callback function.
      // Set the data bytes to 0xff if they are not used by the message.
      rc_t midiTriple( handle_t h, const time::spec_t* timestamp, byte_t status, byte_t d0, byte_t d1 );
      rc_t transmit(    handle_t h ); 

      // Install/Remove additional callbacks.
      rc_t installCallback( handle_t h, cbFunc_t cbFunc, void* cbDataPtr );
      rc_t removeCallback(  handle_t h, cbFunc_t cbFunc, void* cbDataPtr );

      // Returns true if the parser uses the given callback.
      bool hasCallback(     handle_t h, cbFunc_t cbFunc, void* cbDataPtr );
      
    }

    //)
    //(  { label:cmMidiPort file_desc:"Device independent MIDI port." kw:[midi]}

    //===============================================================================================
    // MIDI Device Interface
    //
    

    namespace device
    {
      typedef handle< struct device_str> handle_t;
      
      // 'cbFunc' and 'cbDataPtr' are optional (they may be set to NULL).  In this case
      // 'cbFunc' and 'cbDataPtr' may be set in a later call to cmMpInstallCallback().
      rc_t create( handle_t& h, cbFunc_t cbFunc, void* cbDataPtr, unsigned parserBufByteCnt, const char* appNameStr );
      rc_t destroy( handle_t& h);
      bool isInitialized( handle_t h );

      unsigned    count( handle_t h );
      const char* name(        handle_t h, unsigned devIdx );
      unsigned    nameToIndex(handle_t h, const char* deviceName);
      unsigned    portCount(  handle_t h, unsigned devIdx, unsigned flags );
      const char* portName(   handle_t h, unsigned devIdx, unsigned flags, unsigned portIdx );
      unsigned    portNameToIndex( handle_t h, unsigned devIdx, unsigned flags, const char* portName );
      rc_t        send(       handle_t h, unsigned devIdx, unsigned portIdx, byte_t st, byte_t d0, byte_t d1 );
      rc_t        sendData(   handle_t h, unsigned devIdx, unsigned portIdx, const byte_t* dataPtr, unsigned byteCnt );

      // Set devIdx to -1 to assign the callback to all devices.
      // Set portIdx to -1 to assign the callback to all ports on the specified devices.
      // 
      rc_t installCallback( handle_t h, unsigned devIdx, unsigned portIdx, cbFunc_t cbFunc, void* cbDataPtr );
      rc_t removeCallback(  handle_t h, unsigned devIdx, unsigned portIdx, cbFunc_t cbFunc, void* cbDataPtr );
      bool usesCallback(    handle_t h, unsigned devIdx, unsigned portIdx, cbFunc_t cbFunc, void* cbDataPtr );
      
      void report( handle_t h, textBuf::handle_t tbH);
      
      rc_t test();
    }
  }
}


#endif
