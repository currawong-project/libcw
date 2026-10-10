#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwGutimMeas.h"
#include "cwNumericConvert.h"
#include "cwMidi.h"

namespace cw
{
  namespace gutim_meas
  {
    // Two note records are stored for every note in the score.
    // The first has the actual note pitch and dynamic value.
    // The second has 
    typedef struct note_str
    {
      const char* note_id;
      unsigned    loc_id;
      unsigned    pitch;
      int         score_dyn;

      bool        perf_fl;
      double      perf_sec;
      int         perf_dyn;

           struct note_str* loc_link;
      
      bool                   chord_fl;
      const struct note_str* chord_link;
    } note_t;

    typedef struct loc_str
    {
      unsigned            loc_id; 
      struct section_str* section;   // this loc's section
      unsigned            meas;      // measure number
      double              score_sec; // location score time
      note_t*             noteL;     // list of notes associated with this location
      unsigned            noteN;     // count of notes at this location
      double              dur_pct;   // location time as percentage of total duration

      bool            beat_fl;
      struct loc_str* beat_link;   // beat_group->locL link

      bool            grace_fl;
      struct loc_str* grace_link;  // grace_group->locL link

      bool   eval_fl;
      double est_sec;
      double est_dev_sec;
      
    } loc_t;

    typedef struct chord_group_str
    {
      unsigned      loc_id;         // chord location
      const note_t* noteL;          // note linked list
      unsigned      noteN;          // count of notes in chord      
      struct chord_group_str* link; // section.chordGroupL link

      bool   eval_fl;
      double note_spread_sec; 
    } chord_group_t;

    typedef struct beat_group_str
    {
      loc_t*    locL;              // location linked list
      unsigned  locN;              // count of locations in the list
      double    score_dur_sec;     // total dur. of the scored group in seconds
      double    score_period_sec;  // scored beat period in seconds
      struct beat_group_str* link; // section.beatGroupL link

      bool   eval_fl;
      double period_est_sec;       // estimated beat period in seconds
      double period_dev_est_sec;   // mean deviation from the estimated beat period
      double dur_pct;              // 1=perfect match 0.5=performed at half-speed 2.0=performed at 2x speed.
      
    } beat_group_t;

    typedef struct grace_group_str
    {
      loc_t*   locL;                // location linked list
      unsigned locN;                // count of locations in the list
      double   score_dur_sec;       // grace group score duration in seconds
      double   score_period_sec;    // grace group score period in seconds
      struct grace_group_str* link; // section.graceGroupL link

      bool   eval_fl;
      double period_est_sec;        // estimated grace loc period in seconds
      double period_dev_est_sec;    // measn deviation from the estimated grace period
      double dur_pct;               // 1=perfect match 0.5=performed at half-speed 2.0=performed at 2x speed.
      
    } grace_group_t;

    typedef struct perf_note_str
    {
      unsigned perf_note_idx;
      unsigned loc_id;
      double   sec;
      unsigned midi_pitch;
      unsigned midi_velocity;
    } perf_note_t;
    
    typedef struct section_str
    {
      const char*    section_id;    // section id
      unsigned       section_index; // section index in gutim_meas_t.sectionA[]
      unsigned       beg_loc_id;    // first loc in this section
      unsigned       end_loc_id;    // last loc in this section
      
      double         score_bpm_estimate; // score BPM
      double         dur_sec;      // score section duration in seconds

      unsigned       noteN;        // count of score notes in this section
      
      chord_group_t* chordGroupL; 
      unsigned       chordGroupN;  // count of chords in this seciton

      beat_group_t*  beatGroupL;
      unsigned       beatGroupN;   // count of beat groups in this section

      grace_group_t* graceGroupL;
      unsigned       graceGroupN;  // ground of grace note groups in this section
      

      bool       eval_fl;            // 
      unsigned   missing_loc_cnt;    // count of missing locations
      double     avg_loc_dev_sec;    // avg. deviation of all loc's times from the center time
      double     mean_dyn;           // mean performed dynamic for this section
      double     dyn_dev;            // deviation from the dyn. best fit

      results_t  results;            // results for this section

      
    } section_t;
    
    typedef struct gutim_meas_str
    {
      object_t* file_cfg;

      // see mapping from gutim/score_editor/apply_edit_file.py
      unsigned vel_to_dynA[ midi::kMidiVelCnt ];   // vel_tableA[ k
      
      loc_t*   locA;        // locA[ locAllocN ] - storage for all loc records
      unsigned locAllocN;

      note_t*  noteA;       // noteA[ noteN ] - storage for all note records
      unsigned noteN;
      unsigned noteAllocN;
 
      section_t* sectionA;  // sectionA[ sectionN ] - storage for all section records
      unsigned   sectionN;
      
      chord_group_t* chordGroupA;  // chordGroupA[ chordGroupN ] - storage for all chord group records
      unsigned       chordGroupN;
      unsigned       chordGroupAllocN;

      beat_group_t*  beatGroupA;   // beatGroupA[ beatGroupN ] - storage for all beat group records
      unsigned       beatGroupN;
      unsigned       beatGroupAllocN;

      grace_group_t* graceGroupA;  // graceGroupA[ graceGroupN ] - storage for all grace group records
      unsigned       graceGroupN;
      unsigned       graceGroupAllocN;


      perf_note_t*   perfNoteA;   // perfNoteA[ perfNoteAllocN ]
      unsigned       perfNoteAllocN;

      unsigned   pni_cnt;  // current count of notes cached in perfNoteA[].
      
      section_t* next_eval_section;  // the next section that is ready to be evaluated or null if there is no section ready to be evaluated
      section_t* next_done_section;  // the next section that will have results

      unsigned   submitted_note_cnt;
      unsigned   mismatch_overwrite_cnt;
      
    } gutim_meas_t;

    gutim_meas_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,gutim_meas_t>(h); }

    void _destroy( gutim_meas_t* p )
    {
      if( p->file_cfg != nullptr )
      {
        p->file_cfg->free();
        p->file_cfg = nullptr;
      }

      mem::release(p->perfNoteA);
      mem::release(p->chordGroupA);
      mem::release(p->beatGroupA);
      mem::release(p->graceGroupA);
      mem::release(p->locA);
      mem::release(p->noteA);
      mem::release(p->sectionA);
      mem::release(p);
    }

    bool _pair_validate( const object_t* pair )
    {
      return pair->is_pair() && textLength(pair->pair_label()) > 0 && pair->pair_value()!=nullptr && pair->pair_value()->is_dict();
    }

    rc_t _section_note_count( const object_t* all_cfg, unsigned& note_cnt_ref )
    {
      rc_t     rc   = kOkRC;
      unsigned allN = all_cfg->child_count();

      note_cnt_ref = 0;
      
      for(unsigned all_idx=0; all_idx<allN; ++all_idx)
      {
        const object_t* loc_pair  = all_cfg->child_ele(all_idx);
        const object_t* loc_dict  = nullptr;
        const object_t* note_dict = nullptr;
        
        if(!_pair_validate(loc_pair))
        {
          rc = cwLogError(kSyntaxErrorRC,"Syntax error on 'all' pair at 'all' index %i.",all_idx);
          goto errLabel;
        }

        if((rc = loc_pair->pair_value()->value(loc_dict)) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"Syntax error when accessing 'all' dict. at 'all' index %i.",all_idx);
          goto errLabel;
        }
        
        if((rc = loc_dict->getv("noteD",note_dict)) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"Syntax error when accessing 'all' dict entry 'noteD' at 'all' index %i.",all_idx);
          goto errLabel;
        }

        note_cnt_ref += note_dict->child_count();
      }

    errLabel:
      return rc;
    }

    rc_t _parse_sections_pass_1( gutim_meas_t* p, const object_t* file_cfg )
    {
      rc_t     rc         = kOkRC;
      unsigned max_loc_id = 0;
      
      p->noteAllocN = 0;
      p->locAllocN = 0;

      // for each section
      for(unsigned sect_idx=0; sect_idx<p->sectionN; ++sect_idx)
      {
        const object_t* sect_pair  = p->file_cfg->child_ele(sect_idx);
        const object_t* sect_cfg   = nullptr;
        const object_t* all_cfg    = nullptr;
        const object_t* chord_cfg  = nullptr;
        const object_t* beat_cfg   = nullptr;
        const object_t* grace_cfg  = nullptr;

        // validate the section pair
        if( !_pair_validate(sect_pair) )
        {
          rc = cwLogError(kSyntaxErrorRC,"The section pair at section index %i has a syntax error.",sect_idx);
          goto errLabel;
        }

        // vaidate the section dictionary
        if((rc = sect_pair->pair_value()->value(sect_cfg)) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"The section record at %i could not be accessed.",sect_idx);
          goto errLabel;
        }
        
        if((rc = sect_cfg->getv("beg_loc",p->sectionA[sect_idx].beg_loc_id,
                                 "end_loc",p->sectionA[sect_idx].end_loc_id,
                                 "bpm_estimate",p->sectionA[sect_idx].score_bpm_estimate,
                                 "all",all_cfg,
                                 "chord",chord_cfg,
                                 "beat",beat_cfg,
                                 "grace",grace_cfg)) != kOkRC )
        {
          goto errLabel;
        }

        //printf("section:%s beg_loc:%i end_loc:%i\n",sect_pair->pair_label(),p->sectionA[sect_idx].beg_loc,p->sectionA[sect_idx].end_loc);

        // get the count of notes in this section
        if((rc = _section_note_count( all_cfg, p->sectionA[sect_idx].noteN )) != kOkRC )
        {
          goto errLabel;
        }

        // double the count of notes to allow for notes that do not match the pitch
        p->sectionA[sect_idx].noteN *= 2;

        // update the total score note count
        p->noteAllocN += p->sectionA[sect_idx].noteN;

        // track the max. loc id
        if( p->sectionA[sect_idx].end_loc_id > max_loc_id )
          max_loc_id = p->sectionA[sect_idx].end_loc_id;

        // verify that the section is not empty
        if( p->sectionA[sect_idx].beg_loc_id >= p->sectionA[sect_idx].end_loc_id)
        {
          cwLogWarning("An empty section %s was encountered at locations: beg:%i end:%i.",sect_pair->pair_label(),p->sectionA[sect_idx].beg_loc_id,p->sectionA[sect_idx].end_loc_id);
        }

        // track the count of chords,beat-groups and grace-groups in this section
        p->sectionA[sect_idx].chordGroupN   = chord_cfg->child_count();
        p->sectionA[sect_idx].beatGroupN    = beat_cfg->child_count();
        p->sectionA[sect_idx].graceGroupN   = grace_cfg->child_count();

        p->sectionA[sect_idx].section_id    = sect_pair->pair_label();
        p->sectionA[sect_idx].section_index = sect_idx;

        // track the total count of locations
        p->locAllocN += (max_loc_id - p->sectionA[sect_idx].beg_loc_id)+1;
        
      }
    errLabel:
      return rc;
    }

    note_t* _id_to_note( const gutim_meas_t* p, const char* id )
    {
      for(unsigned i=0; i<p->noteN; ++i)
        if( textIsEqual(p->noteA[i].note_id,id))
          return p->noteA + i;
      return nullptr;
    }

    rc_t _store_note( gutim_meas_t* p, const char* note_id, unsigned loc_id, unsigned pitch, unsigned score_dyn )
    {
      rc_t    rc   = kOkRC;      
      note_t* note = nullptr;
      
      if( p->noteN >= p->noteAllocN )
      {
        rc = cwLogError(kBufTooSmallRC,"The note array is full.");
        goto errLabel;
      }

      note = p->noteA + p->noteN;

      note->note_id = note_id;
      note->loc_id  = loc_id;
      note->pitch   = pitch;
      note->score_dyn = score_dyn;

      p->noteN += 1;

    errLabel:
      return rc;
      
    }
    
    rc_t _parse_sections_pass_2( gutim_meas_t* p, const object_t* file_cfg )
    {
      rc_t rc = kOkRC;

      for(unsigned sect_idx=0; sect_idx<p->sectionN; ++sect_idx)
      {
        const object_t* sect_pair = p->file_cfg->child_ele(sect_idx);
        const object_t* sect_dict = nullptr;
        const object_t* all_cfg   = nullptr;
        unsigned        allN      = 0;
        
        file_cfg->child_ele(sect_idx)->pair_value()->value(sect_dict);

        if((rc = sect_dict->getv("all",all_cfg)) != kOkRC )
        {
          rc = cwLogError(rc,"Error locating 'all' dict at in index:%i.",sect_idx);
          goto errLabel;
        }

        //printf("%s %s\n",p->sectionA[sect_idx].section_id,file_cfg->child_ele(sect_idx)->pair_label());
        
        allN = all_cfg->child_count();
        for(unsigned all_idx=0; all_idx<allN; ++all_idx)
        {
          const object_t* pair      = all_cfg->child_ele(all_idx);
          const object_t* loc_dict  = nullptr;
          const object_t* note_dict = nullptr;
          unsigned        noteN     = 0;
          unsigned        loc_id    = kInvalidIdx;
          
          if(!_pair_validate(pair))
          {
            rc = cwLogError(kSyntaxErrorRC,"'all' pair validation failed on section index %i all index %i.",sect_idx,all_idx);
            goto errLabel;
          }
          
          if((rc = string_to_number( pair->pair_label(), loc_id)) != kOkRC )
          {
            rc = cwLogError(kSyntaxErrorRC,"'all' location id parse failed on section index %i all index %i.",sect_idx,all_idx);
            goto errLabel;
          }

          if( loc_id >= p->locAllocN )
          {
            rc = cwLogError(rc,"The location id %i is out of range on loc dict. on section index %i all index %i.",loc_id,sect_idx,all_idx);
            goto errLabel;
          }

          if((rc = pair->pair_value()->value(loc_dict)) != kOkRC )
          {
            rc = cwLogError(rc,"Parse failed on loc dict. on section index %i all index %i.",sect_idx,all_idx);
            goto errLabel;
          }

          assert( loc_id <p->locAllocN );

          // set the section pointer for this loc
          p->locA[loc_id].section = p->sectionA + sect_idx;
          
          if((rc = loc_dict->getv("sec",p->locA[loc_id].score_sec,
                                  "meas",p->locA[loc_id].meas,
                                  "noteD",note_dict)) != kOkRC )
          {
            rc = cwLogError(rc,"Parse failed on note dict. on section index %i all index %i.",sect_idx,all_idx);
            goto errLabel;
          }

          //printf("  %i:%i loc:%i sec:%f meas:%i\n", all_idx,allN,loc_id, p->locA[loc_id].sec, p->locA[loc_id].meas);

          noteN = note_dict->child_count();

          // fill in the note array
          for(unsigned ni=0; ni<noteN; ++ni)
          {
            const object_t* note_pair = note_dict->child_ele(ni);
            const object_t* note = nullptr;
            unsigned midi_pitch = midi::kInvalidMidiPitch;
            unsigned score_dyn = midi::kInvalidMidiVelocity;
            
            if(!_pair_validate(note_pair))
            {              
              rc = cwLogError(rc,"Note pair validate failed on loc dict. on section index %i all index %i note index %i.",sect_idx,all_idx,ni);
              goto errLabel;
            }

            if((rc = note_pair->pair_value()->value(note)) != kOkRC )
            {
              rc = cwLogError(rc,"Note dict access failed on loc dict. on section index %i all index %i note index %i.",sect_idx,all_idx,ni);
              goto errLabel;
            }

            if( p->noteN >= p->noteAllocN )
            {
              rc = cwLogError(kBufTooSmallRC,"The note array is full.");
              goto errLabel;
            }

            if( _id_to_note(p, note_pair->pair_label()) != nullptr )
            {
              rc = cwLogError(kInvalidStateRC,"The note-id %s was duplicated.",note_pair->pair_label());
              goto errLabel;
            }

            // get the note pitch and dynamic value
            if((rc = note->getv("pitch",midi_pitch, "dlevel",score_dyn)) != kOkRC )
            {
              rc = cwLogError(rc,"Note dict field access failed on loc dict. on section index %i all index %i note index %i.",sect_idx,all_idx,ni);
              goto errLabel;
            }

            // store the note
            if((rc = _store_note(p,note_pair->pair_label(),loc_id,midi_pitch,score_dyn)) != kOkRC )
              goto errLabel;

            // store a placeholder note to capture mismatched notes for every note in the score
            if((rc = _store_note(p, "<mismatch>",loc_id,midi::kInvalidMidiPitch,midi::kInvalidMidiVelocity)) != kOkRC )
              goto errLabel;
            
          }
        }
      }
      
    errLabel:
      return rc;
    }

    rc_t _assign_notes_to_loc( gutim_meas_t* p )
    {
      rc_t rc = kOkRC;

      // for each note
      for(unsigned note_idx=0; note_idx<p->noteN; ++note_idx)
      {
        note_t* note = p->noteA + note_idx;

        // validate the notes loc id
        if(note->loc_id == kInvalidId || note->loc_id >= p->locAllocN )
        {
          rc = cwLogError(kInvalidStateRC,"The loc associated with note %s is invalid.",cwStringNullGuard(note->note_id));
          goto errLabel;
        }

        // link the note onto the loc's note list
        note->loc_link = p->locA[ note->loc_id ].noteL;
        p->locA[ note->loc_id ].noteL = note;
        p->locA[ note->loc_id ].noteN += 1;
      }
      
    errLabel:
      return rc;
    }

    
    
    rc_t _create_section_chord_groups( gutim_meas_t* p, section_t* section, const object_t* chord_dict )
    {
      rc_t     rc        = kOkRC;
      unsigned chord_idN = chord_dict->child_count();
      
      // for each chord group in this section
      for(unsigned chord_id_idx=0; chord_id_idx<chord_idN; ++chord_id_idx)
      {
        const object_t* chord_id_pair   = chord_dict->child_ele(chord_id_idx);
        const object_t* chord_id_dict   = nullptr;
        unsigned        chord_locN      = 0;
        chord_group_t*  new_chord_group = nullptr;

        // verify that there are chord group records available
        if( p->chordGroupN >= p->chordGroupAllocN )
        {
          rc = cwLogError(kSyntaxErrorRC,"The chord group array is full.");
          goto errLabel;
        }

        // get the next empty chord group recd
        new_chord_group = p->chordGroupA + p->chordGroupN;
        p->chordGroupN += 1;

        // link the chord group into the section
        new_chord_group->link = section->chordGroupL;
        section->chordGroupL = new_chord_group;

        // validate the chord-id pair
        if(!_pair_validate(chord_id_pair))
        {
          rc = cwLogError(kSyntaxErrorRC,"The chord-id pair is not valid at chord_id index %i.",chord_id_idx);
          goto errLabel;
        }

        // get the chord-id dict. 
        if((rc = chord_id_pair->pair_value()->value(chord_id_dict)) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"The chord-id dict is not valid at chord_id index %i.",chord_id_idx);
          goto errLabel;          
        }

        // there is a 1:1 relationship between chord-id and chord-loc
        if((chord_locN = chord_id_dict->child_count()) != 1 )
        {
          rc = cwLogError(kSyntaxErrorRC,"The chord-loc count (%i) is not 1.",chord_locN);
          goto errLabel;
        }

        for(unsigned chord_loc_idx=0; chord_loc_idx<chord_locN; ++chord_loc_idx)
        {
          const object_t* chord_loc_pair  = chord_id_dict->child_ele(chord_loc_idx);
          const object_t* chord_note_list = nullptr;
          unsigned        noteN           = 0;
          unsigned        loc_id          = kInvalidId;

          // verify that the note-loc pair is valid
          if( textLength(chord_loc_pair->pair_label()) == 0 || chord_loc_pair->pair_value()==nullptr || !chord_loc_pair->pair_value()->is_list())
          {
            rc = cwLogError(kSyntaxErrorRC,"chord-loc pair is not valid at chord index %i. : %i %i %i : %s",chord_loc_idx, textLength(chord_loc_pair->pair_label())==0,chord_loc_pair->pair_value()==nullptr, !chord_loc_pair->pair_value()->is_list(), chord_loc_pair->pair_label());
            goto errLabel;
          }

          // verify that the note list is a list
          if((rc = chord_loc_pair->pair_value()->value(chord_note_list)) != kOkRC )
          {
            rc = cwLogError(kSyntaxErrorRC,"chord-loc pair value is not a list at chord index %i.",chord_loc_idx);
            goto errLabel;
          }

          // validate the location id
          if(string_to_number(chord_loc_pair->pair_label(),loc_id) != kOkRC || loc_id == kInvalidId || loc_id >= p->locAllocN )
          {
            rc = cwLogError(kSyntaxErrorRC,"chord-loc location is not valid at chord index %i.",chord_loc_idx);
            goto errLabel;            
          }
            

          // get the count of notes in the chord
          noteN = chord_note_list->child_count();

          // for each note in the chord
          for(unsigned note_idx=0; note_idx<noteN; ++note_idx)
          {
            // 
            const object_t* note_id_cfg = chord_note_list->child_ele(note_idx);
            const char*     note_id     = nullptr;
            note_t*         note        = nullptr;

            // validate the note_id cfg
            if( note_id_cfg == nullptr || !note_id_cfg->is_string())
            {
              rc = cwLogError(kSyntaxErrorRC,"The note-id at note index %i is not a string.",note_idx);
              goto errLabel;
            }

            // get the note_id string
            if((rc = note_id_cfg->value(note_id)) != kOkRC )
            {
              rc = cwLogError(kSyntaxErrorRC,"The note-id at note index %i could not be parsed.",note_idx);
              goto errLabel;
            }

            // lookup the note from the note_id
            if((note = _id_to_note(p,note_id)) == nullptr )
            {
              rc = cwLogError(kSyntaxErrorRC,"The chord note-id '%s' could not be found in the note list.",note_id);
              goto errLabel;
            }

            if(note->chord_fl)
            {
              rc = cwLogError(kSyntaxErrorRC,"The note (%s) is assigned to multiple chords.",note_id);
              goto errLabel;
            }

            // link the note on to the chord note list
            note->chord_fl         = true;
            note->chord_link       = new_chord_group->noteL;
            new_chord_group->noteL = note;
          }

          // setup the remaining fields of the chord group
          new_chord_group->loc_id = loc_id;
          new_chord_group->noteN  = noteN;
        }        
      }
      
    errLabel:

      if( rc == kOkRC )
      {
        unsigned n = 0;
        for(const chord_group_t* g=section->chordGroupL; g!=nullptr; g=g->link)
          n += 1;
        
        if( n != section->chordGroupN )
          rc = cwLogError(kInvalidStateRC,"The section chord group count %i does not match the parsed count %i.",section->chordGroupN,n);
      }
      

      return rc;
    }

    rc_t _calc_beat_group_duration_and_period( beat_group_t* beat_group )
    {
      rc_t     rc         = kOkRC;
      double   dsec_accum = 0;
      unsigned n          = 1;
      
      beat_group->score_dur_sec    = 0;
      beat_group->score_period_sec = 0;

      if( beat_group->locL != nullptr )
      {
        double   sec0       = beat_group->locL->score_sec;
        
        for(const loc_t* loc=beat_group->locL->beat_link; loc!=nullptr; loc=loc->beat_link,++n)
        {
          if( loc->score_sec < sec0 )
          {
            rc = cwLogError(kInvalidStateRC,"Beat group locations are out of time order.");
            goto errLabel;
          }
          
          dsec_accum += loc->score_sec - sec0;

          sec0 = loc->score_sec;
        }

        beat_group->score_dur_sec    = dsec_accum;
        beat_group->score_period_sec = dsec_accum / n;
      }

    errLabel:
      return rc;
    }

    rc_t _create_section_beat_groups( gutim_meas_t* p, section_t* section, const object_t* beat_dict )
    {
      rc_t     rc        = kOkRC;
      unsigned beat_idN = beat_dict->child_count();
      
      // for each beat group in this section
      for(unsigned beat_id_idx=0; beat_id_idx<beat_idN; ++beat_id_idx)
      {
        const object_t* beat_id_pair   = beat_dict->child_ele(beat_id_idx);
        const object_t* beat_id_dict   = nullptr;
        beat_group_t*   new_beat_group = nullptr;

        // verify that there are beat group records available
        if( p->beatGroupN >= p->beatGroupAllocN )
        {
          rc = cwLogError(kSyntaxErrorRC,"The beat group array is full.");
          goto errLabel;
        }

        // get the next empty beat group recd
        new_beat_group = p->beatGroupA + p->beatGroupN;
        new_beat_group->locL = nullptr;
        new_beat_group->locN = 0;
        new_beat_group->link = nullptr;

        // validate the beat-id pair
        if(!_pair_validate(beat_id_pair))
        {
          rc = cwLogError(kSyntaxErrorRC,"The beat-id pair is not valid at beat_id index %i.",beat_id_idx);
          goto errLabel;
        }

        // get the beat-id dict. 
        if((rc = beat_id_pair->pair_value()->value(beat_id_dict)) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"The beat-id dict is not valid at beat_id index %i.",beat_id_idx);
          goto errLabel;          
        }

        new_beat_group->locN = beat_id_dict->child_count();
        
        for(unsigned beat_loc_idx=0; beat_loc_idx<new_beat_group->locN; ++beat_loc_idx)
        {
          const object_t* beat_loc_pair  = beat_id_dict->child_ele(beat_loc_idx);
          const object_t* beat_note_list = nullptr;
          unsigned        noteN           = 0;
          unsigned        loc_id          = kInvalidId;
          loc_t**         locpp           = nullptr;

          // verify that the note-loc pair is valid
          if( textLength(beat_loc_pair->pair_label()) == 0 || beat_loc_pair->pair_value()==nullptr || !beat_loc_pair->pair_value()->is_list())
          {
            rc = cwLogError(kSyntaxErrorRC,"beat-loc pair is not valid at beat index %i. : %i %i %i : %s",beat_loc_idx, textLength(beat_loc_pair->pair_label())==0,beat_loc_pair->pair_value()==nullptr, !beat_loc_pair->pair_value()->is_list(), beat_loc_pair->pair_label());
            goto errLabel;
          }

          // verify that the note list is a list
          if((rc = beat_loc_pair->pair_value()->value(beat_note_list)) != kOkRC )
          {
            rc = cwLogError(kSyntaxErrorRC,"beat-loc pair value is not a list at beat index %i.",beat_loc_idx);
            goto errLabel;
          }

          // validate the location id
          if(string_to_number(beat_loc_pair->pair_label(),loc_id) != kOkRC || loc_id == kInvalidId || loc_id >= p->locAllocN )
          {
            rc = cwLogError(kSyntaxErrorRC,"beat-loc location is not valid at beat index %i.",beat_loc_idx);
            goto errLabel;            
          }

          // a location can only be assigned to one beat
          if( p->locA[loc_id].beat_fl)
          {
            rc = cwLogError(kSyntaxErrorRC,"The beat location %i is attached to multiple beat groups.",loc_id);
            goto errLabel;
          }

          p->locA[ loc_id ].beat_fl = true;

          // iterate to the end of the linked list ...
          for(locpp = &new_beat_group->locL; *locpp != nullptr; locpp = &((*locpp)->beat_link) )
          {}

          // ... and point to the new end location
          *locpp = p->locA + loc_id;
                      
          // get the count of notes in the beat
          noteN = beat_note_list->child_count();

          // for each note in the beat
          for(unsigned note_idx=0; note_idx<noteN; ++note_idx)
          {
            // 
            const object_t* note_id_cfg = beat_note_list->child_ele(note_idx);
            const char*     note_id     = nullptr;
            note_t*         note        = nullptr;

            // validate the note_id cfg
            if( note_id_cfg == nullptr || !note_id_cfg->is_string())
            {
              rc = cwLogError(kSyntaxErrorRC,"The note-id at note index %i is not a string.",note_idx);
              goto errLabel;
            }

            // get the note_id string
            if((rc = note_id_cfg->value(note_id)) != kOkRC )
            {
              rc = cwLogError(kSyntaxErrorRC,"The note-id at note index %i could not be parsed.",note_idx);
              goto errLabel;
            }

            // lookup the note from the note_id
            if((note = _id_to_note(p,note_id)) == nullptr )
            {
              rc = cwLogError(kSyntaxErrorRC,"The beat note-id '%s' could not be found in the note list.",note_id);
              goto errLabel;
            }

            if(note->loc_id != loc_id )
            {
              rc = cwLogError(kSyntaxErrorRC,"The location (%i) of the beat note-id '%s' does not match the beat list location (%i).",note->loc_id,note_id,loc_id);
              goto errLabel;
            }
          }
        }

        p->beatGroupN += 1;
        
        // link the beat group into the section
        new_beat_group->link  = section->beatGroupL;
        section->beatGroupL   = new_beat_group;
        
        // calculate the scored beat group duration and period
        if((rc = _calc_beat_group_duration_and_period( new_beat_group )) != kOkRC )
        {
          goto errLabel;
        }

      }
    errLabel:

      if( rc == kOkRC )
      {
        // verify that the cound of beat-groups in the section beat-group list matches the number that were promised in the cfg.
        unsigned n = 0;
        for(const beat_group_t* g=section->beatGroupL; g!=nullptr; g=g->link)
          n += 1;
        
        if( n != section->beatGroupN )
          rc = cwLogError(kInvalidStateRC,"The section beat group count %i does not match the parsed count %i.",section->beatGroupN,n);
      }
      
      return rc;
    }


    rc_t _calc_grace_group_duration_and_period( grace_group_t* grace_group )
    {
      rc_t     rc         = kOkRC;
      double   dsec_accum = 0;
      unsigned n          = 1;
      
      grace_group->score_dur_sec    = 0;
      grace_group->score_period_sec = 0;

      if( grace_group->locL != nullptr )
      {
        double   sec0       = grace_group->locL->score_sec;
        
        for(const loc_t* loc=grace_group->locL->grace_link; loc!=nullptr; loc=loc->grace_link,++n)
        {
          if( loc->score_sec < sec0 )
          {
            rc = cwLogError(kInvalidStateRC,"Grace group locations are out of time order.");
            goto errLabel;
          }
          
          dsec_accum += loc->score_sec - sec0;

          sec0 = loc->score_sec;
        }

        grace_group->score_dur_sec    = dsec_accum;
        grace_group->score_period_sec = dsec_accum / n;
      }

    errLabel:
      return rc;
    }
    
    rc_t _create_section_grace_groups( gutim_meas_t* p, section_t* section, const object_t* grace_dict )
    {
      rc_t     rc        = kOkRC;
      unsigned grace_idN = grace_dict->child_count();
      
      // for each grace group in this section
      for(unsigned grace_id_idx=0; grace_id_idx<grace_idN; ++grace_id_idx)
      {
        const object_t* grace_id_pair   = grace_dict->child_ele(grace_id_idx);
        const object_t* grace_id_dict   = nullptr;
        grace_group_t*  new_grace_group = nullptr;

        // verify that there are grace group records available
        if( p->graceGroupN >= p->graceGroupAllocN )
        {
          rc = cwLogError(kSyntaxErrorRC,"The grace group array is full.");
          goto errLabel;
        }

        // get the next empty grace group recd
        new_grace_group = p->graceGroupA + p->graceGroupN;

        new_grace_group->locL = nullptr;
        new_grace_group->locN = 0;
        new_grace_group->link = nullptr;
        

        // validate the grace-id cfg pair
        if(!_pair_validate(grace_id_pair))
        {
          rc = cwLogError(kSyntaxErrorRC,"The grace-id pair is not valid at grace_id index %i.",grace_id_idx);
          goto errLabel;
        }

        // get the grace-id cfg dict. 
        if((rc = grace_id_pair->pair_value()->value(grace_id_dict)) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"The grace-id dict is not valid at grace_id index %i.",grace_id_idx);
          goto errLabel;          
        }

        // get the count of locations in this grace group
        new_grace_group->locN = grace_id_dict->child_count();

        for(unsigned grace_loc_idx=0; grace_loc_idx<new_grace_group->locN; ++grace_loc_idx)
        {
          const object_t* grace_loc_pair  = grace_id_dict->child_ele(grace_loc_idx);
          const object_t* grace_note_list = nullptr;
          unsigned        noteN           = 0;
          unsigned        loc_id          = kInvalidId;
          loc_t**         locpp           = nullptr;

          // verify that the note-loc cfg pair is valid
          if( textLength(grace_loc_pair->pair_label()) == 0 || grace_loc_pair->pair_value()==nullptr || !grace_loc_pair->pair_value()->is_list())
          {
            rc = cwLogError(kSyntaxErrorRC,"grace-loc pair is not valid at grace index %i. : %i %i %i : %s",grace_loc_idx, textLength(grace_loc_pair->pair_label())==0,grace_loc_pair->pair_value()==nullptr, !grace_loc_pair->pair_value()->is_list(), grace_loc_pair->pair_label());
            goto errLabel;
          }

          // verify that the note list is a cfg list
          if((rc = grace_loc_pair->pair_value()->value(grace_note_list)) != kOkRC )
          {
            rc = cwLogError(kSyntaxErrorRC,"grace-loc pair value is not a list at grace index %i.",grace_loc_idx);
            goto errLabel;
          }

          // parse the cfg location id
          if(string_to_number(grace_loc_pair->pair_label(),loc_id) != kOkRC || loc_id == kInvalidId || loc_id >= p->locAllocN )
          {
            rc = cwLogError(kSyntaxErrorRC,"grace-loc location is not valid at grace index %i.",grace_loc_idx);
            goto errLabel;            
          }

          // a location can only be assigned to one grace group
          if( p->locA[loc_id].grace_fl)
          {
            cwLogWarning("The grace location %i is attached to multiple grace groups. This represents an error in the group_info.json file.",loc_id);
            new_grace_group = nullptr;
            assert( section->graceGroupN > 0 );
            if( section->graceGroupN > 0 )
              section->graceGroupN -= 1;
            break;
          }

          p->locA[ loc_id ].grace_fl = true;

          // iterate to the end of the linked list ...
          for(locpp = &new_grace_group->locL; *locpp != nullptr; locpp = &((*locpp)->grace_link) )
          {}

          // ... and point to the new end location
          *locpp = p->locA + loc_id;

          
          // get the count of notes in the grace
          noteN = grace_note_list->child_count();

          // for each note in the grace
          for(unsigned note_idx=0; note_idx<noteN; ++note_idx)
          {
            // 
            const object_t* note_id_cfg = grace_note_list->child_ele(note_idx);
            const char*     note_id     = nullptr;
            note_t*         note        = nullptr;

            // validate the note_id cfg
            if( note_id_cfg == nullptr || !note_id_cfg->is_string())
            {
              rc = cwLogError(kSyntaxErrorRC,"The note-id at note index %i is not a string.",note_idx);
              goto errLabel;
            }

            // get the note_id string
            if((rc = note_id_cfg->value(note_id)) != kOkRC )
            {
              rc = cwLogError(kSyntaxErrorRC,"The note-id at note index %i could not be parsed.",note_idx);
              goto errLabel;
            }

            // lookup the note from the note_id
            if((note = _id_to_note(p,note_id)) == nullptr )
            {
              rc = cwLogError(kSyntaxErrorRC,"The grace note-id '%s' could not be found in the note list.",note_id);
              goto errLabel;
            }

            if(note->loc_id != loc_id )
            {
              rc = cwLogError(kSyntaxErrorRC,"The location (%i) of the grace note-id '%s' does not match the grace list location (%i).",note->loc_id,note_id,loc_id);
              goto errLabel;
            }
          }
        }

        if( new_grace_group != nullptr )
        {
          p->graceGroupN += 1;
          
          // link the grace group into the section
          new_grace_group->link  = section->graceGroupL;
          section->graceGroupL   = new_grace_group;
          
          // calculate the scored beat group duration and period
          if((rc = _calc_grace_group_duration_and_period( new_grace_group )) != kOkRC )
          {
            goto errLabel;
          }
        }
      }
    errLabel:
      if( rc == kOkRC )
      {
        unsigned n = 0;
        for(const grace_group_t* g=section->graceGroupL; g!=nullptr; g=g->link)
          n += 1;
        
        if( n != section->graceGroupN )
          rc = cwLogError(kInvalidStateRC,"The section grace group count %i does not match the parsed count %i.",section->graceGroupN,n);
      }
      
      return rc;
    }
    
    rc_t _alloc_groups( gutim_meas_t* p )
    {
      p->chordGroupAllocN = 0;
      p->beatGroupAllocN  = 0;
      p->graceGroupAllocN = 0;

      for(unsigned sect_idx=0; sect_idx<p->sectionN; ++sect_idx)
      {
        p->chordGroupAllocN += p->sectionA[sect_idx].chordGroupN;
        p->beatGroupAllocN  += p->sectionA[sect_idx].beatGroupN;
        p->graceGroupAllocN += p->sectionA[sect_idx].graceGroupN;
      }

      p->chordGroupA = mem::allocZ<chord_group_t>(p->chordGroupAllocN);
      p->beatGroupA  = mem::allocZ<beat_group_t>( p->beatGroupAllocN );
      p->graceGroupA = mem::allocZ<grace_group_t>( p->graceGroupAllocN );

      return kOkRC;
    }

    rc_t _create_groups( gutim_meas_t* p, const object_t* file_cfg )
    {
      rc_t rc = kOkRC;

      for(unsigned sect_idx=0; sect_idx<p->sectionN; ++sect_idx)
      {
        const object_t* sect_pair  = file_cfg->child_ele(sect_idx);        
        const object_t* sect_dict  = nullptr;
        const object_t* chord_dict = nullptr;
        const object_t* beat_dict  = nullptr;
        const object_t* grace_dict = nullptr;

        sect_pair->pair_value()->value(sect_dict);

        sect_dict->getv("chord",chord_dict,"beat",beat_dict,"grace",grace_dict);

        if((rc = _create_section_chord_groups(p,p->sectionA + sect_idx, chord_dict)) != kOkRC )
        {
          rc = cwLogError(rc,"Chord group creation at section %s failed.",cwStringNullGuard(p->sectionA[sect_idx].section_id));
          goto errLabel;
        }

        if((rc = _create_section_beat_groups(p, p->sectionA + sect_idx, beat_dict)) != kOkRC )
        {
          rc = cwLogError(rc,"Beat group creation at section %s failed.",cwStringNullGuard(p->sectionA[sect_idx].section_id));
          goto errLabel;
        }

        if((rc = _create_section_grace_groups(p, p->sectionA + sect_idx, grace_dict)) != kOkRC )
        {
          rc = cwLogError(rc,"Grace group creation at section %s failed.",cwStringNullGuard(p->sectionA[sect_idx].section_id));
          goto errLabel;
        }
      }

    errLabel:
      return rc;
    }

    rc_t _validate( gutim_meas_t* p )
    {
      rc_t rc = kOkRC;
      
      return rc;
    }

    rc_t _parse_cfg_file( gutim_meas_t* p, const char* fname )
    {
      rc_t          rc         = kOkRC;
       
      // parse the cfg file 
      if((rc = objectFromFile(fname,p->file_cfg)) != kOkRC )
      {
        goto errLabel;
      }

      // create the section array
      p->sectionN = p->file_cfg->child_count();
      p->sectionA = mem::allocZ<section_t>(p->sectionN);

      // parse the sections and determine the sizes of the location and note arrays
      if((rc = _parse_sections_pass_1( p, p->file_cfg )) != kOkRC )
      {
        rc = cwLogError(rc,"Section parse pass 1 failed.");
        goto errLabel;
      }

      cwLogInfo("%i sections %i locs %i notes.",p->sectionN,p->locAllocN,p->noteAllocN);
      
      p->locA  = mem::allocZ<loc_t>(p->locAllocN);
      p->noteA = mem::allocZ<note_t>(p->noteAllocN);

      // allocate space for twice as many notes as are likely to be performed 
      p->perfNoteAllocN = p->noteAllocN * 2;  
      p->perfNoteA      = mem::allocZ<perf_note_t>(p->perfNoteAllocN);
      p->pni_cnt        = 0;
      

      for(unsigned i=0; i<p->noteAllocN; ++i)
        p->noteA[i].loc_id = kInvalidId;
      
      for(unsigned i=0; i<p->locAllocN; ++i)
        p->locA[i].loc_id = i;
      
      // fill in p->locA[] and p->noteA[]
      if((rc = _parse_sections_pass_2(p, p->file_cfg )) != kOkRC )
      {
        rc = cwLogError(rc,"Section parse pass 2 failed.");
        goto errLabel;
      }

      // attach notes to their location
      if((rc = _assign_notes_to_loc(p)) != kOkRC )
      {
        rc = cwLogError(rc,"Assignment of notes to locations failed.");
      }

      // allocate beat,chord,grace groups
      if((rc = _alloc_groups(p)) != kOkRC )
      {
        rc = cwLogError(rc,"Group allocation failed.");
        goto errLabel;
      }

      // fill the group score data
      if((rc = _create_groups(p,p->file_cfg)) != kOkRC )
      {
        rc = cwLogError(rc,"Create groups failed.");
        goto errLabel;
      }

      // validate the data structures
      if((rc = _validate(p)) != kOkRC )
      {
        rc = cwLogError(rc,"Validation failed.");
        goto errLabel;
      }
      
    errLabel:

      if( rc != kOkRC )
        rc = cwLogError(rc,"GUTIM meas. cfg file parse failed on '%s'.",cwStringNullGuard(fname));

      return rc;
    }
    rc_t _fill_vel_table( gutim_meas_t* p, const object_t* vel_cfg_list )
    {
      rc_t     rc  = kOkRC;
      unsigned vti = 0;
      unsigned dyn = 0;
      
      unsigned cfg_vel_tbl_cnt = vel_cfg_list->child_count();

      // for each element in the cfg vel. table
      for(unsigned cfg_idx=0; cfg_idx<cfg_vel_tbl_cnt; ++cfg_idx)
      {
        const object_t* int_cfg   = vel_cfg_list->child_ele(cfg_idx);
        unsigned        upr_limit = midi::kInvalidMidiByte;

        // read the upper limit on this segment of the vel table
        if(!int_cfg->is_integer() || (rc = int_cfg->value(upr_limit)) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"Velocity table read failed on index %i.",cfg_idx);
          goto errLabel;
        }

        // fill in this segment of the vel table
        for(; vti<midi::kMidiVelCnt && vti<=upr_limit; ++vti)
          p->vel_to_dynA[ vti ] = dyn;

        dyn+= 1;
      }
      
    errLabel:
      return rc;
    }
    
    rc_t _parse_vel_table( gutim_meas_t* p, const char* fname, const char* table_name )
    {
      rc_t            rc              = kOkRC;
      object_t*       vt_file         = nullptr;
      const object_t* tables_cfg_list = nullptr;
      unsigned        tableN          = 0;
      
      if((rc = objectFromFile(fname,vt_file)) != kOkRC )
      {
        goto errLabel;
      }
      
      if((rc = vt_file->getv("tables",tables_cfg_list)) != kOkRC )
      {
        goto errLabel;
      }

      if(!tables_cfg_list->is_list())
      {
        rc = cwLogError(kSyntaxErrorRC,"The velocity table cfg list is not a list.");
        goto errLabel;
      }

      tableN = tables_cfg_list->child_count();

      // for each table in the vel table file
      for(unsigned i=0; i<tableN; ++i)
      {
        const object_t* table_dict     = tables_cfg_list->child_ele(i);
        const object_t* vel_cfg_list   = nullptr;
        const char*     cfg_table_name = nullptr;

        // validate the table-dict
        if( !table_dict->is_dict() )
        {
          rc = cwLogError(kSyntaxErrorRC,"The table cfg dictionary is not a dictionary.");
          goto errLabel;
        }

        // get the vel-table name and data list 
        if((rc = table_dict->getv("name",cfg_table_name, "table", vel_cfg_list )) != kOkRC )
        {
          goto errLabel;
        }

        // if this is the table we are looking for
        if( textIsEqual(table_name,cfg_table_name) )
        {
          // fill the vel-to-dyn table
          if((rc = _fill_vel_table(p,vel_cfg_list)) != kOkRC )
            goto errLabel;
          
          break;
        }        
      }
      
    errLabel:
      if( rc != kOkRC )
      {
        rc = cwLogError(rc,"Velocity table parse failed.");
      }
      
      if(vt_file != nullptr )
        vt_file->free();
      
      return rc;
    }


    void _chord_eval(gutim_meas_t* p, chord_group_t* cg )
    {
      // if this chord has at least 2 notes
      if( cg->noteN > 2 && cg->noteL != nullptr && cg->noteL->chord_link != nullptr )
      {
        double   acc  = 0;
        unsigned n    = 1;

        // get the time of the first note
        double   sec0 = cg->noteL->perf_sec; 

        // for each successive note that was performed
        for(const note_t* note=cg->noteL->chord_link; note!=nullptr; note=note->chord_link)
          if( note->perf_fl )
          {
            // sum the delta time between notes
            acc += fabs(note->perf_sec - sec0);            
            n   += 1;
            sec0 = note->perf_sec;            
          }

        cg->eval_fl = true;
        cg->note_spread_sec = acc/n;
        
        cwLogInfo("     : chord : spread:%6.3f", cg->note_spread_sec ); 

      }
    }

    void _section_loc_time_eval( gutim_meas_t* p, section_t* section )
    {
      double  section_dev_accum = 0.0;
      unsigned section_dev_cnt = 0;
      
      // for each location in this section
      for(unsigned loc_id=section->beg_loc_id; loc_id<=section->end_loc_id; ++loc_id)
        if( p->locA[loc_id].noteN > 0 )
        {
          double   acc = 0;
          unsigned n   = 0;
          loc_t*   loc = p->locA + loc_id;
          unsigned vi  = 0;
          unsigned vN  = loc->noteN;
          double   vA[ vN ];

          // for each note at this location
          for(const note_t* note=loc->noteL; vi<vN && note!=nullptr; note=note->loc_link)
            if( note->perf_fl )
            {
              vA[vi++] = note->perf_sec;
              acc += note->perf_sec;
            }

          // if some performed notes were found
          if( vi > 0 )
          {
            loc->eval_fl = true;

            if( vi == 1 )
            {
              loc->est_sec = acc;
              loc->est_dev_sec = 0;
            }
            else
            {          
              // set the mean time of all notes as the location time
              loc->est_sec = acc / vi;
            
              // calc. the deviation from the mean
              acc = 0.0;
              for(unsigned i=0; i<vi; ++i)
                acc += fabs(vA[i] - loc->est_sec);
              loc->est_dev_sec = acc/vi;

              section_dev_accum += loc->est_dev_sec;
              section_dev_cnt   += 1;
            }
          }
        
        }

      // get the total spread across all sections
      section->avg_loc_dev_sec = section_dev_cnt==0 ? 0 : section_dev_accum / section_dev_cnt;

      // update the results
      section->results.avg_loc_dev_sec = section->avg_loc_dev_sec;
      section->results.avg_dyn         = section->mean_dyn;
      section->results.avg_dyn_dev     = section->dyn_dev;
      
    }

    rc_t _linear_fit(const double* X, const double* Y, int N, double& mean_y_ref, double& rss_ref)
    {
      rc_t   rc        = kOkRC;
      double mean_x    = 0.0;
      double mean_y    = 0.0;
      double Sxx       = 0.0;
      double Sxy       = 0.0;
      double Syy       = 0.0;
      double slope     = 0;
      double intercept = 0;


      if( N < 2 )
      {
        rc = cwLogError(kInvalidArgRC,"linear_fit requires N >= 2");
        goto errLabel;
      }
      
      // First pass: means.
      for (int i = 0; i < N; ++i)
      {
        mean_x += X[i];
        mean_y += Y[i];
      }

      mean_x /= N;
      mean_y /= N;

      // Second pass: centered sums.
      for (int i = 0; i < N; ++i)
      {
        const double dx = X[i] - mean_x;
        const double dy = Y[i] - mean_y;

        Sxx += dx * dx;
        Sxy += dx * dy;
        Syy += dy * dy;
      }

      if (Sxx == 0.0)
      {
        cwLogWarning("Linear fit found 0 variance!");
        goto errLabel;
      }
      
      slope     = Sxy / Sxx;
      intercept = mean_y - slope * mean_x;

      // Sum of squared residuals.
      rss_ref    = Syy - slope * Sxy;
      mean_y_ref = mean_y;

    errLabel:
      if(rc!=kOkRC)
        rc = cwLogError(rc,"Linear fit failed.");
      
      return rc;
    }

    rc_t _section_dynamics_eval( gutim_meas_t* p, section_t* section )
    {
      rc_t rc = kOkRC;
      
      if( section->noteN < 2  )
      {
        rc = cwLogError(kInvalidStateRC,"The section has fewer than 2 notes (%s).",cwStringNullGuard(section->section_id));
        goto errLabel;
      }
      else
      {
        double score_dynV[ section->noteN ];
        double perf_dynV[  section->noteN ];
        unsigned vi = 0;
        
        // for each location in this section
        for(unsigned loc_id=section->beg_loc_id; vi<section->noteN && loc_id<=section->end_loc_id; ++loc_id)
        {
          loc_t*   loc = p->locA + loc_id;
          
          // for each note at this location
          for(const note_t* note=loc->noteL; vi<section->noteN && note!=nullptr; note=note->loc_link)
            if( note->perf_fl )
            {
              score_dynV[ vi ] = note->score_dyn;
              perf_dynV[  vi ] = note->perf_dyn;
              vi += 1;
            }
        }

        if((rc = _linear_fit(score_dynV, perf_dynV, vi, section->mean_dyn, section->dyn_dev)) != kOkRC )
          goto errLabel;
                  
      }
      
    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Dynamics evaluation failed.");
      
      return rc;
        
    }

    rc_t _section_chord_eval( gutim_meas_t* p, section_t* section )
    {
      rc_t     rc    = kOkRC;
      double   accum = 0.0;
      unsigned n     = 0;
      
      for(chord_group_t* cg=section->chordGroupL; cg!=nullptr; cg=cg->link)
      {
        _chord_eval(p,cg);

        if( cg->eval_fl )
        {
          accum += cg->note_spread_sec;
          n += 1;
        }
        
      }
      
      section->results.chord_cnt = n;
      section->results.avg_chord_spread_secs = n==0 ? 0 : accum / n;

    errLabel:
      return rc;
    }
    

    typedef struct time_posn_str
    {
      double sec;
      double position;
    } time_posn_t;
    
    rc_t _period_and_deviation_estimate( const time_posn_t* psA, unsigned psN, double& est_period_sec_ref, double& period_dev_ref )
    {
      rc_t rc = kOkRC;
      
      double sec_mean = 0.0;
      double pos_mean = 0.0;
      double num      = 0.0;
      double den      = 0.0;
      double slope    = 0.0;
      double rss      = 0.0;

      if( psN < 2 )
      {
        rc = cwLogError(kInvalidArgRC,"The period of a sequence cannot be determined from less than 2 points.");
        goto errLabel;
      }
      
      for(unsigned i=0; i<psN; ++i)
      {
        sec_mean += psA[i].sec;
        pos_mean += psA[i].position;
      }

      sec_mean /= psN;
      pos_mean /= psN;

      for(unsigned i=0; i<psN; ++i)
      {
        double x = psA[i].position - pos_mean;
        num += x * (psA[i].sec - sec_mean);
        den += x*x;
      }

      slope = num/den;

      for(unsigned i=0; i<psN; ++i)
      {
        double x = psA[i].sec - (psA[0].sec + slope * psA[i].position);
        rss = x*x;
      }

      est_period_sec_ref = slope;
      period_dev_ref     = rss;

    errLabel:
      return rc;
    }

    rc_t _beat_group_eval(gutim_meas_t* p, beat_group_t* bg)
    {
      rc_t rc = kOkRC;

      bg->eval_fl = false;

      if( bg->locN < 2 )
      {
        cwLogWarning("Cannot evaluate a beat group with less than 2 beats.");
        return rc;
      }
      
           
      time_posn_t  time_posA[bg->locN];
      unsigned     beat_index  = 0;
      unsigned     tpi         = 0;
      const loc_t* min_sec_loc = nullptr;
      const loc_t* max_sec_loc = nullptr;
      
      for(const loc_t* loc=bg->locL; tpi<bg->locN && loc!=nullptr; loc=loc->beat_link,++beat_index)
        if( loc->eval_fl )
        {
          time_posA[tpi].sec = loc->est_sec;
          time_posA[tpi].position = beat_index;
          tpi += 1;

          if( min_sec_loc == nullptr or loc->score_sec < min_sec_loc->score_sec )
            min_sec_loc = loc;
          
          if( max_sec_loc == nullptr or loc->score_sec > max_sec_loc->score_sec )
            max_sec_loc = loc;
          
        }

      if( tpi < 2 )
      {
        cwLogWarning("Cannot evaluate a beat group with less than 2 observed beats.");
        return rc;          
      }

      if((rc = _period_and_deviation_estimate(time_posA, tpi, bg->period_est_sec, bg->period_dev_est_sec )) != kOkRC )
      {
        goto errLabel;
      }

      // by default make the performed duration a perfect match to the score duration
      bg->dur_pct = 1.0;
      
      // if the min/max observed locations are out of order then we can't estimate the performed duration
      if( min_sec_loc->est_sec >= max_sec_loc->est_sec || min_sec_loc->score_sec > max_sec_loc->score_sec )
      {
        cwLogWarning("Min/max observed or score beat locations are out of time order.");
        goto errLabel;
      }
      
      bg->dur_pct = (max_sec_loc->est_sec - min_sec_loc->est_sec)/(max_sec_loc->score_sec - min_sec_loc->score_sec);

      cwLogInfo("     : beat  : period: %6.3f dev:%6.3f dur pct:%6.3f", bg->period_est_sec, bg->period_dev_est_sec, bg->dur_pct ); 
      
    errLabel:
      if( rc!=kOkRC )
      {
        rc = cwLogError(rc,"Beat group evaluation failed.");
      }
      
      return rc;
    }

    rc_t _section_beat_group_eval( gutim_meas_t* p, section_t* section )
    {
      rc_t     rc = kOkRC;
      unsigned n  = 0;
      
      // for each beat group
      section->results.avg_beat_period_dev_sec = 0;
      section->results.avg_beat_dur_pct = 0;
      
      for(beat_group_t* bg=section->beatGroupL; bg!=nullptr; bg=bg->link)
      {
        if((rc = _beat_group_eval(p,bg)) != kOkRC )
          goto errLabel;

        if(bg->eval_fl)
        {
          section->results.avg_beat_period_dev_sec += bg->period_est_sec;
          section->results.avg_beat_dur_pct        += bg->period_dev_est_sec;
          n += 1;
        }        
      }
      
      if( n != 0 )
      { 
        section->results.beat_group_cnt = n;
        section->results.avg_beat_period_dev_sec /= n;
        section->results.avg_beat_dur_pct /= n;
      }

    errLabel:
      return rc;
    } 

    
    rc_t _grace_group_eval(gutim_meas_t* p, grace_group_t* gg)
    {
      rc_t rc = kOkRC;

      gg->eval_fl = false;

      if( gg->locN < 2 )
      {
        cwLogWarning("Cannot evaluate a grace group with less than 2 grace notes.");
        return rc;
      }
                 
      time_posn_t  time_posA[gg->locN];
      unsigned     grace_index = 0;
      unsigned     tpi         = 0;
      const loc_t* min_sec_loc = nullptr;
      const loc_t* max_sec_loc = nullptr;
      
      for(const loc_t* loc=gg->locL; tpi<gg->locN && loc!=nullptr; loc=loc->grace_link,++grace_index)
        if( loc->eval_fl )
        {
          time_posA[tpi].sec = loc->est_sec;
          time_posA[tpi].position = grace_index;
          tpi += 1;

          if( min_sec_loc == nullptr or loc->score_sec < min_sec_loc->score_sec )
            min_sec_loc = loc;
          
          if( max_sec_loc == nullptr or loc->score_sec > max_sec_loc->score_sec )
            max_sec_loc = loc;          
        }

      if( tpi < 2 )
      {
        cwLogWarning("Cannot evaluate a grace group with less than 2 observed grace notes.");
        return rc;          
      }

      if((rc = _period_and_deviation_estimate(time_posA, tpi, gg->period_est_sec, gg->period_dev_est_sec )) != kOkRC )
      {
        goto errLabel;
      }

      // by default make the performed duration a perfect match to the score duration
      gg->dur_pct = 1.0;
      
      // if the min/max observed locations are out of order then we can't estimate the performed duration
      if( min_sec_loc->est_sec >= max_sec_loc->est_sec || min_sec_loc->score_sec > max_sec_loc->score_sec )
      {
        cwLogWarning("Min/max observed or score grace note locations are out of time order.");
        goto errLabel;
      }
      
      gg->dur_pct = (max_sec_loc->est_sec - min_sec_loc->est_sec)/(max_sec_loc->score_sec - min_sec_loc->score_sec);

      cwLogInfo("     : grace  : period: %6.3f dev:%6.3f dur pct:%6.3f", gg->period_est_sec, gg->period_dev_est_sec, gg->dur_pct ); 
      
    errLabel:
      if( rc!=kOkRC )
      {
        rc = cwLogError(rc,"Grace group evaluation failed.");
      }
      
      return rc;
    }

    rc_t _section_grace_group_eval( gutim_meas_t* p, section_t* section )
    {
      rc_t     rc = kOkRC;
      unsigned n  = 0;
      
      // for each grace group
      section->results.avg_grace_period_dev_sec = 0;
      section->results.avg_grace_dur_pct = 0;
      
      for(grace_group_t* bg=section->graceGroupL; bg!=nullptr; bg=bg->link)
      {
        if((rc = _grace_group_eval(p,bg)) != kOkRC )
          goto errLabel;

        if(bg->eval_fl)
        {
          section->results.avg_grace_period_dev_sec += bg->period_est_sec;
          section->results.avg_grace_dur_pct        += bg->period_dev_est_sec;
          n += 1;
        }        
      }
      
      if( n != 0 )
      {
        section->results.grace_group_cnt = n;
        section->results.avg_grace_period_dev_sec /= n;
        section->results.avg_grace_dur_pct /= n;
      }

    errLabel:
      return rc;
    } 

    
    rc_t  _set_perf_note( gutim_meas_t* p, note_t* note, double sec, unsigned midi_vel )
    {
      rc_t rc = kOkRC;
      if( midi_vel >= midi::kMidiVelCnt )
      {
        cwLogWarning("An invalid MIDI velocity was encountered.");
        goto errLabel;
      }
      
      note->perf_fl = true;
      note->perf_sec = sec;
      note->perf_dyn = p->vel_to_dynA[ midi_vel ];

      p->submitted_note_cnt += 1;

    errLabel:
      return rc;
    }
    
    rc_t _process_incoming_note( gutim_meas_t* p, unsigned loc_id, double sec, unsigned midi_pitch, unsigned midi_vel )
    {
      rc_t          rc  = kOkRC;
      loc_t*        loc = nullptr;

      if( loc_id == kInvalidId )
        return rc;

      if( loc_id >= p->locAllocN )
      {
        rc = cwLogError(kInvalidArgRC,"The loc. id %i is out of range %i.",loc_id,p->locAllocN);
        goto errLabel;
      }

      loc = p->locA + loc_id;

      if( midi_pitch != midi::kInvalidMidiPitch )
      {
        // for each note at the performed location
        for(note_t* note = loc->noteL; note!=nullptr; note=note->loc_link)
        {
          // if this is the pitch of interest
          if( note->pitch == midi_pitch )
          {
            rc = _set_perf_note( p, note, sec, midi_vel);
            goto errLabel;           
          }
        }
      }
      else  // this is a mismatch note
      {
        note_t* fallback_note = nullptr;
        
        // for each note at the performed location
        for(note_t* note = loc->noteL; note!=nullptr; note=note->loc_link)
        {
          // if this is a designated mismatch note
          if( note->pitch == midi::kInvalidMidiPitch )
          {
            // if this note is already in use
            if( note->perf_fl )
            {
              // track the oldest mis-match note
              if( fallback_note == nullptr || note->perf_sec < fallback_note->perf_sec )
                fallback_note = note;
            }
            else
            {
              rc = _set_perf_note(p,note,sec,midi_vel);
              goto errLabel;
            }
          }
        }

        // all mismatch notes are in use - overwrite the oldest
        if( fallback_note != nullptr )
        {
          p->mismatch_overwrite_cnt += 1;
          
          rc = _set_perf_note(p,fallback_note, sec, midi_vel );
          goto errLabel;
        }
      }
      
      rc = cwLogError(kInvalidStateRC,"The pitch %i was not found at the location %i.",midi_pitch,loc_id);
  
    errLabel:
      return rc;
    }

    rc_t _submit_cached_notes( gutim_meas_t* p )
    {
      rc_t rc = kOkRC;
      unsigned n = 0;
      
      // Itertate through the cache 
      for(unsigned i=0; i<p->pni_cnt; ++i)
      {
        perf_note_t* pn = p->perfNoteA + i;

        // Cache records with an invalid perf_note_idx are empty.
        if( pn->perf_note_idx != kInvalidIdx )
        {
          if((rc = _process_incoming_note(p,pn->loc_id, pn->sec, pn->midi_pitch, pn->midi_velocity)) != kOkRC )
          {
            rc = cwLogError(rc,"Note submission failed.");
            goto errLabel;
          }
          
          pn->perf_note_idx = kInvalidIdx;
          n += 1;
        }
      }

      cwLogInfo("%i notes submitted.",n);
      p->pni_cnt = 0;
      
    errLabel:
      return rc;
    }

    rc_t _section_eval(gutim_meas_t* p, section_t* section )
    {
      rc_t rc = kOkRC;
      
      cwLogInfo("Evaluating section:%s",cwStringNullGuard(section->section_id));

      // calc the location times
      _section_loc_time_eval( p, section );

      // evaluate the dynamics across all notes in the section
      _section_dynamics_eval( p, section );

      // evaluate the chords in this section
      _section_chord_eval(p,section);

      // evaluate the beat groups
      _section_beat_group_eval(p,section);

      // evaluate the grace groups
      _section_grace_group_eval(p,section);

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Section evaluation failed on '%s'.",cwStringNullGuard(section->section_id));
      
      return rc;
    }
    
  }
}

cw::rc_t cw::gutim_meas::create( handle_t& hRef, const char* group_info_json_fname, const char* vel_table_fname, const char* vel_table_name )  
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  gutim_meas_t* p = mem::allocZ<gutim_meas_t>();

  if((rc = _parse_cfg_file(p, group_info_json_fname )) != kOkRC )
  {
    goto errLabel;
  }

  if((rc = _parse_vel_table(p, vel_table_fname, vel_table_name )) != kOkRC )
  {
    goto errLabel;
  }
  
  hRef.set(p);

  reset(hRef);
  
errLabel:
  if(rc != kOkRC )
    _destroy(p);
  
  return rc;
}

cw::rc_t cw::gutim_meas::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if(!hRef.isValid())
  {
    return rc;
  }

  gutim_meas_t* p = _handleToPtr(hRef);
  _destroy(p);
  hRef.clear();
  return rc;
}


namespace cw
{
  namespace gutim_meas
  {
    void _section_reset( section_t* section )
    {
      section->eval_fl = false;
      
      for(chord_group_t* cg=section->chordGroupL; cg!=nullptr; cg=cg->link)
        cg->eval_fl = false;

      for(beat_group_t* bg=section->beatGroupL; bg!=nullptr; bg=bg->link)
        bg->eval_fl = false;

      for(grace_group_t* gg=section->graceGroupL; gg!=nullptr; gg=gg->link)
        gg->eval_fl = false;
      
    }
  }
}

cw::rc_t cw::gutim_meas::reset( handle_t h )
{
  rc_t          rc  = kOkRC;
  gutim_meas_t* p   = _handleToPtr(h);

  p->next_eval_section = p->sectionN>0 ? p->sectionA : nullptr;
  p->next_done_section = p->next_eval_section;
  p->pni_cnt = 0;
  
  for(unsigned i=0; i<p->noteN; ++i)
  {
    p->noteA[i].perf_fl  = false;
    p->noteA[i].perf_sec = -1;
    p->noteA[i].perf_dyn = 0;
  }

  for(unsigned i=0; i<p->perfNoteAllocN; ++i)
  {
    p->perfNoteA[i].perf_note_idx  = kInvalidIdx;
  }
  
  
  for(unsigned i=0; i<p->locAllocN; ++i)
  {
    p->locA[i].eval_fl = false;
  }

  for(unsigned i=0; i<p->sectionN; ++i)
    _section_reset(p->sectionA + i );

errLabel:
  return rc;
}

cw::rc_t cw::gutim_meas::set_current_section( handle_t h, unsigned beg_loc_id, unsigned end_loc_id )
{
  rc_t          rc  = kOkRC;
  gutim_meas_t* p   = _handleToPtr(h);

  // sanity check the incoming beg/end location id's
  if( beg_loc_id == kInvalidId || end_loc_id == kInvalidId || beg_loc_id >= p->locAllocN || end_loc_id >= p->locAllocN )
  {
    rc = cwLogError(kInvalidArgRC,"The begin (%i) or end (%i) location id is invalid or outside the range (%i) of the score.",beg_loc_id,end_loc_id,p->locAllocN);
    goto errLabel;    
  }

  // submit all cached notes to their respective sections
  if((rc = _submit_cached_notes(p)) != kOkRC )
  {
    // we're not going to fail if _submit_cached_notes_fails()
    // goto errLabel;
  }

  
  cwLogInfo("Gutim measurement: New section: locs:%i %i.",beg_loc_id,end_loc_id);
  if( p->next_eval_section == nullptr )
  {
    cwLogInfo("No next eval section.");
  }
  else
  {
    cwLogInfo("Next eval section:%s (loc:%i %i).",cwStringNullGuard(p->next_eval_section->section_id),p->next_eval_section->beg_loc_id,p->next_eval_section->end_loc_id);
  }

  // check for sections that are complete
  while( p->next_eval_section != nullptr && p->next_eval_section->end_loc_id < beg_loc_id )
  {
    if((rc = _section_eval(p,p->next_eval_section )) != kOkRC )
    {
      goto errLabel;
    }

    if( p->next_done_section == nullptr )
      p->next_done_section = p->next_eval_section;
    
    p->next_eval_section = p->next_eval_section->section_index + 1 >= p->sectionN ? nullptr : p->sectionA + p->next_eval_section->section_index + 1;
  }
  
errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"set section failed.");
  
  return rc;
  
}
cw::rc_t cw::gutim_meas::on_note( handle_t h, unsigned perf_note_idx, unsigned loc_id, double sec, unsigned midi_pitch, unsigned midi_vel )
{
  rc_t          rc        = kOkRC;
  gutim_meas_t* p         = _handleToPtr(h);
  perf_note_t*  perf_note = nullptr;
  unsigned      pni       = kInvalidIdx;

  //printf("pni:%i loc:%i sec:%f %i %i\n",perf_note_idx,loc_id,sec,midi_pitch,midi_vel);

  // validate loc_id
  if( loc_id == kInvalidId )
  {
    rc = cwLogError(kInvalidArgRC,"The GUTIM meas. object only accepts notes with valid 'loc' id's.");
    goto errLabel;
  }
  
  if( perf_note_idx >= p->perfNoteAllocN )
  {
    rc = cwLogError(kInvalidStateRC,"The perf-note index (%i) is greater than the range of the perf-note cache (%i).",perf_note_idx,p->perfNoteAllocN);
    goto errLabel;
  }
  
  perf_note = p->perfNoteA + perf_note_idx;

  perf_note->perf_note_idx = perf_note_idx;
  perf_note->loc_id        = loc_id;
  perf_note->sec           = sec;
  perf_note->midi_pitch    = midi_pitch;
  perf_note->midi_velocity = midi_vel;

  if( perf_note_idx >=p->pni_cnt )
    p->pni_cnt = perf_note_idx + 1;
  
errLabel:
  return rc;
}

bool cw::gutim_meas::is_section_complete( handle_t h )
{
  rc_t          rc = kOkRC;
  gutim_meas_t* p  = _handleToPtr(h);

  return p->next_done_section != nullptr && p->next_done_section->eval_fl;
}



cw::rc_t cw::gutim_meas::get_results( handle_t h, results_t*& results_ref )
{
  rc_t          rc = kOkRC;
  gutim_meas_t* p  = _handleToPtr(h);

  results_ref = nullptr;

  if( p->next_done_section == nullptr || p->next_done_section->eval_fl==false )
  {
    cwLogWarning("No sections ready for measurement analysis.");
    return kOkRC;
  }
     
  results_ref = &p->next_done_section->results;

  p->next_done_section = p->next_done_section->section_index + 1 >= p->sectionN ? nullptr : p->sectionA + p->next_done_section->section_index + 1;
  
errLabel:
  
  return rc;
}

cw::rc_t cw::gutim_meas::report( handle_t h )
{
  rc_t          rc = kOkRC;
  gutim_meas_t* p  = _handleToPtr(h);

  cwLogPrint("Notes submitted:%i mismatch-overwrite:%i",p->submitted_note_cnt,p->mismatch_overwrite_cnt);
  
  cwLogPrint("Vel-to-Dyn:\n");
  for(unsigned i=0; i<midi::kMidiVelCnt; ++i)
    cwLogPrint("%3i %3i\n",i,p->vel_to_dynA[i]);

  for(unsigned sect_idx=0; sect_idx<p->sectionN; ++sect_idx)
  {
    const section_t* s = p->sectionA + sect_idx;
    
    cwLogPrint("section: %3i %s loc:(%i to %i) score:( dur:%8.2f bpm:%6.2f )\n", s->section_index, s->section_id, s->beg_loc_id, s->end_loc_id, s->dur_sec, s->score_bpm_estimate );

    unsigned i = 0;
    for(const chord_group_t* cg=s->chordGroupL; cg!=nullptr; cg=cg->link,++i)
    {
      cwLogPrint("  chord: %i of %i : loc:%i notes: ",i,s->chordGroupN,cg->loc_id);

      if( cg->noteL == nullptr )
      {
        cwLogPrint("No-notes\n");
      }
      else
      {
        unsigned j=0;
        for(const note_t* n=cg->noteL; n!=nullptr;  n=n->chord_link,++j)
        {   
          cwLogPrint("%3i ",n->pitch);
          assert(n->loc_id == cg->loc_id);
        }      
        assert(j == cg->noteN );
        cwLogPrint("\n");
      }
    }
    assert(i==s->chordGroupN);

    i = 0;
    for(const beat_group_t* bg=s->beatGroupL; bg!=nullptr; bg=bg->link,++i)
    {
      cwLogPrint("  beat : %i of %i : score:( dur:%8.2f period:%8.2f ) : ", i, s->beatGroupN, bg->score_dur_sec, bg->score_period_sec);

      if( bg->locL == nullptr )
      {
        cwLogPrint("No-locs\n");
      }
      else
      {
        unsigned j =0;
        for(const loc_t* loc=bg->locL; loc!=nullptr; loc=loc->beat_link,++j)
        {
          cwLogPrint("%4i ",loc->loc_id);
        }
        assert(j==bg->locN);        
        cwLogPrint("\n");
      }
    }
    assert(i==s->beatGroupN);

    i = 0;
    for(const grace_group_t* gg=s->graceGroupL; gg!=nullptr; gg=gg->link,++i)
    {
      cwLogPrint("  grace: %i of %i : score:( dur:%8.2f period:%8.2f ) : ", i, s->graceGroupN, gg->score_dur_sec, gg->score_period_sec);

      if( gg->locL == nullptr )
      {
        cwLogPrint("No-locs\n");
      }
      else
      {
        unsigned j =0;
        for(const loc_t* loc=gg->locL; loc!=nullptr; loc=loc->grace_link,++j)
        {
          cwLogPrint("%4i ",loc->loc_id);
        }
        assert(j==gg->locN);        
        cwLogPrint("\n");
      }
    }
    assert(i==s->graceGroupN);
    
  } 
  
  return rc;
}
