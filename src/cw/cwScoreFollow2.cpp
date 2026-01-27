

//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwMidi.h"
#include "cwTime.h"
#include "cwFile.h"
#include "cwCsv.h"
#include "cwVectOps.h"
#include "cwPianoScore.h"
#include "cwScoreFollow2.h"

#define INVALID_SEC (-1.0)

namespace cw
{
  namespace score_follow_2
  {
    struct sf_str;
    
    typedef struct trkr_str
    {
      struct sf_str* sf;

      unsigned new_note_idx;
      
      bool end_fl;
      
      unsigned* note_match_cntA;  // note_match_cntA[ sf->noteN ]
      unsigned* loc_match_cntA;   // loc_match_cntA[ sf->locN ]
      float*    expV;             // expV[ sf->pitchN ]

      unsigned prv_loc_idx; // loc reported on the previous cycle
      unsigned exp_loc_idx; // expected location on this cycle
      
      unsigned search_bni;  // current search window 
      unsigned search_eni;  //

      double beg_score_sec; // score time of the first match
      double beg_perf_sec;  // perf time of the first match

      double   time_delta_sum;
      unsigned time_delta_cnt;
      double   time_fact;       // score_time * time_fact = perf_time

      double prv_match_perf_sec;
      unsigned decay_cnt;
      
    } trkr_t;
    
    typedef struct aff_str
    {
      unsigned loc_id;  // location where this affinity function should be applied    
      unsigned bni;     // pitch index associated with envA[0]
      float*   envA;    // envA[ envN ] 
      unsigned envN;    // count of pitch cells covered by envA[]
    } aff_t;

    typedef struct wnd_str
    {
      unsigned loc_id;  // location where this search window is used
      unsigned bni;     // window start pitch index
      unsigned eni;     // window end pitch index
    } wnd_t;
    
    typedef struct loc_str
    {
      unsigned  loc_id;     // id this location represents
      unsigned  meas_num;   // measure number of this location
      double    sec;        // score time in seconds
      unsigned* note_idxA; // note_idxA[ note_idxN ] indexes into noteA[] of notes starting at this location
      unsigned  note_idxN; // 
    } loc_t;

    typedef struct note_str
    {
      unsigned uid;        // unique id identifying this note
      unsigned loc_id;     // location this note falls on
      unsigned pitch;      // note of this note
      unsigned vel;        // velocity of this note
    } note_t;

    
    typedef struct sf_str
    {
      args_t    args;
      
      loc_t*    locA;           // locA[ locN ]
      unsigned* locMapA;        // locMapA[ max_loc_id ] maps loc_id to loc_index
      aff_t*    loc_affA;       // loc_affA[ locN ]
      wnd_t*    loc_wndA;       // loc_wndA[ locN ]
      unsigned  locAllocN;      //
      unsigned  locN;           // 
      unsigned  max_loc_id;     // 

      note_t*  noteA;       // noteA[ noteN ]
      unsigned noteAllocN;  //
      unsigned noteN;       //

      unsigned beg_loc_id;
      unsigned end_loc_id;

      result_t* resultA; // resultA[ resultN ]
      unsigned resultAllocN;
      unsigned resultN;

      trkr_t* trk;
      
    } sf_t;


    //================================================================================================================
    //
    // trk_t
    //
    
    void _trkr_destroy( trkr_t* trk )
    {
      if( trk != nullptr )
      {
        mem::release(trk->note_match_cntA);
        mem::release(trk->loc_match_cntA);
        mem::release(trk->expV);
        mem::release(trk);
      }
    }

    void _trkr_apply_affinity( trkr_t* trk, unsigned loc_id  )
    {
      assert( loc_id <= trk->sf->max_loc_id );

      unsigned loc_idx = trk->sf->locMapA[ loc_id ];
      const aff_t* a = trk->sf->loc_affA + loc_idx;

      assert( a->loc_id == loc_id );

      //printf("apply: %i %i : %i %i : decay:%i\n",loc_id,loc_idx,a->bni, a->bni + a->envN - 1, trk->decay_cnt);
      trk->decay_cnt = 0;

      for(unsigned i=0; i<a->envN; ++i)
        trk->expV[ a->bni + i ] += a->envA[i];
      
    }

    rc_t _trkr_reset( trkr_t* trk, unsigned beg_loc_id )
    {
      assert( beg_loc_id <= trk->sf->max_loc_id );

      trk->new_note_idx = 0;
      trk->end_fl = false;

      //printf("reset expV: %i\n",beg_loc_id);
      
      for(unsigned ni=0; ni<trk->sf->noteN; ++ni)
      {
        trk->note_match_cntA[ni] = 0;
        trk->expV[ni] = 0;
      }
      for(unsigned li=0; li<trk->sf->locN; ++li)
        trk->loc_match_cntA[li] = 0;
      
      trk->prv_loc_idx = kInvalidIdx;
      trk->exp_loc_idx = trk->sf->locMapA[ beg_loc_id ];
      
      trk->search_bni = kInvalidIdx;  // current search window 
      trk->search_eni = kInvalidIdx;  //

      trk->beg_score_sec = INVALID_SEC; // score time of the first match
      trk->beg_perf_sec  = INVALID_SEC;  // perf time of the first match

      trk->time_delta_sum = 0;
      trk->time_delta_cnt = 0;
      trk->time_fact      = 1.0; // score_time * time_fact = perf_time

      trk->prv_match_perf_sec = INVALID_SEC;

      _trkr_apply_affinity(trk,beg_loc_id);

      return kOkRC;
    }
    
    trkr_t* _trkr_create( sf_t* sf )
    {
      trkr_t* trk = mem::allocZ<trkr_t>();

      trk->sf = sf;
      trk->note_match_cntA = mem::allocZ<unsigned>(sf->noteN);
      trk->loc_match_cntA  = mem::allocZ<unsigned>(sf->locN);
      trk->expV            = mem::allocZ<float>(sf->noteN);

      _trkr_reset(trk,sf->locA[0].loc_id);

      return trk;
    }

    void _trkr_rpt_debug( trkr_t* trk, unsigned pitch, unsigned vel, unsigned loc_idx )
    {
      unsigned loc_id = trk->sf->locA[loc_idx].loc_id;

      printf("loc:%i pitch:%i vel:%i bni:%i eni:%i\n",loc_id,pitch,vel,trk->search_bni,trk->search_eni);
      
      for(unsigned ni=trk->search_bni; ni<=trk->search_eni; ++ni)
        printf("%s%4i%s ", trk->sf->noteA[ni].loc_id==loc_id ? "(":"", trk->note_match_cntA[ni], trk->sf->noteA[ni].loc_id==loc_id ? ")":"");
      printf("\n");

      for(unsigned ni=trk->search_bni; ni<=trk->search_eni; ++ni)
        printf("%s%4.2f%s ", trk->sf->noteA[ni].loc_id==loc_id ? "(":"", trk->expV[ni], trk->sf->noteA[ni].loc_id==loc_id ? ")":"");
      printf("\n");

      for(unsigned ni=trk->search_bni; ni<=trk->search_eni; ++ni)
        printf("%s%4i%s ", trk->sf->noteA[ni].loc_id==loc_id ? "(":"", trk->sf->noteA[ni].pitch, trk->sf->noteA[ni].loc_id==loc_id ? ")":"");
      printf("\n");

      
    }
    
    rc_t _trkr_on_new_note( trkr_t* trk, double sec, unsigned pitch, unsigned vel, bool rpt_fl, unsigned& matched_loc_id_ref, unsigned& meas_numb_ref, unsigned& score_vel_ref )
    {
      rc_t        rc                         = kOkRC;
      double      d_corr_sec                 = 0.0;
      bool        d_corr_sec_valid_fl        = false;
      double      d_match_score_sec          = 0.0;
      bool        d_match_score_sec_valid_fl = false;
      double      d_match_perf_sec           = 0.0;
      bool        d_match_perf_sec_valid_fl  = false;      
      int         d_loc_id                   = 0;
      bool        d_loc_id_valid_fl          = false;
      unsigned    prv_loc_id                 = trk->prv_loc_idx==kInvalidIdx  ? kInvalidIdx : trk->sf->locA[ trk->prv_loc_idx ].loc_id;
      unsigned    match_loc_id               = kInvalidId;
      unsigned    exp_loc_id                 = kInvalidId;
      const char* rpt_status                 = "";
      unsigned    match_ni                   = kInvalidIdx;
      double      match_val                  = 0;

      score_vel_ref = -1;
      matched_loc_id_ref = kInvalidId;
      meas_numb_ref      = kInvalidId;
      
      assert( trk->exp_loc_idx != kInvalidIdx && trk->exp_loc_idx < trk->sf->locN );
      
      trk->search_bni = trk->sf->loc_wndA[ trk->exp_loc_idx ].bni;
      trk->search_eni = trk->sf->loc_wndA[ trk->exp_loc_idx ].eni;

      // set 'match_ni' to the best match candidate
      for(unsigned ni=trk->search_bni; ni<=trk->search_eni; ++ni)
        if( trk->note_match_cntA[ni]==0 && trk->sf->noteA[ni].pitch==pitch && (match_ni==kInvalidIdx || trk->expV[ni]>match_val))
        {
          match_ni = ni;
          match_val = trk->expV[ni];
        }

      //if( pitch==33 )
      //  _trkr_rpt_debug(trk,pitch,vel,trk->exp_loc_idx);

      // if no match candidate was found
      if( match_ni == kInvalidIdx )
      {
        rpt_status = "spurious";
      }
      else
      {
        // get the loc_id that we expected to match
        exp_loc_id = trk->exp_loc_idx==kInvalidIdx  ? kInvalidIdx : trk->sf->locA[ trk->exp_loc_idx ].loc_id;

        // get attributes of the candidate match
        match_loc_id = trk->sf->noteA[match_ni].loc_id;
        unsigned match_loc_idx= trk->sf->locMapA[match_loc_id];
        double   match_sec    = trk->sf->locA[match_loc_idx].sec;

        // set d_loc_id to the diff between the matched loc and the expected match loc
        if( exp_loc_id != kInvalidIdx )
        {
          d_loc_id          = (int)match_loc_id - (int)exp_loc_id;
          d_loc_id_valid_fl = true;
        }

        // if this is the first match since a reset then record this as the first match
        // TODO: what  if this point gets rejected
        if( trk->beg_score_sec == INVALID_SEC )
        {
          trk->beg_score_sec = match_sec;
          trk->beg_perf_sec = sec;
        }

        // track the score time diff. between this match and the prev score match
        if( trk->prv_loc_idx!=kInvalidIdx )
        {
          // delta score match time between this match and prev match
          double prv_score_sec = trk->sf->locA[ trk->prv_loc_idx ].sec;
          d_match_score_sec = match_sec - prv_score_sec;
          d_match_score_sec_valid_fl = true;
        }

        // track the perf time diff. between this score and the prev perf match
        if( trk->prv_match_perf_sec != INVALID_SEC )
        {
          // delta perf time between this match and prev match
          d_match_perf_sec = sec - trk->prv_match_perf_sec;
          d_match_perf_sec_valid_fl = true;
        }

        // if there the delta score/perf match time's are valid then estimate the tempo corrected delta match time
        if( d_match_score_sec_valid_fl && d_match_perf_sec_valid_fl )
        {
          double d_corr_match_score_sec = d_match_score_sec / trk->time_fact;
          d_corr_sec = d_match_perf_sec - d_corr_match_score_sec;
          d_corr_sec_valid_fl = true;
        }

        // if this is a high confidence match then update the score->perf time tempo correction factor
        if( d_loc_id_valid_fl && 0 <= d_loc_id && d_loc_id < trk->sf->args.d_loc_stats_thresh )
        {
          if( sec - trk->beg_perf_sec > 0 && (match_sec - trk->beg_score_sec) > 0 )
          {
            trk->time_delta_sum += (match_sec - trk->beg_score_sec)/(sec - trk->beg_perf_sec);
            trk->time_delta_cnt += 1;
            trk->time_fact = trk->time_delta_sum / trk->time_delta_cnt;
          }          
        }

        // set the thresh flags that are violated
        bool lo_time_thresh_fl = d_corr_sec_valid_fl && fabs(d_corr_sec) > trk->sf->args.d_sec_err_thresh_lo;
        bool hi_time_thresh_fl = d_loc_id_valid_fl && d_loc_id>0 && d_corr_sec_valid_fl && fabs(d_corr_sec) > trk->sf->args.d_sec_err_thresh_hi;
        bool lo_loc_thresh_fl  = d_loc_id_valid_fl && abs(d_loc_id) > trk->sf->args.d_loc_thresh_lo;
        bool hi_loc_thresh_fl  = d_loc_id_valid_fl && abs(d_loc_id) > trk->sf->args.d_loc_thresh_hi;
        
        // if this match breaks the threshold rules  ....
        if( (lo_time_thresh_fl && lo_loc_thresh_fl) || hi_loc_thresh_fl || hi_time_thresh_fl )
        {          
          match_ni = kInvalidIdx;   // ... then reject the match
          rpt_status = "rejected";
        }
        else
        {
          // ... the match was accepted
          
          trk->prv_match_perf_sec               = sec;
          trk->prv_loc_idx                      = match_loc_idx;
          trk->loc_match_cntA[ match_loc_idx ] += 1;
          trk->note_match_cntA[ match_ni ]     += 1;

          score_vel_ref = trk->sf->noteA[ match_ni ].vel;
          matched_loc_id_ref = match_loc_id;
          meas_numb_ref = trk->sf->locA[ match_loc_idx ].meas_num;
          
          // notice if we arrived at the end of the score tracking range
          if( match_loc_id >= trk->sf->end_loc_id )
          {
            trk->end_fl = true;
          }
          else
          {
            
            // select the next expected location by advancing to the next location that has not yet been matched
            unsigned exp_loc_idx = match_loc_idx;
            while( exp_loc_idx < trk->sf->locN && trk->loc_match_cntA[exp_loc_idx] >= trk->sf->locA[exp_loc_idx].note_idxN)
              ++exp_loc_idx;

            // if we arrived at the end of the score 
            if( exp_loc_idx >= trk->sf->locN )
              trk->end_fl = true;
            else
            {
              
              exp_loc_id = trk->sf->locA[ exp_loc_idx ].loc_id;

              // apply the affinity envelope at the exected location
              _trkr_apply_affinity(trk,exp_loc_id);

              // update the expected loc. for the next cycle
              trk->exp_loc_idx = exp_loc_idx;
            }              
          }          
        }
      }

      if( rpt_fl )
      {

        printf("%4i pitch:%3i ",trk->new_note_idx, pitch);
        if( prv_loc_id != kInvalidIdx )
          printf("LOC: prv:%4i ",prv_loc_id);
        else
          printf("LOC: prv:     ");

        if( match_loc_id != kInvalidIdx )
          printf("match:%4i ",match_loc_id);
        else
          printf("match:     ");

        if( exp_loc_id != kInvalidIdx )
          printf("exp:%4i ",exp_loc_id);
        else
          printf("exp:     ");
        
        if( d_loc_id_valid_fl )
          printf(": dLoc:%4i ",d_loc_id);
        else
          printf(": dLoc:     ");

        
        if( d_match_score_sec_valid_fl )
          printf("| Match dsec score:%6.3f ",d_match_score_sec);
        else
          printf("| Match dsec score:       ");

        if( d_match_perf_sec_valid_fl )
          printf("perf:%6.3f ",d_match_perf_sec);
        else
          printf("perf:      ");
        
        if( d_corr_sec_valid_fl )
          printf("corr:%6.3f ",d_corr_sec);
        else
          printf("corr:      ");

        //printf(" : (%f %i %f) : ",trk->time_delta_sum,trk->time_delta_cnt,trk->time_fact);
        
        printf("%s ",rpt_status);

        if( vel < 5 )
          printf("vel(%i) ",vel);

        printf("\n");
        
      }

      trk->new_note_idx+=1;
      return rc;
    }

    rc_t _trkr_do_decay( trkr_t* trk )
    {
      rc_t rc = kOkRC;

      if( trk->search_bni != kInvalidIdx && trk->search_eni != kInvalidIdx )
      {
        //printf("decay: %i %i\n",trk->search_bni, trk->search_eni);
        trk->decay_cnt += 1;
        
        for(unsigned ni=trk->search_bni; ni<=trk->search_eni; ++ni)
          trk->expV[ni] *= trk->sf->args.decay_coeff;
      }
      return rc;
    }

    //==========================================================================================================
    //
    //
    //
    
    sf_t* _handleToPtr(handle_t h)
    {
      return handleToPtr<handle_t,sf_t>(h);
    }
    
    rc_t _destroy( sf_t* p )
    {
      for(unsigned i=0; i<p->locN; ++i)
      {
        if( p->locA != nullptr )
          mem::release(p->locA[i].note_idxA);
        
        if( p->loc_affA != nullptr )
          mem::release(p->loc_affA[i].envA);
      }

      _trkr_destroy(p->trk);

      mem::release(p->resultA);
      mem::release(p->noteA);
      mem::release(p->locA);
      mem::release(p->locMapA);
      mem::release(p->loc_wndA);
      mem::release(p->loc_affA);
      mem::release(p);
      return kOkRC;
    }

    rc_t _get_loc_and_note_count( sf_t* p, const perf_score::handle_t& scoreH )
    {
      rc_t     rc      = kOkRC;
      unsigned loc_id0 = kInvalidId;
      
      p->locAllocN  = 0;
      p->noteAllocN = 0;
      p->max_loc_id = kInvalidId;

      // for each score event
      for(const perf_score::event_t* e = base_event(scoreH); e != nullptr; e=e->link)
      {
        // if this is a note-on
        if( midi::isNoteOn( e->status, e->d1 ) )
        {
          p->noteAllocN += 1;

          // if this is a new location
          if( loc_id0 == kInvalidId || loc_id0 != e->loc )
          {
            // if this is the max location id
            if( p->max_loc_id == kInvalidId || e->loc > p->max_loc_id )
              p->max_loc_id = e->loc;
          
            p->locAllocN += 1;
            
          }

          // check that location id's are in increasing order
          if( loc_id0 != kInvalidId && e->loc < loc_id0 )
          {
            rc = cwLogError(kInvalidStateRC,"The score location id's are not increasing. (%i < %i)",e->loc,loc_id0);
            goto errLabel;
          }
          
          loc_id0 = e->loc;
        }
      }

    errLabel:
      return rc;
    }

    rc_t _alloc_fill_loc_and_note_arrays( sf_t* p, const perf_score::handle_t& scoreH )
    {
      rc_t     rc      = kOkRC;
      unsigned loc_id0 = kInvalidId;
      unsigned uid     = 0;
      
      p->noteA   = mem::allocZ<note_t>(p->noteAllocN);
      p->locA    = mem::allocZ<loc_t>(p->locAllocN);
      p->locMapA = mem::allocZ<unsigned>(p->max_loc_id+1);
      p->locN    = 0;
      p->noteN   = 0;

      // for each score event
      for(const perf_score::event_t* e = base_event(scoreH); e != nullptr; e=e->link)
      {
        // if this is a note-on 
        if( midi::isNoteOn( e->status, e->d1 ) )
        {
          note_t* note = p->noteA + p->noteN;

          note->uid    = uid++;
          note->loc_id = e->loc;
          note->pitch  = e->d0;
          note->vel    = e->d1;
          
          p->noteN += 1;

          // if this is a new location
          if( loc_id0 == kInvalidId || loc_id0 != e->loc )
          {
            loc_t* loc = p->locA + p->locN;
          
            loc->loc_id   = e->loc;
            loc->meas_num = e->meas;
            loc->sec      = e->sec;

            // fill in the loc_id to loc_idx map
            p->locMapA[ e->loc ] = p->locN;

            p->locN += 1;
          }

          loc_id0 = e->loc;

        }        
      }
      
      return rc;
    }

    rc_t _alloc_and_fill_loc_note_arrays( sf_t* p )
    {
      rc_t     rc = kOkRC;
      unsigned ni = 0;
      
      for(unsigned li=0; li<p->locN; ++li)
      {
        unsigned n   = 0;
        unsigned bni = ni;
        
        // count the number of notes assigned to location loc[li]->loc_id
        for(; ni<p->noteN && p->noteA[ni].loc_id == p->locA[li].loc_id; ++ni )
          ++n;

        if( n == 0 )
        {
          rc = cwLogError(kInvalidStateRC,"No notes exist for score location '%i'.",p->locA[li].loc_id);
          goto errLabel;
        }
        else
        {
          // allocate this loc's pitch index array
          p->locA[li].note_idxA = mem::allocZ<unsigned>(n);
          p->locA[li].note_idxN = n;

          // fill the pitch index array
          for(unsigned i=0; i<n; ++i)
          {
            p->locA[li].note_idxA[i] = bni + i;

            assert( p->noteA[ bni+i ].loc_id == p->locA[li].loc_id );
          }
          
        }
      }

    errLabel:
      return rc;
    }

    rc_t _alloc_and_fill_search_wnd_array( sf_t* p, double pre_wnd_sec, double post_wnd_sec, unsigned min_loc_cnt )
    {
      rc_t rc = kOkRC;
      
      p->loc_wndA = mem::allocZ<wnd_t>(p->locN);

      for(unsigned li=0; li<p->locN; ++li)
      {
        int      bli  = li;
        unsigned eli  = li;
        unsigned locN = 0;
        
        // look backward for window start location
        while(bli-1 >= 0 &&  (p->locA[bli-1].sec >= p->locA[li].sec - pre_wnd_sec || locN<min_loc_cnt) )
        {
          bli -= 1;
          locN += 1;
        }

        // look forward for the window end location
        locN = 0;
        while(eli+1<p->locN && (p->locA[eli+1].sec <= p->locA[li].sec + post_wnd_sec || locN<min_loc_cnt) )
        {
          eli += 1;
          locN += 1;
        }
        // verfiy that notes exist for location 'bli' (this is a redundant check - see _alloc_and_fill_note_arrays()
        if( p->locA[bli].note_idxN == 0 )
        {
          rc = cwLogError(kInvalidStateRC,"No notes exist for score location '%i'.",p->locA[bli].loc_id);
          goto errLabel;
        }

        // verfiy that notes exist for location 'eli' (this is a redundant check - see _alloc_and_fill_note_arrays()
        if( p->locA[eli].note_idxN == 0 )
        {
          rc = cwLogError(kInvalidStateRC,"No notes exist for score location '%i'.",p->locA[eli].loc_id);
          goto errLabel;
        }

        // translate the begin/end locations to begin/end pitich indexes
        p->loc_wndA[li].bni    = p->locA[bli].note_idxA[0];
        p->loc_wndA[li].eni    = p->locA[eli].note_idxA[ p->locA[eli].note_idxN-1 ];
        p->loc_wndA[li].loc_id = p->locA[li].loc_id;
      }

    errLabel:
      return rc;
    }


    float _aff_func( sf_t* p, double t0, unsigned li, double wnd_dur_sec )
    {
      double t1 = p->locA[li].sec;
      double dt = std::max(t0,t1)-std::min(t0,t1);
      //assert( dt <= wnd_dur_sec );
      return (wnd_dur_sec-dt)/wnd_dur_sec; 
    }
    
    rc_t _alloc_and_fill_affinity_wnd_arrays( sf_t*p, double pre_aff_sec, double post_aff_sec, unsigned min_loc_cnt )
    {
      rc_t     rc      = kOkRC;
      unsigned locEnvN = 0;
      float*   locEnvV = nullptr;
      unsigned locN    = 0;

      p->loc_affA = mem::allocZ<aff_t>(p->locN);

      // for each location
      for(unsigned li = 0; li<p->locN; ++li)
      {
        int      bli = li;
        unsigned eli = li;
        double   t0  = p->locA[li].sec;
        unsigned bpi = kInvalidId;
        unsigned epi = kInvalidId;
        
        // look backward for window start location
        while(bli-1 >= 0 &&  (p->locA[bli-1].sec >= t0 - pre_aff_sec || locN < min_loc_cnt) )
        {
          bli  -= 1;
          locN += 1;
        }
        
        // look forward for the window end location
        locN = 0;
        while(eli+1<p->locN && (p->locA[eli+1].sec <= t0 + post_aff_sec || locN < min_loc_cnt) )
        {
          eli  += 1;
          locN += 1;
        }
        
        // calc the count of the loc's in the aff. env.
        locEnvN = (eli-bli) + 1;
        locEnvV = mem::resize<float>(locEnvV,locEnvN);

        // fill in the pre-aff wnd
        for(unsigned i = bli; i<li; ++i)
        {
          assert( bli <= (int)i && i-bli < locEnvN );
          locEnvV[i-bli] =  _aff_func(p,t0,i,pre_aff_sec);
        }
        // fill in the post-aff wnd
        for(unsigned i = li; i<=eli; ++i)
        {
          assert( bli<=(int)i && (li-bli)+(i-li) < locEnvN );          
          locEnvV[(li-bli)+(i-li)] = _aff_func(p,t0,i,post_aff_sec);
        }

        assert( p->locA[eli].note_idxN > 0 );
        
        // allocate the per-note envelope
        bpi = p->locA[bli].note_idxA[0];
        epi = p->locA[eli].note_idxA[ p->locA[eli].note_idxN-1 ];

        assert( bpi < p->noteN && epi < p->noteN && li < p->locN );

        // init. the aff. env. record
        p->loc_affA[ li ].loc_id = p->locA[li].loc_id;
        p->loc_affA[ li ].bni  = bpi;
        p->loc_affA[ li ].envN = (epi-bpi)+1;
        p->loc_affA[ li ].envA = mem::allocZ<float>(p->loc_affA[ li ].envN);

        // for each note that fall withing the aff. env
        for(unsigned pi = bpi; pi<=epi; ++pi)
        {
          // get the loc idx assoc with this note
          unsigned loc_idx = p->locMapA[ p->noteA[pi].loc_id ];

          // calc the loc. based envA[] index
          unsigned loc_env_i = loc_idx - bli;
          
          assert( loc_env_i < locEnvN );
          assert( pi-bpi < p->loc_affA[li].envN );

          p->loc_affA[li].envA[pi-bpi] = locEnvV[ loc_env_i ];
        } 
      }
    
      mem::release(locEnvV);
      return rc;
    }

    void _report_score( sf_t* p, unsigned beg_loc_id, unsigned end_loc_id )
    {
      for(unsigned ni=0; ni<p->noteN; ++ni)
        if(beg_loc_id <= p->noteA[ni].loc_id && p->noteA[ni].loc_id <= end_loc_id )
          printf("%4i %6.2f %3i\n", p->noteA[ni].loc_id, p->locA[ p->locMapA[ p->noteA[ni].loc_id ] ].sec, p->noteA[ni].pitch );
    }

    void _report_affinity( sf_t* p, unsigned N=10 )
    {
      for(unsigned i=0; i<N; ++i)
      {
        const aff_t& a = p->loc_affA[i];
        printf("%i %i %i [",a.loc_id,a.bni,a.envN);
        for(unsigned j=0; j<a.envN; ++j)
          printf("%f ",a.envA[j]);
        printf("]\n");
      }
    }
    
  }
}

cw::rc_t cw::score_follow_2::parse_args( const object_t* cfg, args_t& args )
{
  rc_t rc = kOkRC;

  if((rc                                                               = cfg->getv("rpt_fl",args.rpt_fl,
                     "pre_affinity_sec",    args.pre_affinity_sec,
                     "post_affinity_sec",   args.post_affinity_sec,
                     "pre_wnd_sec",         args.pre_wnd_sec,
                     "post_wnd_sec",        args.post_wnd_sec,
                     "decay_coeff",         args.decay_coeff,
                     "d_sec_err_thresh_lo", args.d_sec_err_thresh_lo,
                     "d_loc_thresh_lo",     args.d_loc_thresh_lo,
                     "d_sec_err_thresh_hi", args.d_sec_err_thresh_hi,
                     "d_loc_thresh_hi",     args.d_loc_thresh_hi,
                     "d_loc_stats_thresh",  args.d_loc_stats_thresh)) != kOkRC )
  {
    rc = cwLogError(rc,"SF2 parse args. failed.");
    goto errLabel;
  }
  
errLabel:  
  return rc;
}

cw::rc_t cw::score_follow_2::create( handle_t& hRef, const args_t& args, perf_score::handle_t scoreH )
{
  rc_t rc;
  if((rc = destroy(hRef)) != kOkRC )
    return rc;

  sf_t* p = mem::allocZ<sf_t>();

  if((rc = _get_loc_and_note_count( p, scoreH )) != kOkRC )
    goto errLabel;

  if((rc = _alloc_fill_loc_and_note_arrays( p, scoreH )) != kOkRC )
    goto errLabel;

  if((rc = _alloc_and_fill_loc_note_arrays( p )) != kOkRC )
    goto errLabel;

  if((rc = _alloc_and_fill_search_wnd_array( p, args.pre_wnd_sec, args.post_wnd_sec, args.min_wnd_loc_cnt )) != kOkRC )
    goto errLabel;

  if((rc = _alloc_and_fill_affinity_wnd_arrays(p, args.pre_affinity_sec, args.post_affinity_sec, args.min_affinity_loc_cnt )) != kOkRC )
    goto errLabel;

  p->trk = _trkr_create(p);

  p->args = args;
  
  p->resultAllocN = p->noteN*2;
  p->resultA      = mem::allocZ<result_t>(p->resultAllocN);
  p->resultN      = 0;

  //_report_affinity( p );

  
  hRef.set(p);
    
errLabel:
  if(rc != kOkRC )
  {
    rc = cwLogError(rc,"Score follower create failed.");
    _destroy(p);
  }

  return rc;
}

cw::rc_t cw::score_follow_2::destroy( handle_t& hRef )
{
  rc_t  rc = kOkRC;
  sf_t* p = nullptr;
  
  if( !hRef.isValid() )
    return rc;

  p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
  {
    rc = cwLogError(rc,"Score follow destroy failed.");
    goto errLabel;
  }

  hRef.clear();
  
errLabel:
  return rc;
}

cw::rc_t cw::score_follow_2::reset( handle_t h, unsigned beg_loc_id, unsigned end_loc_id )
{
  rc_t  rc = kOkRC;
  sf_t* p = _handleToPtr(h);

  if( beg_loc_id > p->max_loc_id )
  {
    rc = cwLogError(kInvalidArgRC,"An invalid location id (%i) was encountered.",beg_loc_id);
    goto errLabel;
  }

  //_report_score(p,beg_loc_id,end_loc_id);
  
  
  p->beg_loc_id = beg_loc_id;
  p->end_loc_id = end_loc_id;

  p->resultN = 0;

  _trkr_reset(p->trk,beg_loc_id);

  //cwLogInfo("SF2 reset: %i %i",beg_loc_id,end_loc_id);

errLabel:
  
  return rc;
}

cw::rc_t cw::score_follow_2::on_new_note( handle_t h, unsigned uid, double sec, uint8_t pitch, uint8_t vel, unsigned& matched_loc_id_ref, unsigned& meas_numb_ref, unsigned& score_vel_ref )
{
  rc_t  rc = kOkRC;
  sf_t* p  = _handleToPtr(h);
  
  _trkr_on_new_note(p->trk,sec,pitch,vel, p->args.rpt_fl, matched_loc_id_ref, meas_numb_ref, score_vel_ref);

  if( p->resultN < p->resultAllocN )
  {
    result_t* r = p->resultA + p->resultN;
    r->perf_uid     = uid;
    r->perf_pitch   = pitch;
    r->perf_vel     = vel;
    r->match_loc_id = matched_loc_id_ref;
    p->resultN += 1;
  }
  
  return rc;
}

cw::rc_t cw::score_follow_2::do_exec( handle_t h )
{
  rc_t  rc = kOkRC;
  sf_t* p  = _handleToPtr(h);

  _trkr_do_decay(p->trk);
  return rc;
}

void cw::score_follow_2::report_summary( handle_t h, rpt_t& rpt_ref )
{
  sf_t* p = _handleToPtr(h);
  
  rpt_ref.matchN    = 0;
  rpt_ref.missN     = 0;
  rpt_ref.spuriousN = 0;
  rpt_ref.perfNoteN = 0;
  
  for(unsigned i=0; i<p->resultN; ++i)    
    if( p->resultA[i].match_loc_id == kInvalidIdx )
      rpt_ref.spuriousN += 1;

  unsigned bli = p->locMapA[ p->beg_loc_id ];
  unsigned eli = p->locMapA[ p->end_loc_id ];
    
  unsigned bni = p->locA[ bli ].note_idxA[0];
  unsigned eni = p->locA[ eli ].note_idxA[ p->locA[eli].note_idxN-1 ];

  for(unsigned ni=bni; ni<=eni; ++ni)
    if( p->trk->note_match_cntA[ni] == 0 )
      rpt_ref.missN += 1;
    else
      rpt_ref.matchN += 1;

  rpt_ref.perfNoteN = p->trk->new_note_idx;

  if( p->args.rpt_fl )
    cwLogInfo("Matched:%i Missed:%i Spurious:%i",rpt_ref.matchN,rpt_ref.missN,rpt_ref.spuriousN);

}

unsigned cw::score_follow_2::max_loc_id( handle_t h )
{
  sf_t* p = _handleToPtr(h);
  
  return p->max_loc_id;
}


const cw::score_follow_2::result_t* cw::score_follow_2::result_array( handle_t h, unsigned& resultN_ref)
{
  sf_t* p = _handleToPtr(h);
  resultN_ref = p->resultN;
  return p->resultA;
}
