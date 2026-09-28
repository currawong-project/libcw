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

namespace cw
{
  namespace gutim_meas
  {
    typedef struct note_str
    {
      const char* note_id;
      unsigned    loc_id;
      unsigned    pitch;
      int         score_dyn;

      bool     perf_fl;
      double   perf_sec;
      int      perf_dyn;

            struct note_str* loc_link;
      const struct note_str* chord_link;
    } note_t;

    typedef struct loc_str
    {
      struct section_str* section;   // this loc's section
      unsigned            meas;      // measure number
      double              score_sec; // location score time
      note_t*             noteL;     // list of notes associated with this location
      unsigned            noteN;     // count of notes at this location
      double              dur_pct;   // location time as percentage of total duration
      
      struct loc_str* beat_link;   // beat_group->locL link
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

      bool   spread_dev_valid_fl; 
      double spread_dev; 
    } chord_group_t;

    typedef struct beat_group_str
    {
      loc_t*    locL;              // location linked list
      unsigned  locN;              // count of locations in the list
      double    score_dur_sec;     // total dur. of the scored group in seconds
      double    score_period_sec;  // scored beat period in seconds
      struct beat_group_str* link; // section.beatGroupL link

      bool   eval_fl;
      double period_est_sec;
      double period_dev_est_sec;
      double dur_est_sec;
      
    } beat_group_t;

    typedef struct grace_group_str
    {
      loc_t*   locL;                // location linked list
      unsigned locN;                // count of locations in the list
      double   score_dur_sec;       // grace group score duration in seconds
      double   score_period_sec;    // grace group score period in seconds
      struct grace_group_str* link; // section.graceGroupL link

      bool   eval_fl;
      double period_est_sec;
      double period_dev_est_sec;
      double dur_est_sec;
      
    } grace_group_t;
    
    typedef struct section_str
    {
      const char*    section_id;    // section id
      unsigned       section_index; // section index in gutim_meas_t.sectionA[]
      unsigned       beg_loc_id;    // first loc in this section
      unsigned       end_loc_id;    // last loc in this section
      
      double         score_bpm_estimate; // score BPM
      double         dur_sec;      // score section duration in seconds
      
      chord_group_t* chordGroupL; 
      unsigned       chordGroupN;  // count of chords in this seciton

      beat_group_t*  beatGroupL;
      unsigned       beatGroupN;   // count of beat groups in this section

      grace_group_t* graceGroupL;
      unsigned       graceGroupN;  // ground of grace note groups in this section

      bool eval_fl;
      unsigned missing_loc_cnt; // count of missing locations
      int mean_dyn;             // mean performed dynamic for this section
      int mean_score_dev_dyn;   // mean deviation from the score for secion
      
    } section_t;
    
    typedef struct gutim_meas_str
    {
      object_t* file_cfg;
      
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

      section_t* last_perf_section;   // section containing the last performed locations
      section_t* ready_perf_section;  // non-null if this section is ready for evaluation
      
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

      mem::release(p->chordGroupA);
      mem::release(p->beatGroupA);
      mem::release(p->graceGroupA);
      mem::release(p->locA);
      mem::release(p->noteA);
      mem::release(p->sectionA);
    }

    bool _pair_validate( const object_t* pair )
    {
      return pair->is_pair() && textLength(pair->pair_label()) > 0 && pair->pair_value()!=nullptr && pair->pair_value()->is_dict();
    }

    rc_t _accum_note_count( const object_t* all_cfg, unsigned& note_cnt_ref )
    {
      rc_t     rc   = kOkRC;
      unsigned allN = all_cfg->child_count();
      
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

        // accumulate the count of notes in this section into p->noteAllocN
        if((rc = _accum_note_count( all_cfg, p->noteAllocN )) != kOkRC )
        {
          goto errLabel;
        }

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

            if((rc = note->getv("pitch",p->noteA[p->noteN].pitch,
                                "dlevel",p->noteA[p->noteN].score_dyn)) != kOkRC )
            {
              rc = cwLogError(rc,"Note dict field access failed on loc dict. on section index %i all index %i note index %i.",sect_idx,all_idx,ni);
              goto errLabel;
            }

            p->noteA[p->noteN].note_id = note_pair->pair_label();
            p->noteA[p->noteN].loc_id  = loc_id;

            //printf("    pitch:%i dlevel:%i id:%s\n",p->noteA[p->noteN].pitch,p->noteA[p->noteN].score_dyn,p->noteA[p->noteN].note_id);
            
            p->noteN += 1;
            
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

            // link the note on to the chord note list
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
        unsigned        beat_locN      = 0;
        beat_group_t*   new_beat_group = nullptr;

        // verify that there are beat group records available
        if( p->beatGroupN >= p->beatGroupAllocN )
        {
          rc = cwLogError(kSyntaxErrorRC,"The beat group array is full.");
          goto errLabel;
        }

        // get the next empty beat group recd
        new_beat_group = p->beatGroupA + p->beatGroupN;
        p->beatGroupN += 1;

        // link the beat group into the section
        new_beat_group->locN += 1;
        new_beat_group->link  = section->beatGroupL;
        section->beatGroupL   = new_beat_group;

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

        for(unsigned beat_loc_idx=0; beat_loc_idx<beat_locN; ++beat_loc_idx)
        {
          const object_t* beat_loc_pair  = beat_id_dict->child_ele(beat_loc_idx);
          const object_t* beat_note_list = nullptr;
          unsigned        noteN           = 0;
          unsigned        loc_id          = kInvalidId;

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

          p->locA[ loc_id ].beat_link = new_beat_group->locL;
          new_beat_group->locL = p->locA + loc_id;
            
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

    rc_t _calc_grace_group_meas( grace_group_t* grace_group )
    {
      rc_t rc = kOkRC;
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
        unsigned        grace_locN      = 0;
        grace_group_t*  new_grace_group = nullptr;

        // verify that there are grace group records available
        if( p->graceGroupN >= p->graceGroupAllocN )
        {
          rc = cwLogError(kSyntaxErrorRC,"The grace group array is full.");
          goto errLabel;
        }

        // get the next empty grace group recd
        new_grace_group = p->graceGroupA + p->graceGroupN;
        p->graceGroupN += 1;

        // link the grace group into the section
        new_grace_group->locN += 1;
        new_grace_group->link  = section->graceGroupL;
        section->graceGroupL   = new_grace_group;

        // validate the grace-id pair
        if(!_pair_validate(grace_id_pair))
        {
          rc = cwLogError(kSyntaxErrorRC,"The grace-id pair is not valid at grace_id index %i.",grace_id_idx);
          goto errLabel;
        }

        // get the grace-id dict. 
        if((rc = grace_id_pair->pair_value()->value(grace_id_dict)) != kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"The grace-id dict is not valid at grace_id index %i.",grace_id_idx);
          goto errLabel;          
        }

        for(unsigned grace_loc_idx=0; grace_loc_idx<grace_locN; ++grace_loc_idx)
        {
          const object_t* grace_loc_pair  = grace_id_dict->child_ele(grace_loc_idx);
          const object_t* grace_note_list = nullptr;
          unsigned        noteN           = 0;
          unsigned        loc_id          = kInvalidId;

          // verify that the note-loc pair is valid
          if( textLength(grace_loc_pair->pair_label()) == 0 || grace_loc_pair->pair_value()==nullptr || !grace_loc_pair->pair_value()->is_list())
          {
            rc = cwLogError(kSyntaxErrorRC,"grace-loc pair is not valid at grace index %i. : %i %i %i : %s",grace_loc_idx, textLength(grace_loc_pair->pair_label())==0,grace_loc_pair->pair_value()==nullptr, !grace_loc_pair->pair_value()->is_list(), grace_loc_pair->pair_label());
            goto errLabel;
          }

          // verify that the note list is a list
          if((rc = grace_loc_pair->pair_value()->value(grace_note_list)) != kOkRC )
          {
            rc = cwLogError(kSyntaxErrorRC,"grace-loc pair value is not a list at grace index %i.",grace_loc_idx);
            goto errLabel;
          }

          // validate the location id
          if(string_to_number(grace_loc_pair->pair_label(),loc_id) != kOkRC || loc_id == kInvalidId || loc_id >= p->locAllocN )
          {
            rc = cwLogError(kSyntaxErrorRC,"grace-loc location is not valid at grace index %i.",grace_loc_idx);
            goto errLabel;            
          }

          p->locA[ loc_id ].grace_link = new_grace_group->locL;
          new_grace_group->locL = p->locA + loc_id;
            
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

        // calculate the scored beat group duration and period
        if((rc = _calc_grace_group_duration_and_period( new_grace_group )) != kOkRC )
        {
          goto errLabel;
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

    rc_t _parse_cfg_fname( gutim_meas_t* p, const char* fname )
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
        rc = cwLogError(rc,"Section parse 1 failed.");
        goto errLabel;
      }

      cwLogInfo("%i sections %i locs %i notes.",p->sectionN,p->locAllocN,p->noteAllocN);
      
      p->locA  = mem::allocZ<loc_t>(p->locAllocN);
      p->noteA = mem::allocZ<note_t>(p->noteAllocN);

      for(unsigned i=0; i<p->noteAllocN; ++i)
        p->noteA[i].loc_id = kInvalidId;
      
      // fill in p->locA[] and p->noteA[]
      if((rc = _parse_sections_pass_2(p, p->file_cfg )) != kOkRC )
      {
        rc = cwLogError(rc,"Section parse 2 failed.");
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
      
    errLabel:

      if( rc != kOkRC )
        rc = cwLogError(rc,"GUTIM meas. cfg file parse failed on '%s'.",cwStringNullGuard(fname));

      return rc;
    }

    void _calc_chord_spread(gutim_meas_t* p, chord_group_t* cg )
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

        cg->spread_dev_valid_fl = true;
        cg->spread_dev          = acc/n;
      }
    }

    void _calc_loc_time( gutim_meas_t* p, section_t* section )
    {
      // for each location in this section
      for(unsigned loc_id=section->beg_loc_id; loc_id<=section->end_loc_id; ++loc_id)
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
          }
        }
        
      }
    }

    void _eval_dynamics( gutim_meas_t* p, section_t* section )
    {
      int      acc = 0;
      int      d_acc = 0;
      unsigned n = 0;

      section->mean_dyn = 0;
      section->mean_score_dev_dyn = 0;
      
      // for each location in this section
      for(unsigned loc_id=section->beg_loc_id; loc_id<=section->end_loc_id; ++loc_id)
      {
        loc_t*   loc = p->locA + loc_id;

        // for each note at this location
        for(const note_t* note=loc->noteL; note!=nullptr; note=note->loc_link)
          if( note->perf_fl )
          {
            acc   += note->perf_dyn;
            d_acc += abs(note->perf_dyn - note->score_dyn);
            n += 1;
          }
      }

      if( n > 0 )
      {
        section->eval_fl = true;
        section->mean_dyn     = acc/n;
        section->mean_score_dev_dyn = d_acc/n;        
      }
      
    }


    typedef struct seq_ele_str
    {
      double sec;
      bool   perf_fl;
    } seq_ele_t;

    void _beat_group_eval(gutim_meas_t* p, beat_group_t* bg)
    {
      bg->eval_fl = false;
      
      if( bg->locN < 3 )
        return;
      
      seq_ele_t seqA[bg->locN];
      unsigned  seq_idx  = 0;
      unsigned  meas_n   = 0;
      double    min_sec  = -1;
      unsigned  min_idx  = kInvalidIdx;      
      double    max_sec  = -1;
      unsigned  max_idx  = kInvalidIdx;
      double    sec0     = -1;
      double    acc      = 0;
      
      // fill in seq_idx with measured values
      for(const loc_t* loc=bg->locL; seq_idx<bg->locN && loc!=nullptr; loc=loc->beat_link,++seq_idx)
      {
        bool in_order_fl = sec0==-1 || sec0 < loc->est_sec;

        // if this ele does not have a valid time or is out of time order
        if( !loc->eval_fl || !in_order_fl )
        {
          seqA[seq_idx].perf_fl = false;
        }
        else
        {
          seqA[seq_idx].perf_fl = true;
          seqA[seq_idx].sec     = loc->est_sec;
          sec0                  = loc->est_sec;
          meas_n += 1;

          if( min_idx == kInvalidIdx || loc->est_sec < min_sec )
          {
            min_sec = loc->est_sec;
            min_idx = seq_idx;
          }
          
          if( max_idx == kInvalidIdx || loc->est_sec > max_sec )
          {
            max_sec = loc->est_sec;
            max_idx = seq_idx;
          }
        }
      }

      // if there are less than two valid measurements then there is nothing to be done
      if( meas_n < 2 || min_idx==kInvalidIdx || max_idx==kInvalidIdx || min_idx >= max_idx || min_sec >= max_sec )
      {
        return;
      }

      // how many periods are there between the first and last measurement count
      unsigned period_cnt = max_idx - min_idx;
          
      // estimate a single period duration based on the measurements
      double period_est_sec = (max_sec - min_sec) / period_cnt;
      
      // if there are missing measurments - then fill in the missing values with estimates
      if( meas_n < bg->locN )
      {          
        // estimate the loc times between min and max
        for(unsigned i=min_idx+1; i<max_idx; ++i)
        {
          seqA[i].sec = min_sec + (i-min_idx) * period_est_sec;
          seqA[i].perf_fl = true;
        }
      }

      // seqA[] now has measured or estimated times in all positions between min_idx and max_idx
        
      acc  = 0.0;
      sec0 = seqA[min_idx].sec;
      for(unsigned i=min_idx+1; i<=max_idx; ++i)
      {
        assert( seqA[i].perf_fl && seqA[i-1].sec < seqA[i].sec );
          
        acc += seqA[i].sec - sec0;
        sec0 = seqA[i].sec;
      }

      // update the period estimate based on the the individual periods
      period_est_sec = acc/(max_idx-min_idx);
      
      acc = 0;
      sec0 = seqA[min_idx].sec;
      for(unsigned i=min_idx+1; i<=max_idx; ++i)
      {
        double dsec = seqA[i].sec - sec0;
        acc += fabs(dsec - period_est_sec);
        sec0 = seqA[i].sec;
      }

      // calc the deviation from the mean period 
      double dsec_dev_sec = acc/(max_idx-min_idx);

      double est_dur_sec = 0;
      if( min_idx > 0 )
        est_dur_sec += period_est_sec * (min_idx-1);

      est_dur_sec += max_sec - min_sec;

      if( max_idx < bg->locN-1 )
        est_dur_sec += period_est_sec * ((bg->locN-1) - max_idx);
        
      bg->eval_fl            = true;
      bg->period_est_sec     = period_est_sec;
      bg->period_dev_est_sec = dsec_dev_sec;
      bg->dur_est_sec        = est_dur_sec;
    }

    void _grace_group_eval(gutim_meas_t* p, grace_group_t* gg)
    {
      gg->eval_fl = false;
      
      if( gg->locN < 3 )
        return;
      
      seq_ele_t seqA[gg->locN];
      unsigned  seq_idx  = 0;
      unsigned  meas_n   = 0;
      double    min_sec  = -1;
      unsigned  min_idx  = kInvalidIdx;      
      double    max_sec  = -1;
      unsigned  max_idx  = kInvalidIdx;
      double    sec0     = -1;
      double    acc      = 0;
      
      // fill in seq_idx with measured values
      for(const loc_t* loc=gg->locL; seq_idx<gg->locN && loc!=nullptr; loc=loc->grace_link,++seq_idx)
      {
        bool in_order_fl = sec0==-1 || sec0 < loc->est_sec;

        // if this ele does not have a valid time or is out of time order
        if( !loc->eval_fl || !in_order_fl )
        {
          seqA[seq_idx].perf_fl = false;
        }
        else
        {
          seqA[seq_idx].perf_fl = true;
          seqA[seq_idx].sec     = loc->est_sec;
          sec0                  = loc->est_sec;
          meas_n += 1;

          if( min_idx == kInvalidIdx || loc->est_sec < min_sec )
          {
            min_sec = loc->est_sec;
            min_idx = seq_idx;
          }
          
          if( max_idx == kInvalidIdx || loc->est_sec > max_sec )
          {
            max_sec = loc->est_sec;
            max_idx = seq_idx;
          }
        }
      }

      // if there are less than two valid measurements then there is nothing to be done
      if( meas_n < 2 || min_idx==kInvalidIdx || max_idx==kInvalidIdx || min_idx >= max_idx || min_sec >= max_sec )
      {
        return;
      }

      // how many periods are there between the first and last measurement count
      unsigned period_cnt = max_idx - min_idx;
          
      // estimate a single period duration based on the measurements
      double period_est_sec = (max_sec - min_sec) / period_cnt;
      
      // if there are missing measurments - then fill in the missing values with estimates
      if( meas_n < gg->locN )
      {          
        // estimate the loc times between min and max
        for(unsigned i=min_idx+1; i<max_idx; ++i)
        {
          seqA[i].sec = min_sec + (i-min_idx) * period_est_sec;
          seqA[i].perf_fl = true;
        }
      }

      // seqA[] now has measured or estimated times in all positions between min_idx and max_idx
        
      acc  = 0.0;
      sec0 = seqA[min_idx].sec;
      for(unsigned i=min_idx+1; i<=max_idx; ++i)
      {
        assert( seqA[i].perf_fl && seqA[i-1].sec < seqA[i].sec );
          
        acc += seqA[i].sec - sec0;
        sec0 = seqA[i].sec;
      }

      // update the period estimate based on the the individual periods
      period_est_sec = acc/(max_idx-min_idx);
      
      acc = 0;
      sec0 = seqA[min_idx].sec;
      for(unsigned i=min_idx+1; i<=max_idx; ++i)
      {
        double dsec = seqA[i].sec - sec0;
        acc += fabs(dsec - period_est_sec);
        sec0 = seqA[i].sec;
      }

      // calc the deviation from the mean period 
      double dsec_dev_sec = acc/(max_idx-min_idx);

      double est_dur_sec = 0;
      if( min_idx > 0 )
        est_dur_sec += period_est_sec * (min_idx-1);

      est_dur_sec += max_sec - min_sec;

      if( max_idx < gg->locN-1 )
        est_dur_sec += period_est_sec * ((gg->locN-1) - max_idx);
        
      gg->eval_fl            = true;
      gg->period_est_sec     = period_est_sec;
      gg->period_dev_est_sec = dsec_dev_sec;
      gg->dur_est_sec        = est_dur_sec;
    }
    

    rc_t _section_eval(gutim_meas_t* p, section_t* section, results_t* results )
    {
      rc_t rc = kOkRC;

      // calc the location times
      _calc_loc_time( p, section );

      _eval_dynamics( p, section );
      
      
      // for each chord group in this section
      for(chord_group_t* cg=section->chordGroupL; cg!=nullptr; cg=cg->link)
      {
        _calc_chord_spread(p,cg);
      }

      // for each beat group
      for(beat_group_t* bg=section->beatGroupL; bg!=nullptr; bg=bg->link)
      {
        _beat_group_eval(p,bg);
      }

      // for each grace group
      for(grace_group_t* gg=section->graceGroupL; gg!=nullptr; gg=gg->link)
      {
        _grace_group_eval(p,gg);
      }

      
      

    errLabel:
      return rc;
    }
    
    
  }
}

cw::rc_t cw::gutim_meas::create( handle_t& hRef, const char* group_info_json_fname )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  gutim_meas_t* p = mem::allocZ<gutim_meas_t>();

  if((rc = _parse_cfg_fname(p, group_info_json_fname )) != kOkRC )
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
  mem::release(p);
  hRef.clear();
  return rc;
}


namespace cw
{
  namespace gutim_meas
  {
    void _section_reset( section_t* section )
    {
      for(chord_group_t* cg=section->chordGroupL; cg!=nullptr; cg=cg->link)
        cg->spread_dev_valid_fl = false;

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

  p->ready_perf_section = nullptr;
  p->last_perf_section  = nullptr;
  
  for(unsigned i=0; i<p->noteN; ++i)
  {
    p->noteA[i].perf_fl  = false;
    p->noteA[i].perf_sec = -1;
    p->noteA[i].perf_dyn = 0;
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

cw::rc_t cw::gutim_meas::on_note( handle_t h, unsigned loc_id, double sec, unsigned midi_pitch, unsigned midi_vel )
{
  rc_t          rc  = kOkRC;
  gutim_meas_t* p   = _handleToPtr(h);
  loc_t*        loc = nullptr;

  if( loc_id == kInvalidId )
    return rc;

  if( loc_id >= p->locAllocN )
  {
    rc = cwLogError(kInvalidArgRC,"The loc. id %i is out of range %i.",loc_id,p->locAllocN);
    goto errLabel;
  }

  
  loc = p->locA + loc_id;
  
  // for each note at the performed location
  for(note_t* note = loc->noteL; note!=nullptr; note=note->loc_link)
  {
    // if this is the pitch of interest
    if( note->pitch == midi_pitch )
    {
      // set the performance measurements
      note->perf_fl  = true;
      note->perf_sec = sec;
      note->perf_dyn = midi_vel;

      // if there is no valid 'last section' then make this the 'last section'
      if( p->last_perf_section == nullptr )
      {
        p->last_perf_section = loc->section;
        p->ready_perf_section = nullptr;
      }
      else 
      {
        // if this performed location is not in the same section as the last performed location then we are changing sections
        if( p->last_perf_section != loc->section && p->last_perf_section->section_index+1 == loc->section->section_index )
        {
          p->ready_perf_section = p->last_perf_section;
          p->last_perf_section  = loc->section;
        }
      }
      return kOkRC;
    }
  }
  rc = cwLogError(kInvalidStateRC,"The pitch %i was not found at the location %i.",midi_pitch,loc_id);
  
errLabel:
  return rc;
}

bool cw::gutim_meas::is_section_complete( handle_t h )
{
  rc_t          rc = kOkRC;
  gutim_meas_t* p  = _handleToPtr(h);

  return p->ready_perf_section != nullptr;
}



cw::rc_t cw::gutim_meas::get_results( handle_t h, results_t& results_ref )
{
  rc_t          rc = kOkRC;
  gutim_meas_t* p  = _handleToPtr(h);

  if( p->ready_perf_section == nullptr )
  {
    cwLogWarning("No sections ready for measurement analysis.");
    return kOkRC;
  }

  if((rc = _section_eval(p,p->ready_perf_section,&results_ref)) != kOkRC )
  {
  }
     
  p->ready_perf_section = nullptr;
errLabel:
  
  return rc;
}
