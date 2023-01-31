#ifndef cwMidiFile_h
#define cwMidiFile_h

namespace cw
{
  namespace midi
  {

    namespace file
    {

      // MIDI file timing:
      // Messages in the MIDI file are time tagged with a delta offset in 'ticks'
      // from the previous message in the same track.
      // 
      // A 'tick' can be converted to microsends as follows:
      //
      // microsecond per tick = micros per quarter note / ticks per quarter note
      // 
      // MpT = MpQN / TpQN
      // 
      // TpQN is given as a constant in the MIDI file header.
      // MpQN is given as the value of the MIDI file tempo message.
      //
      // See seekUSecs() for an example of converting ticks to milliseconds.
      //
      // Notes:
      // As part of the file reading process, the status byte of note-on messages 
      // with velocity=0 are is changed to a note-off message. See _readChannelMsg().
      

      typedef struct
      {
        uint8_t hr;
        uint8_t min;
        uint8_t sec;
        uint8_t frm;
        uint8_t sfr;
      } smpte_t;

      typedef struct
      {
        uint8_t num;
        uint8_t den;
        uint8_t metro;
        uint8_t th2s;
      } timeSig_t;

      typedef struct
      {
        uint8_t key;
        uint8_t scale;
      } keySig_t;

      struct midiTrackMsg_str;
  
      typedef struct
      {
        uint8_t  ch;
        uint8_t  d0;
        uint8_t  d1;
        unsigned durMicros;       // note duration in microseconds (corrected for tempo changes)
        struct trackMsg_str* end; // note-off or pedal-up message
      } chMsg_t;

      enum
      {
        kDropTrkMsgFl = 0x01
      };

      typedef struct trackMsg_str
      {
        unsigned             flags;   // see k???TrkMsgFl
        unsigned             uid;     // uid's are unique among all msg's in the file
        unsigned             dtick;   // delta ticks between events on this track (ticks between this event and the previous event on this track)
        unsigned long long   atick;   // global (all tracks interleaved) accumulated ticks
        unsigned long long   amicro;  // global (all tracks interleaved) accumulated microseconds adjusted for tempo changes
        uint8_t              status;  // ch msg's have the channel value removed (it is stored in u.chMsgPtr->ch)
        uint8_t              metaId;  //
        unsigned short       trkIdx;  //  
        unsigned             byteCnt; // length of data pointed to by u.voidPtr (or any other pointer in the union)
        struct trackMsg_str* link;    // link to next record in this track

        union
        {
          uint8_t          bVal;
          unsigned         iVal;
          unsigned short   sVal;
          const char*      text;
          const void*      voidPtr;
          const smpte_t*   smptePtr;
          const timeSig_t* timeSigPtr;
          const keySig_t*  keySigPtr;
          const chMsg_t*   chMsgPtr;
          const uint8_t*   sysExPtr;
        } u;
      } trackMsg_t;

      inline bool isNoteOn(const trackMsg_t* m)     { return midi::isChStatus(m->status) ? midi::isNoteOn( m->status,m->u.chMsgPtr->d1) : false; }
      inline bool isNoteOff(const trackMsg_t* m)    { return midi::isChStatus(m->status) ? midi::isNoteOff(m->status,m->u.chMsgPtr->d1) : false; }

      inline bool isPedalUp(const trackMsg_t* m)    { return midi::isChStatus(m->status) ? midi::isPedalUp(    m->status, m->u.chMsgPtr->d0, m->u.chMsgPtr->d1) : false; }
      inline bool isPedalDown(const trackMsg_t* m)  { return midi::isChStatus(m->status) ? midi::isPedalDown(  m->status, m->u.chMsgPtr->d0, m->u.chMsgPtr->d1) : false; }
    
      inline bool isSustainPedalUp(const trackMsg_t* m)     { return midi::isChStatus(m->status) ? midi::isSustainPedalUp(    m->status,m->u.chMsgPtr->d0,m->u.chMsgPtr->d1) : false; }
      inline bool isSustainPedalDown(const trackMsg_t* m)   { return midi::isChStatus(m->status) ? midi::isSustainPedalDown(  m->status,m->u.chMsgPtr->d0,m->u.chMsgPtr->d1) : false; }
      
      inline bool isSostenutoPedalUp(const trackMsg_t* m)   { return midi::isChStatus(m->status) ? midi::isSostenutoPedalUp(  m->status,m->u.chMsgPtr->d0,m->u.chMsgPtr->d1) : false; }
      inline bool isSostenutoPedalDown(const trackMsg_t* m) { return midi::isChStatus(m->status) ? midi::isSostenutoPedalDown(m->status,m->u.chMsgPtr->d0,m->u.chMsgPtr->d1) : false; }
  
      typedef handle<struct file_str> handle_t;

      // Read a MIDI file.
      rc_t open( handle_t& hRef, const char* fn );

      // Create an empty MIDI file object.
      rc_t create( handle_t& hRef, unsigned trkN, unsigned ticksPerQN );

      // Release all resources associated with this MIDI file object
      rc_t close( handle_t& hRef );

      // Write this MIDI file to the specified file.
      rc_t write( handle_t h, const char* fn );

      // Return midi file format id (0,1,2) or kInvalidId if 'h' is invalid.
      unsigned fileType( handle_t h );

      // Returns ticks per quarter note or kInvalidMidiByte if 'h' is
      // invalid or 0 if file uses SMPTE ticks per frame time base.
      unsigned ticksPerQN( handle_t h );

      // The file name used in an earlier call to midiFileOpen() or NULL if this 
      // midi file did not originate from an actual file.
      const char* filename( handle_t h );

      // Returns SMPTE ticks per frame or kInvalidMidiByte if 'h' is
      // invalid or 0 if file uses ticks per quarter note time base.
      uint8_t ticksPerSmpteFrame( handle_t h );

      // Returns SMPTE format or kInvalidMidiByte if 'h' is invalid or 0
      // if file uses ticks per quarter note time base.
      uint8_t smpteFormatId( handle_t h );

      // Return the count of tracks in the file.
      unsigned trackCount( handle_t h );

      // Returns count of records in track 'trackIdx' or kInvalidCnt if 'h' is invalid.
      unsigned trackMsgCount( handle_t h, unsigned trackIdx );

      // Returns base of record chain from track 'trackIdx' or NULL if 'h' is invalid.
      const trackMsg_t* trackMsg( handle_t h, unsigned trackIdx );

      // Returns the total count of records in the midi file and the
      // number in the array returned by cmMidiFileMsgArray(). 
      // Return kInvalidCnt if 'h' is invalid.
      unsigned msgCount( handle_t h );

      // Returns a pointer to the base of an array of pointers to all records
      // in the file sorted in ascending time order. 
      // Returns NULL if 'h' is invalid.
      const trackMsg_t** msgArray( handle_t h );

      // Set the velocity of a note-on/off msg identified by 'uid'.
      rc_t setVelocity( handle_t h, unsigned uid, uint8_t vel );

  
      // Insert a MIDI message relative to the reference msg identified by 'uid'.
      // If dtick is positive/negative then the new msg is inserted after/before the reference msg.  
      rc_t insertMsg( handle_t h, unsigned uid, int dtick, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 );

      //
      // Insert a new trackMsg_t into the MIDI file on the specified track.
      //
      // Only the following fields need be set in 'msg'.
      //   atick    - used to position the msg in the track
      //   status   - this field is always set (Note that channel information must stripped from the status byte and included in the channel msg data)
      //   metaId   - this field is optional depending on the msg type
      //   byteCnt  - used to allocate storage for the data element in 'trackMsg_t.u'
      //   u        - the message data
      //
      rc_t insertTrackMsg(     handle_t h, unsigned trkIdx, const trackMsg_t* msg );
      rc_t insertTrackChMsg(   handle_t h, unsigned trkIdx, unsigned atick, uint8_t status, uint8_t d0, uint8_t d1 );
      rc_t insertTrackTempoMsg( handle_t h, unsigned trkIdx, unsigned atick, unsigned bpm );
  
      // Return a pointer to the first msg at or after 'usecsOffs' or kInvalidIdx if no
      // msg exists after 'usecsOffs'.  Note that 'usecOffs' is an offset from the beginning
      // of the file.
      // On return *'msgUsecsPtr' is set to the actual time of the msg. 
      // (which will be equal to or greater than 'usecsOffs').
      unsigned seekUsecs( handle_t h, unsigned long long usecsOffs, unsigned* msgUsecsPtr, unsigned* newMicrosPerTickPtr );

      double durSecs( handle_t h );

      // Calculate Note Duration
      enum { kWarningsMfFl=0x01, kDropReattacksMfFl=0x02 };
      void calcNoteDurations( handle_t h, unsigned flags );

      // Set the delay prior to the first non-zero msg.
      void setDelay( handle_t h, unsigned ticks );

      void printMsgs( handle_t h, log::handle_t logH );
      void printTrack( handle_t h, unsigned trkIdx, log::handle_t logH );

      typedef struct
      {
        unsigned           uid;
        unsigned long long amicro;
        unsigned           density; 
      } density_t;

      // Generate the note onset density measure for each note in the MIDI file.
      // Delete the returned memory with a call to cmMemFree().
      density_t* noteDensity( handle_t h, unsigned* cntRef );


      // Generate a piano-roll plot description file which can be displayed with cmXScore.m
      rc_t genPlotFile( const char* midiFn, const char* outFn );

      rc_t genSvgFile(const char* midiFn, const char* outSvgFn, const char* cssFn, bool standAloneFl, bool panZoomFl );

      rc_t genCsvFile( const char* midiFn, const char* csvFn, bool printWarningsFl=true );

      // Generate a text file reportusing cmMIdiFilePrintMsgs()
      rc_t report( const char* midiFn, log::handle_t logH );

      void printControlNumbers( const char* midiFileName );

      rc_t test( const object_t* cfg );
      
      
      
    }
  }
}


#endif
