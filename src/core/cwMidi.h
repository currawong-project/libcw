//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwMidi_h
#define cwMidi_h

namespace cw
{
  namespace midi
  {
    const uint8_t kMidiChCnt           = 16;
    const uint8_t kInvalidMidiByte     = 128;
    const uint8_t kMidiNoteCnt         = kInvalidMidiByte;
    const uint8_t kMidiCtlCnt          = kInvalidMidiByte;
    const uint8_t kMidiVelCnt          = kInvalidMidiByte;
    const uint8_t kMidiPgmCnt          = kInvalidMidiByte;
    const uint8_t kInvalidMidiPitch    = kInvalidMidiByte;
    const uint8_t kInvalidMidiVelocity = kInvalidMidiByte;
    const uint8_t kInvalidMidiCtl      = kInvalidMidiByte;
    const uint8_t kInvalidMidiPgm      = kInvalidMidiByte;
    const uint8_t kMidiSciPitchCharCnt = 5;  // A#-1


    const unsigned kInvalidStatusMdId = 0x00;
    
    const unsigned kNoteOffMdId       = 0x80;
    const unsigned kNoteOnMdId        = 0x90;
    const unsigned kPolyPresMdId      = 0xa0;
    const unsigned kCtlMdId           = 0xb0;
    const unsigned kPgmMdId           = 0xc0;
    const unsigned kChPresMdId        = 0xd0;
    const unsigned kPbendMdId         = 0xe0;
    const unsigned kSysExMdId         = 0xf0;

    const unsigned kSysComMtcMdId     = 0xf1;
    const unsigned kSysComSppMdId     = 0xf2;
    const unsigned kSysComSelMdId     = 0xf3;
    const unsigned kSysComUndef0MdId  = 0xf4;
    const unsigned kSysComUndef1MdId  = 0xf5;
    const unsigned kSysComTuneMdId    = 0xf6;
    const unsigned kSysComEoxMdId     = 0xf7;

    const unsigned kSysRtClockMdId  = 0xf8;
    const unsigned kSysRtUndef0MdId = 0xf9;
    const unsigned kSysRtStartMdId  = 0xfa;
    const unsigned kSysRtContMdId   = 0xfb;
    const unsigned kSysRtStopMdId   = 0xfc;
    const unsigned kSysRtUndef1MdId = 0xfd;
    const unsigned kSysRtSenseMdId  = 0xfe;
    const unsigned kSysRtResetMdId  = 0xff;
    const unsigned kMetaStId        = 0xff;

    const unsigned kSeqNumbMdId     = 0x00;
    const unsigned kTextMdId        = 0x01;
    const unsigned kCopyMdId        = 0x02;
    const unsigned kTrkNameMdId     = 0x03;
    const unsigned kInstrNameMdId   = 0x04;
    const unsigned kLyricsMdId      = 0x05;
    const unsigned kMarkerMdId      = 0x06;
    const unsigned kCuePointMdId    = 0x07;
    const unsigned kMidiChMdId      = 0x20;
    const unsigned kMidiPortMdId    = 0x21;
    const unsigned kEndOfTrkMdId    = 0x2f;
    const unsigned kTempoMdId       = 0x51;
    const unsigned kSmpteMdId       = 0x54;
    const unsigned kTimeSigMdId     = 0x58;
    const unsigned kKeySigMdId      = 0x59;
    const unsigned kSeqSpecMdId     = 0x7f;
    const unsigned kInvalidMetaMdId = 0x80;

    const unsigned kSustainCtlMdId    = 0x40;
    const unsigned kPortamentoCtlMdId = 0x41;
    const unsigned kSostenutoCtlMdId  = 0x42;
    const unsigned kSoftPedalCtlMdId  = 0x43;
    const unsigned kLegatoCtlMdId     = 0x44;

  
    const uint8_t kResetAllCtlsMdId = 121;
    const uint8_t kAllNotesOffMdId  = 123;

    
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

    template< typename T> bool isAllNotesOff(   T s, T d0 )      { return isCtlStatus(s) && (d0)==kAllNotesOffMdId; }
    template< typename T> bool isResetAllCtls(  T s, T d0 )      { return isCtlStatus(s) && (d0)==kResetAllCtlsMdId; }
    

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

    //=================================================================
    // Pitch conversion
    unsigned hzToMidi( double hz );
    float    midiToHz( unsigned midi );
    
    


  }
}




#endif
