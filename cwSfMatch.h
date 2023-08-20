#ifndef cwScoreMatch_h
#define cwScoreMatch_h

namespace cw
{
  namespace sfmatch
  {
    enum 
    { 
      kSmMinIdx, // 
      kSmSubIdx, // 'substitute' - may or may not match
      kSmDelIdx, // 'delete'     - delete a MIDI note 
      kSmInsIdx, // 'insert'     - insert a space in the score 
      kSmCnt 
    };

    enum
    {
      kSmMatchFl    = 0x01,
      kSmTransFl    = 0x02,
      kSmTruePosFl  = 0x04,
      kSmFalsePosFl = 0x08,
      kSmBarFl      = 0x10,
      kSmNoteFl     = 0x20
    };

    // Dynamic Programming (DP) matrix element
    typedef struct
    {
      unsigned v[kSmCnt]; // cost for each operation 
      unsigned flags;     // cmSmMatchFl | cmSmTransFl
      unsigned scEvtIdx; 
    } value_t;

    // List record used to track a path through the DP matrix p->m[,]
    typedef struct path_str
    {
      unsigned                  code;     // kSmXXXIdx
      unsigned                  ri;       // matrix row index
      unsigned                  ci;       // matrix col index
      unsigned                  flags;    // cmSmMatchFl | cmSmTransFl
      unsigned                  locIdx;   // p->loc index or cmInvalidIdx
      unsigned                  scEvtIdx; // scScore event index
      struct path_str* next;     //
    } path_t;

    typedef struct event_str
    {
      unsigned pitch;     // 
      unsigned scEvtIdx;  // scScore event index
    } event_t;

    // Score location record. 
    typedef struct
    {
      unsigned evtCnt;          // count of score events at this location (i.e. a chord will have more than one event at a given location)
      event_t* evtV;            // evtV[evtCnt]
      unsigned scLocIdx;        // scH score location index
      int      barNumb;         // bar number of this location
    } loc_t;

    typedef struct
    {
      unsigned mni;             // unique identifier for this MIDI note - used to recognize when the sfmatcher backtracks.
      unsigned muid;            // MIDI file event msg unique id (See cmMidiTrackMsg_t.uid)
      unsigned smpIdx;          // time stamp of this event
      unsigned pitch;           // MIDI note pitch
      unsigned vel;             //  "    "   velocity
      unsigned locIdx;          // location assoc'd with this MIDI evt (kInvalidIdx if not a  matching or non-matching 'substitute')
      unsigned scEvtIdx;        // sfscore event index assoc'd with this event
    } midi_t;

    typedef struct sfmatch_str
    {
      sfscore::handle_t scH;    // score handle
      
      unsigned locN;            // Same as sfscore::loc_count()
      loc_t*   loc;             // loc[locN] One element for each score location

      // DP matrix
      unsigned mrn;             // max m[] row count (midi)
      unsigned rn;              // cur m[] row count
      unsigned mcn;             // max m[] column count (score)
      unsigned cn;              // cur m[] column count
      value_t* m;               // m[mrn,mcn]  DP matrix
      
      unsigned mmn;             // max length of midiBuf[]    (mrn-1)
      unsigned msn;             // max length of score window (mcn-1)
      
      unsigned pn;              // mrn+mcn      
      path_t*  p_mem;           // pmem[ 2*pn ] - pre-allocated path memory
      path_t*  p_avl;           // available path record linked list
      path_t*  p_cur;           // current path linked list
      path_t*  p_opt;           // p_opt[pn] - current best alignment as a linked list
      double   opt_cost;        // last p_opt cost set by exec() 
    } sfmatch_t;

    typedef handle<struct sfmatch_str> handle_t;
      
    //  This matcher determines the optimal alignment of a short list of MIDI notes
    //  within a longer score window. It is designed to work for a single
    //  limited size alignment.  
    //        
    //  1) This matcher cannot handle multiple instances of the same pitch occuring 
    //  at the same 'location'.
    // 
    //  2) Because each note of a chord is spread out over multiple locations, and 
    //  there is no way to indicate that a note in the chord is already 'in-use'.  
    //  If a MIDI note which is part of the chord is repeated, in error, it will 
    //  appear to be correct (a positive match will be assigned to
    //  the second (and possible successive notes)). 
   

    // maxScWndN - max length of the score window (also the max value of 'locN' in subsequent calls to exec()).
    // maxMidiWndN  - max length of the performance (MIDI) buffer to compare to the score window
    //                (also the max value of 'midiN' in subsequent calls to exec()).
    rc_t create( handle_t& hRef, sfscore::handle_t scoreH, unsigned maxScWndN, unsigned maxMidiWndN );
    rc_t destroy( handle_t& hRef  );

    // Locate the position in p->loc[locIdx:locIdx+locN-1] which bests matches midiV[0:midiN].
    // The result of this function is to update p_opt[] 
    // The optimal path p_opt[] will only be updated if the edit_cost associated 'midiV[0:midiN]'.
    // with the best match is less than 'min_cost'.
    // Set 'min_cost' to DBL_MAX to force p_opt[] to be updated.
    // Returns kEofRC if locIdx + locN > p->locN - note that this is not necessarily an error.
    rc_t exec(  handle_t h, unsigned locIdx, unsigned locN, const midi_t* midiV, unsigned midiN, double min_cost );



    // This function updates the midiBuf[] fields 'locIdx' and 'scEvtIdx' after an alignment
    // has been found via an earlier call to 'exec()'.
    //   
    // Traverse the least cost path and:
    //
    // 1) Returns, esi, the score location index of the last MIDI note
    // which has a positive match with the score and assign
    // the internal score index to cp->locIdx.
    //
    // 2) Set cmScAlignPath_t.locIdx - index into p->loc[] associated
    // with each path element that is a 'substitute' or an 'insert'.
    //
    // 3) Set *missCnPtr: the count of trailing non-positive matches in midiBuf[].
    //
    // i_opt is index into p->loc[] of p->p_opt. 
    unsigned sync( handle_t h, unsigned i_opt, midi_t* midiBuf, unsigned midiN, unsigned* missCntPtr );


    // Print a matched path.
    // cp - pointer to the first path element 
    // bsi - score location index of the first element in the score window which was used to form the path.
    // midiV - pointer to the first element of the MIDI buffer used to form the path.
    void print_path( handle_t h, unsigned bsi, const midi_t* midiV );

    // Returns the index into loc->evtV[] of pitch.
    inline unsigned match_index( const loc_t* loc, unsigned pitch )
    {
      for(unsigned i=0; i<loc->evtCnt; ++i)
        if( loc->evtV[i].pitch == pitch )
          return i;
      return kInvalidIdx;
    }  

    // Return true if 'pitch' occurs at loc.
    inline bool   is_match( const loc_t* loc, unsigned pitch )          { return match_index(loc,pitch) != kInvalidIdx; }

    inline unsigned                   max_midi_wnd_count( handle_t h )  { return handleToPtr<handle_t,sfmatch_t>(h)->mmn; }
    inline unsigned                   max_score_wnd_count( handle_t h ) { return handleToPtr<handle_t,sfmatch_t>(h)->msn; }
    inline unsigned                   loc_count( handle_t h )           { return handleToPtr<handle_t,sfmatch_t>(h)->locN; }
    inline const cw::sfmatch::loc_t*  loc_base(  handle_t h )           { return handleToPtr<handle_t,sfmatch_t>(h)->loc; }
    inline double                     cost( handle_t h )                { return handleToPtr<handle_t,sfmatch_t>(h)->opt_cost; }
    inline const cw::sfmatch::path_t* optimal_path( handle_t h )        { return handleToPtr<handle_t,sfmatch_t>(h)->p_opt; }

    
    rc_t test( const object_t* cfg, sfscore::handle_t scoreH );
      
  }  
}

#endif
