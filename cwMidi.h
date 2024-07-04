#ifndef cwMidi_h
#define cwMidi_h

namespace cw
{
  namespace midi
  {
    enum
    {
     kMidiChCnt           = 16,
     kInvalidMidiByte     = 128,
     kMidiNoteCnt         = kInvalidMidiByte,
     kMidiCtlCnt          = kInvalidMidiByte,
     kMidiVelCnt          = kInvalidMidiByte,
     kMidiPgmCnt          = kInvalidMidiByte,
     kInvalidMidiPitch    = kInvalidMidiByte,
     kInvalidMidiVelocity = kInvalidMidiByte,
     kInvalidMidiCtl      = kInvalidMidiByte,
     kInvalidMidiPgm      = kInvalidMidiByte,
     kMidiSciPitchCharCnt = 5  // A#-1
    };


    // MIDI status bytes
    enum
    {
     kInvalidStatusMdId = 0x00,
     kNoteOffMdId       = 0x80,
     kNoteOnMdId        = 0x90,
     kPolyPresMdId      = 0xa0,
     kCtlMdId           = 0xb0,
     kPgmMdId           = 0xc0,
     kChPresMdId        = 0xd0,
     kPbendMdId         = 0xe0,
     kSysExMdId         = 0xf0,

     kSysComMtcMdId     = 0xf1,
     kSysComSppMdId     = 0xf2,
     kSysComSelMdId     = 0xf3,
     kSysComUndef0MdId  = 0xf4,
     kSysComUndef1MdId  = 0xf5,
     kSysComTuneMdId    = 0xf6,
     kSysComEoxMdId     = 0xf7,

     kSysRtClockMdId  = 0xf8,
     kSysRtUndef0MdId = 0xf9,
     kSysRtStartMdId  = 0xfa,
     kSysRtContMdId   = 0xfb,
     kSysRtStopMdId   = 0xfc,
     kSysRtUndef1MdId = 0xfd,
     kSysRtSenseMdId  = 0xfe,
     kSysRtResetMdId  = 0xff,
     kMetaStId        = 0xff,

     kSeqNumbMdId     = 0x00,
     kTextMdId        = 0x01,
     kCopyMdId        = 0x02,
     kTrkNameMdId     = 0x03,
     kInstrNameMdId   = 0x04,
     kLyricsMdId      = 0x05,
     kMarkerMdId      = 0x06,
     kCuePointMdId    = 0x07,
     kMidiChMdId      = 0x20,
     kMidiPortMdId    = 0x21,
     kEndOfTrkMdId    = 0x2f,
     kTempoMdId       = 0x51,
     kSmpteMdId       = 0x54,
     kTimeSigMdId     = 0x58,
     kKeySigMdId      = 0x59,
     kSeqSpecMdId     = 0x7f,
     kInvalidMetaMdId = 0x80,

     kSustainCtlMdId    = 0x40,
     kPortamentoCtlMdId = 0x41,
     kSostenutoCtlMdId  = 0x42,
     kSoftPedalCtlMdId  = 0x43,
     kLegatoCtlMdId     = 0x44
  
    };


    
    //===============================================================================================
    // Utility Functions
    //
    template< typename T> T removeCh(T s) { return (s) & 0xf0; };

    template< typename T> bool isStatus( T s )   { return  (kNoteOffMdId <= removeCh(s) /*&& ((unsigned)(s)) <= kSysRtResetMdId*/ ); }
    template< typename T> bool isChStatus( T s ) { return  (kNoteOffMdId <= removeCh(s) && removeCh(s) <  kSysExMdId); }

    template< typename T> bool isCtlStatus( T s ) { return  removeCh(s) == kCtlMdId; }
    
    template< typename T> bool isNoteOnStatus( T s )        { return ( kNoteOnMdId <= removeCh(s) && removeCh(s) <= (kNoteOnMdId + kMidiChCnt) ); }
    template< typename T> bool isNoteOn( T s, T d1 )  { return ( isNoteOnStatus(removeCh(s)) && (d1)!=0) ; }
    template< typename T> bool isNoteOff( T s, T d1 ) { return ( (isNoteOnStatus(removeCh(s)) && (d1)==0) || (kNoteOffMdId <= removeCh(s) && removeCh(s) <= (kNoteOffMdId + kMidiChCnt)) ); }
    template< typename T> bool isCtl( T s )           { return ( kCtlMdId <= removeCh(s) && removeCh(s) <= (kCtlMdId + kMidiChCnt) ); }

    template< typename T> bool isPedal(     T s, T d0 )        { return isCtlStatus(s) && kSustainCtlMdId <= (d0) && (d0) <= kLegatoCtlMdId; }
    template< typename T> bool isPedalDown( T d1 )             { return ( (d1)>=64 ); }
    template< typename T> bool isPedalUp(   T d1 )             { return ( !isPedalDown(d1)  ); }
    template< typename T> bool isPedalDown( T s, T d0, T d1 )  { return ( isPedal(s,d0) && isPedalDown(d1) ); }
    template< typename T> bool isPedalUp(   T s, T d0, T d1 )  { return ( isPedal(s,d0) && isPedalUp(d1)  ); }
    
    template< typename T> bool isSustainPedal(     T s, T d0 )      { return isCtlStatus(s) && (d0)==kSustainCtlMdId; }
    template< typename T> bool isSustainPedalDown( T s, T d0, T d1) { return ( isSustainPedal(s,d0) && isPedalDown(d1) ); }
    template< typename T> bool isSustainPedalUp(   T s, T d0, T d1) { return ( isSustainPedal(s,d0) && isPedalUp(d1) ); }
  
    template< typename T> bool isSostenutoPedal(     T s, T d0 )      { return isCtlStatus(s) && (d0)==kSostenutoCtlMdId; }
    template< typename T> bool isSostenutoPedalDown( T s, T d0, T d1) { return ( isSostenutoPedal(s,d0) && isPedalDown(d1) ); }
    template< typename T> bool isSostenutoPedalUp(   T s, T d0, T d1) { return ( isSostenutoPedal(s,d0) && isPedalUp(d1) ); }

    template< typename T> bool isSoftPedal(     T s, T d0 )      { return isCtlStatus(s) && (d0)==kSoftPedalCtlMdId; }
    template< typename T> bool isSoftPedalDown( T s, T d0, T d1) { return ( isSoftPedal(s,d0) && isPedalDown(d1)); }
    template< typename T> bool isSoftPedalUp(   T s, T d0, T d1) { return ( isSoftPedal(s,d0) && isPedalUp(d1)); }

    

    typedef uint8_t byte_t;
    

    
    const char*   statusToLabel(     uint8_t status );
    const char*   metaStatusToLabel( uint8_t metaStatus );
    const char*   pedalLabel(        uint8_t d0 );

    // Returns kInvalidMidiByte if status is not a valid status byte
    uint8_t  statusToByteCount( uint8_t status );

    unsigned      to14Bits( uint8_t d0, uint8_t d1 );
    void          split14Bits( unsigned v, uint8_t& d0Ref, uint8_t& d1Ref );
    int           toPbend(  uint8_t d0, uint8_t d1 );
    void          splitPbend( int v, uint8_t& d0Ref, uint8_t& d1Ref ); 

    //===============================================================================================
    // MIDI Communication data types
    //


    // Notes: If the sys-ex message can be contained in a single msg then
    // then the first msg byte is kSysExMdId and the last is kSysComEoxMdId.
    // If the sys-ex message is broken into multiple pieces then only the
    // first will begin with kSysExMdId and the last will end with kSysComEoxMdId.


    // If label is NULL or labelCharCnt==0 then a pointer to an internal static
    // buffer is returned. If label[] is given the it
    // should have at least 5 (kMidiSciPitchCharCnt) char's (including the terminating zero).
    // If 'pitch' is outside of the range 0-127 then a blank string is returned.
    const char*    midiToSciPitch( uint8_t pitch, char* label=nullptr, unsigned labelCharCnt=0 );

    // Convert a scientific pitch to MIDI pitch.  acc == 1 == sharp, acc == -1 == flat.
    // The pitch character must be in the range 'A' to 'G'. Upper or lower case is valid.
    // Return kInvalidMidiPitch if the arguments are not valid. 
    uint8_t    sciPitchToMidiPitch( char pitch, int acc, int octave );


    // Scientific pitch string: [A-Ga-g][#b][#] where  # may be -1 to 9.
    // Return kInvalidMidiPitch if sciPtichStr does not contain a valid 
    // scientific pitch string. This function will convert C-1 to G9 to 
    // valid MIDI pitch values 0 to 127.  Scientific pitch strings outside
    // of this range will be returned as kInvalidMidiPitch.   
    uint8_t    sciPitchToMidi( const char* sciPitchStr );

    
#define midi_to_hz( midi_pitch ) (13.75 * std::pow(2,(-9.0/12.0))) * std::pow(2.0,(midi_pitch / 12.0))


  }
}




#endif
