//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.

namespace cw
{
  namespace midi
  {
    namespace parser
    {
      typedef handle<struct parser_str> handle_t;
      
      // 'cbFunc' and 'cbArg' are optional.  If 'cbFunc' is not supplied in the call to
      // create() it may be supplied later by installCallback().
      // 'bufByteCnt' define is the largest complete system-exclusive message the parser will 
      // by able to transmit. System-exclusive messages larger than this will be broken into 
      // multiple sequential callbacks. 
      rc_t     create(  handle_t& hRef, unsigned devIdx, unsigned portIdx, cbFunc_t cbFunc, void* cbArg, unsigned bufByteCnt );
      rc_t     destroy( handle_t& hRef );
      unsigned errorCount( handle_t h );
      void     parseMidiData(    handle_t h, const time::spec_t* timestamp, const uint8_t* buf, unsigned bufByteCnt );

      // The following two functions are intended to be used togetther.
      // Use midiTriple() to insert pre-parsed msg's to the output buffer,
      // and then use transmit() to send the buffer via the parsers callback function.
      // Set the data bytes to 0xff if they are not used by the message.
      rc_t midiTriple( handle_t h, const time::spec_t* timestamp, uint8_t status, uint8_t d0, uint8_t d1 );
      rc_t transmit(    handle_t h ); 

      // Install/Remove additional callbacks.
      rc_t installCallback( handle_t h, cbFunc_t cbFunc, void* cbDataPtr );
      rc_t removeCallback(  handle_t h, cbFunc_t cbFunc, void* cbDataPtr );

      // Returns true if the parser uses the given callback.
      bool hasCallback(     handle_t h, cbFunc_t cbFunc, void* cbDataPtr );
      
    }
  }
}
