#ifndef cwSfTracker_h
#define cwSfTracker_h

namespace cw
{
  namespace sftrack
  {
    typedef struct result_str
    {
      unsigned index;     // index of this record into the internal result[] array or kInvalidIdx if it is not in the array.
      unsigned oLocId;    // index into cwSfMatch_t.loc[]
      unsigned scEvtIdx;  // score event index 
      unsigned mni;       // index of the performed MIDI event associated with this score location
      double   sec;       // seconds of the performed MIDI event
      unsigned smpIdx;    // sample time index of performed MIDI event
      unsigned muid;      // MIDI file event msg unique id (See cwMidiFile trackMsg_t.uid)
      unsigned pitch;     // performed pitch 
      unsigned vel;       // performed velocity
      unsigned flags;     // See kSm???Fl
      double   cost;      // match cost
    } result_t;


    struct sftrack_str;
    typedef void (*callback_func_t)(  void* arg, result_t* rp );
    
    typedef handle<struct sftrack_str> handle_t;

    enum {
      // Set this flag to overwrite results in the internal result[] array when
      // the matcher backtracks.  If this flag is set then the result[] array  
      // will have one record for each unique MIDI event (count of calls to exec()).
      // If it is cleared then the result array will have a unique entry for
      // each alignment and re-alignment. In this case a given performed
      // event may have multiple entries in the result[] array if it is
      // re-aligned.  result[] records with identical 'muid' fields indicate
      // that the performed event represented by the 'muid' was re-aligned..
      kBacktrackResultsFl = 0x01,
      kPrintFl            = 0x02
    };
    
    rc_t create( handle_t&         hRef,
                 sfscore::handle_t scH,      // Score handle.  See cwSfScore.h.
                 unsigned          scWndN,   // Length of the scores active search area. ** See Notes.
                 unsigned          midiWndN, // Length of the MIDI active note buffer.    ** See Notes.
                 unsigned          flags,    // See k???Fl
                 callback_func_t   cbFunc,   // A function to be called to notify the recipient of changes in the score matcher status.
                 void*             cbArg );  // User argument to 'cbFunc'.

    // Notes:
    // The cwSfTrack maintains an internal cwSfMatch object which is used to attempt to find the
    // best match between the current MIDI active note buffer and the current score search area.
    // 'scWndN' is used to set the cwSfMatch 'locN' argument.
    // 'midiWndN' sets the length of the MIDI FIFO which is used to match to the score with
    // each recceived MIDI note.
    // 'midiWndN' must be <= 'scWndN'.

    rc_t destroy( handle_t& hRef );

    // Set the starting position of the tracker and clear the internal 'result' array.
    // 'scLocIdx' is a score index as used by cmScoreLoc(scH) not into p->mp->loc[].
    rc_t reset( handle_t h, unsigned scLocIdx );

    // Slide a score window 'hopCnt' times, beginning at 'bli' (an
    // index into p->mp->loc[]) looking for the best match to p->midiBuf[].  
    // The score window contain scWndN (p->mp->mcn-1) score locations.
    // Returns the index into p->mp->loc[] of the start of the best
    // match score window. The score associated
    // with this match is stored in s_opt.
    //unsigned   _scan( handle_t h, unsigned bli, unsigned hopCnt );

    // Step forward/back by p->stepCnt from p->eli.
    // p->eli must therefore be valid prior to calling this function.
    // If more than p->maxMissCnt consecutive MIDI events are 
    // missed then automatically run cmScAlignScan().
    // Return cmEofRC if the end of the score is encountered.
    // Return cmSubSysFailRC if an internal scan resync. failed.
    //rc_t _step( handle_t h );

    // This function calls _scan() and _step() internally.
    //
    // If 'status' is not kNonMidiMdId then the function returns without changing the
    // state of the object. (i.e. the matcher only recognizes MIDI note-on messages.)
    //
    // If the MIDI note passed by the call results in a successful match then
    // p->eli will be updated to the location in p->sfMatch.loc[] of the latest 
    // match, the MIDI note in p->midiBuf[] associated with this match
    // will be assigned a valid locIdx and scLocIdx values, and *scLocIdxPtr
    // will be set with the matched scLocIdx of the match.
    //
    // If this call does not result in a successful match *scLocIdxPtr is set
    // to kInvalidIdx.
    //
    // Calling exec() may result in alignment/re-alignment of both the incoming note
    // and/or previous notes.  (i.e. previous note that did not align may be found
    // to align or previous notes that were found to align may now not align).
    // The exact status of the alignment and re-alignments are held in the
    // internal 'result' array.
    //
    // Every alignment/realignment of an event results in a call to the callback function.
    // This means that multiple callbacks may occur as the result of a single
    // call to exec().
    
    // 'muid' is the unique id associated with this MIDI event under the circumstances
    // that the event came from a MIDI file.  See cwMidiFile.h trackMsg_t.uid.
    //
    // Return:
    //   cmOkRC  - Continue processing MIDI events.
    //   cmEofRC - The end of the score was encountered.
    //   cmInvalidArgRC - scan failed or the object was in an invalid state to attempt a match.
    //   cmSubSysFailRC - a scan resync failed in cmScMatcherStep().
    rc_t exec( handle_t h,
               double   sec,
               unsigned smpIdx,
               unsigned muid,
               unsigned status,
               midi::byte_t d0,
               midi::byte_t d1,
               unsigned* scLocIdxPtr );


    unsigned        result_count( handle_t h );
    const result_t* result_base( handle_t h );
    
    void print( handle_t h );

    rc_t test( const object_t* cfg, sfscore::handle_t scoreH );
    
  }
}


#endif
