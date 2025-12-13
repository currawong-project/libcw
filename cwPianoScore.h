//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwPianoScore_h
#define cwPianoScore_h

namespace cw
{
  namespace perf_score
  {
    typedef handle<struct score_str> handle_t;

    typedef struct stats_str
    {
      unsigned id;   // see: perf_meas::k???VarIdx
      double min;
      double max;
      double mean;
      double std;      
    } stats_t;

    typedef struct event_str
    {
      unsigned uid;           // unique id for this event
      unsigned meas;          // measure number
      unsigned voice;         // score number
      unsigned loc;           // score location
      unsigned tick;          // event tick location
      double   sec;           // event absolute time in seconds
      double   rval;          // event rythmic value 2=1/2 1/4 .5=2 or 0
      char     sci_pitch[5];  // scientific pitch
      char     dmark[6];      // dynamic mark (e.g. "pp","mf","fff")
      unsigned dlevel;        // dynamic level as an integer associated with dyn. mark 
      unsigned status;        // MIDI status < type | channel > or 0
      unsigned d0;            // MIDI d0 or 0
      unsigned d1;            // MIDI d1 or 0
      unsigned bpm;           // tempo BPM or 0  
      char     grace_mark[4]; // grace mark or 0
      unsigned bar;           // bar number or 0
      unsigned barPitchIdx;   // bar pitch index or 0 
      unsigned section;       // section number or 0
      unsigned chord_note_cnt;// count of notes in the chord which this note-on msg is part of
      unsigned chord_note_idx;// which note in the chord is this note-on msg (or kInvalididx if this is not a note-on msg)

      unsigned player_id;  // identifies which of multiple players will perform this event (0 based)
      unsigned piano_id;   // identifies with of multiple pianos this event is associated with (0 based )

      bool    valid_stats_fl; // is statsA valid in this record[]
      stats_t statsA[ perf_meas::kValCnt ];
      
      double   even;
      double   dyn;
      double   tempo;
      double   cost;

      double featV[    perf_meas::kValCnt ];
      double featMinV[ perf_meas::kValCnt ];
      double featMaxV[ perf_meas::kValCnt ];
      
      struct event_str* link; // list link
    } event_t;

    // CSV formats:
    //
    // 1. "dev","port","microsec","id","sec","ch","status","sci_pitch","d0","d1"
    //    This parse is chosen if the 'loc' is not contained in the CSV column title row.
    //    id -> loc
    //    See _parse_midi_csv()
    //
    // 2. "meas","loc","sec","sci_pitch","status","d0","d1","bar","section","even","dyn","tempo","cost"
    //    This CSV parse is chosen if the 'oloc' is not contained in the CSV column title row.
    //    See _read_csv()
    //
    // 3. "meas","loc","sec","sci_pitch","status","d0","d1","bar","section","oloc","index","grace",{"player_id"},{"piano_id"},{has_stats_fl}
    //    This CSV parse is chosen if the 'oloc' is not contained in the CSV column title row.
    //    oloc  -> loc
    //    index -> uid
    //    If 'hast_stats_fl' exists and is set then a wide set of statistics columns are included see: _read_meas_stats().
    rc_t create(  handle_t& hRef, const char* csv_fname );


    // JSON format:
    // "evtL":{
    //            "meas","voice","loc","tick","sec",                                                   // mandatory
    //            "rval","sci_pitch","dmark","dlevel","status","d0","d1","grace","section","bpm","bar" // optional
    //        }
    // See _parse_event_list()
    rc_t create(  handle_t& hRef, const object_t* cfg );

    // Read a CSV as written by cwIoMidiRecordPlay.save_csv().
    // In this case event.loc == event.muid.
    rc_t create_from_midi_csv( handle_t& hRef, const char* fn );
    rc_t destroy( handle_t& hRef );
    
    unsigned       event_count( handle_t h );

    // Get first event in linked list.
    const event_t* base_event( handle_t h );

    const event_t* loc_to_event( handle_t h, unsigned loc );

    // Format the event as a string for printing.
    rc_t  event_to_string( handle_t h, unsigned uid, char* buf, unsigned buf_byte_cnt );

    rc_t test( const object_t* cfg );
    
  }
}


#endif
