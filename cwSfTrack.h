#ifndef cwSfTracker_h
#define cwSfTracker_h

namespace cw
{
  namespace sftrack
  {
    typedef struct result_str
    {
      unsigned locIdx;    // index into cmScMatch_t.loc[]
      unsigned scEvtIdx;  // score event index 
      unsigned mni;       // index of the performed MIDI event associated with this score location
      unsigned smpIdx;    // sample time index of performed MIDI event
      unsigned muid;      // MIDI file event msg unique id (See cmMidiTrackMsg_t.uid)
      unsigned pitch;     // performed pitch 
      unsigned vel;       // performed velocity
      unsigned flags;     // smTruePosFl | smFalsePosFl 
    } result_t;


    struct sftrack_str;
    typedef void (*callback_func_t)(  void* arg, result_t* rp );
    
    typedef struct sftrack_str
    {
      callback_func_t      cbFunc;
      void*                cbArg;
      sfmatch::handle_t    matchH;
      unsigned             mn;       // size of midiBuf[] 
      sfmatch::midi_t*     midiBuf;  // midiBuf[mn]

      result_t*            res;      // res[rn]
      unsigned             rn;       // length of res[] (set to 2*score event count)
      unsigned             ri;       // next avail res[] recd.

      double               s_opt;          // 
      unsigned             missCnt;        // current count of consecutive trailing non-matches
      unsigned             ili;            // index into loc[] to start scan following reset
      unsigned             eli;            // index into loc[] of the last positive match. 
      unsigned             mni;            // current count of MIDI events since the last call to cmScMatcherReset()
      unsigned             mbi;            // index of oldest MIDI event in midiBuf[]; stays at 0 when the buffer is full.
      unsigned             begSyncLocIdx;  // start of score window, in mp->loc[], of best match in previous scan
      unsigned             initHopCnt;     // max window hops during the initial (when the MIDI buffer fills for first time) sync scan 
      unsigned             stepCnt;        // count of forward/backward score loc's to examine for a match during cmScMatcherStep().
      unsigned             maxMissCnt;     // max. number of consecutive non-matches during step prior to executing a scan.
      unsigned             scanCnt;        // current count of times a resync-scan was executed during cmScMatcherStep()
 
      bool                 printFl;
    } sftrack_t;

    typedef handle<struct sftrack_str> handle_t;
    
    rc_t create( handle_t&         hRef,
                 sfscore::handle_t scH,      // Score handle.  See cmScore.h.
                 unsigned          scWndN,   // Length of the scores active search area. ** See Notes.
                 unsigned          midiWndN, // Length of the MIDI active note buffer.    ** See Notes.
                 callback_func_t   cbFunc,   // A cmScMatcherCb_t function to be called to notify the recipient of changes in the score matcher status.
                 void*             cbArg );  // User argument to 'cbFunc'.

    // Notes:
    // The cmScMatcher maintains an internal cmScMatch object which is used to attempt to find the
    // best match between the current MIDI active note buffer and the current score search area.
    // 'scWndN' is used to set the cmScMatch 'locN' argument.
    // 'midiWndN' sets the length of the MIDI FIFO which is used to match to the score with
    // each recceived MIDI note.
    // 'midiWndN' must be <= 'scWndN'.

    rc_t destroy( handle_t& hRef );

    // 'scLocIdx' is a score index as used by cmScoreLoc(scH) not into p->mp->loc[].
    rc_t reset( handle_t h, unsigned scLocIdx );

    // Slide a score window 'hopCnt' times, beginning at 'bli' (an
    // index into p->mp->loc[]) looking for the best match to p->midiBuf[].  
    // The score window contain scWndN (p->mp->mcn-1) score locations.
    // Returns the index into p->mp->loc[] of the start of the best
    // match score window. The score associated
    // with this match is stored in s_opt.
    //unsigned   scan( handle_t h, unsigned bli, unsigned hopCnt );

    // Step forward/back by p->stepCnt from p->eli.
    // p->eli must therefore be valid prior to calling this function.
    // If more than p->maxMissCnt consecutive MIDI events are 
    // missed then automatically run cmScAlignScan().
    // Return cmEofRC if the end of the score is encountered.
    // Return cmSubSysFailRC if an internal scan resync. failed.
    //rc_t step( handle_t h );

    // This function calls cmScMatcherScan() and cmScMatcherStep() internally.
    // If 'status' is not kNonMidiMdId then the function returns without changing the
    // state of the object. In other words the matcher only recognizes MIDI note-on messages.
    // If the MIDI note passed by the call results in a successful match then
    // p->eli will be updated to the location in p->mp->loc[] of the latest 
    // match, the MIDI note in p->midiBuf[] associated with this match
    // will be assigned a valid locIdx and scLocIdx values, and *scLocIdxPtr
    // will be set with the matched scLocIdx of the match.
    // If this call does not result in a successful match *scLocIdxPtr is set
    // to cmInvalidIdx.
    // 'muid' is the unique id associated with this MIDI event under the circumstances
    // that the event came from a MIDI file.  See cmMidiFile.h cmMidiTrackMsg_t.uid.
    // Return:
    // cmOkRC  - Continue processing MIDI events.
    // cmEofRC - The end of the score was encountered.
    // cmInvalidArgRC - scan failed or the object was in an invalid state to attempt a match.
    // cmSubSysFailRC - a scan resync failed in cmScMatcherStep().
    rc_t exec( handle_t h,
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
