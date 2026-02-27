#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwVectOps.h"

#include "cwMtx.h"

#include "cwDspTypes.h" // srate_t, sample_t, coeff_t, ...

#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwMidi.h"
#include "cwMidiFile.h"

#include "cwFlowDecl.h"
#include "cwFlow.h"
#include "cwFlowValue.h"
#include "cwFlowTypes.h"
#include "cwFlowNet.h"
#include "cwFlowProc.h"


#include "cwDynRefTbl.h"    
#include "cwScoreParse.h"
#include "cwSfScore.h"
#include "cwSfTrack.h"
#include "cwPerfMeas.h"
#include "cwScoreFollowerPerf.h"
#include "cwScoreFollower.h"

#include "cwPianoScore.h"
#include "cwScoreFollow2.h"

#include "cwPianoScore.h"

#include "cwPresetSel.h"

#include "cwMidiDetectors.h"

#include "cwFlowPerf.h"

namespace cw
{

  namespace flow
  {
    //------------------------------------------------------------------------------------------------------------------
    //
    // Score Player
    //

    
    namespace score_player
    {
      enum {
        kScoreFNamePId,
        kStoppingMsPId,
        kDoneFlPId,
        kMaxLocPId,
        kOutPId,
        kStartPId,
        kStopPId,        
        kBLocPId,
        kELocPId,
        kBMeasPId,
        kEMeasPId,
      };

      enum {
        kDampPedalDownFl = 0x01,
        kSostPedalDownFl = 0x02,
        kSoftPedalDownFl = 0x04
      };

      typedef enum {
        kIdleStateId = 0,
        kPlayStateId,
        kStoppingStateId
      } state_t;

      enum {
        kAllNotesOffMsgIdx,
        kResetAllCtlsMsgIdx,
        kDampPedalDownMsgIdx,
        kSostPedalDownMsgIdx,
        
        kMidiMsgN
      };

      typedef struct
      {
        unsigned         flags;       
        unsigned         sample_idx;
        unsigned         loc;
        unsigned         meas;
        unsigned         piano_id;
        unsigned         d1;   // inital d1 value before velocity mapping was applied
        midi::ch_msg_t*  midi; // index of associated msg in chMsgA
      } msg_t;

      typedef struct
      {
        unsigned        msgAllocN; // allocated size of msgA[] and chMsgA
        unsigned        msgN;      // actual count of records in msgA[] and chMsgA[]
        msg_t*          msgA;      // msgA[ msgN ]     - score messages [ meas,loc, ch_msg, ... ]
        midi::ch_msg_t* chMsgA;    // chMsgA[ msgN ]   - ch_msg_t part of the score messages

        // pre-computed special midi msg's: all ctl's,all notes off, dampler down, sostenuto down
        msg_t           midiMsgA[ kMidiMsgN ];
        midi::ch_msg_t  midiChMsgA[ kMidiMsgN ];

        
        recd_array_t* recd_array;    // output record array for 'out'.
        unsigned      midi_fld_idx;  // pre-computed record field indexes
        unsigned      loc_fld_idx;   //
        unsigned      meas_fld_idx;  //
        unsigned      piano_fld_idx; //

        unsigned  score_end_loc;     // last score location
        unsigned  score_end_meas;    // last measure number

        unsigned sample_idx;          // next score sample index - increments during playback
        unsigned msg_idx;             // next score msg index - increments during playback
        
        unsigned end_msg_idx;         // last msg to play before going into 'stopping' state or kInvalidIdx to play entire score
        unsigned cur_meas;            // measure number of the measure currently being played

        unsigned bVId;                 // set to variable->vid when the begin loc or begin measure is changed
        unsigned eVId;                 // set to variable->vid when the end loc or end measure is changed
        bool     start_trig_fl;        // the start btn was clicked
        bool     stop_trig_fl;         // the stop btn was clicked

        unsigned note_cnt;             // count of currently active notes (based on note-on messages) 
        state_t  state;                // idle,play,stopping
        unsigned stopping_ms;          // max time in milliseconds to wait for all notes to end before sending all-note-off
        unsigned stopping_sample_idx;  // 0 if not 'stopping', otherwise the max sample index after which the player will enter 'idle' state.

        unsigned maxLocId; 
        
      } inst_t;

      rc_t _load_score( proc_t* proc, inst_t* p, const char* score_fname )
      {
        rc_t rc = kOkRC;

        perf_score::handle_t       perfScoreH;
        const perf_score::event_t* score_evt   = nullptr;
        char*                      fname       = nullptr;
        unsigned                   pedalStateFlags = 0;
        unsigned                   last_loc = 0;
        p->score_end_loc  = 0;
        p->score_end_meas = 0;

        if( score_fname == nullptr || textLength(score_fname)==0  )
        {
          rc = proc_error(proc,kInvalidArgRC,"The score filename is blank.");
          goto errLabel;
        }

        if((fname = proc_expand_filename( proc, score_fname )) == nullptr )
        {
          rc = proc_error(proc,kOpFailRC,"The score filename (%s) is invalid.",score_fname);
          goto errLabel;
        }

        proc_info(proc,"Opening:%s",fname);
        
        if((rc= perf_score::create( perfScoreH, fname )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Score create failed on '%s'.",fname);
          goto errLabel;          
        }

        if((p->msgAllocN = perf_score::event_count(perfScoreH)) == 0 )
        {
          rc = proc_warn(proc,"The score '%s' is empty.",fname);
          goto errLabel;
        }

        if((score_evt = perf_score::base_event(perfScoreH)) == nullptr )
        {
          rc = proc_error(proc,kOpFailRC,"The score '%s' could not be accessed.",fname);
          goto errLabel;
        }

        p->maxLocId = 0;
        p->msgA   = mem::allocZ<msg_t>(p->msgAllocN);
        p->chMsgA = mem::allocZ<midi::ch_msg_t>(p->msgAllocN);
        
        for(; p->msgN<p->msgAllocN && score_evt !=nullptr; score_evt=score_evt->link)
        {
          if( score_evt->status != 0 )
          {
            bool            note_on_fl = false;
            msg_t*          m  = p->msgA   + p->msgN;
            midi::ch_msg_t* mm = p->chMsgA + p->msgN;

            if( score_evt->loc != kInvalidId )
            {
              // verify that the score is in order by location
              if( score_evt->loc < last_loc )
              {
                rc = proc_error(proc,kInvalidStateRC,"The score cannot be loaded because is not in order by location.");
                break;
              }
              
              last_loc = score_evt->loc;
            }
            
            if( last_loc > p->maxLocId )
              p->maxLocId = last_loc;
            
            m->sample_idx = (unsigned)(proc->ctx->sample_rate * score_evt->sec);
            m->loc        = last_loc;
            m->meas       = score_evt->meas;           
            m->midi       = mm;
            m->piano_id   = score_evt->piano_id;

            //printf("%i %i\n",m->meas,m->loc);

            if( m->loc!=kInvalidId && m->loc > p->score_end_loc )              
              p->score_end_loc  = m->loc;
            
            if( m->meas > p->score_end_meas )
              p->score_end_meas = m->meas;
                        
            time::fracSecondsToSpec( mm->timeStamp, score_evt->sec );

            note_on_fl = midi::isNoteOn(score_evt->status,score_evt->d1);

            mm->devIdx = note_on_fl ? m->loc : kInvalidIdx;             //BUG BUG BUG: hack to do per chord/note processing in gutim_ps
            mm->portIdx= note_on_fl ? score_evt->chord_note_idx : kInvalidIdx;
            mm->uid    = note_on_fl ? score_evt->chord_note_cnt : kInvalidId;    
            mm->ch     = score_evt->status & 0x0f;
            mm->status = score_evt->status & 0xf0;
            mm->d0     = score_evt->d0;
            mm->d1     = score_evt->d1;
            m->d1      = score_evt->d1;  // track the initial d1 before vel. mapping is applied

            

            if(  midi::isSustainPedal( mm->status, mm->d0 ) )
            {
              bool down_fl = pedalStateFlags & kDampPedalDownFl;
              pedalStateFlags = cwEnaFlag(pedalStateFlags, kDampPedalDownFl, midi::isPedalDown( mm->d1 ) );
              if( (pedalStateFlags & kDampPedalDownFl) == down_fl )
                proc_error(proc,kInvalidStateRC,"Two damper pedal %s msg's without an intervening %s msg. meas:%i", down_fl ? "down" : "up", down_fl ? "up" : "down", m->meas );
              
            }

            if(  midi::isSostenutoPedal( mm->status, mm->d0 ) )
            {
              bool down_fl = pedalStateFlags & kSostPedalDownFl;
              pedalStateFlags = cwEnaFlag(pedalStateFlags, kSostPedalDownFl, midi::isPedalDown( mm->d1 ) );
              if( (pedalStateFlags & kSostPedalDownFl) == down_fl )
                proc_error(proc,kInvalidStateRC,"Two sostenuto pedal %s msg's without an intervening %s msg. meas:%i", down_fl ? "down" : "up", down_fl ? "up" : "down", m->meas );

            }            
            m->flags = pedalStateFlags;
            
            p->msgN += 1;
          }

        }
        
        
      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Score load failed on '%s'.",cwStringNullGuard(fname));
        
        perf_score::destroy(perfScoreH);

        mem::release(fname);

        return rc;
      }

      rc_t _on_new_begin_loc( proc_t* proc, inst_t* p, unsigned vid )
      {
        rc_t rc = kOkRC;
        unsigned i = 0;
        const char* label = "";
        unsigned value = 0;
        unsigned bmeas;
        unsigned bloc;
        
        var_get( proc, kBMeasPId, kAnyChIdx, bmeas );
        var_get( proc, kBLocPId,  kAnyChIdx, bloc );

        for(i=0; i<p->msgN; ++i)
          if( (vid==kBLocPId && p->msgA[i].loc >= bloc) || (vid==kBMeasPId && p->msgA[i].meas>=bmeas) )
            break;

        switch( vid )
        {
          case kBLocPId:
            printf("Setting: bloc:%i %i meas:%i msg_idx:%i\n",bloc,p->msgA[i].loc, p->msgA[i].meas+1,i);
            if( i < p->msgN )
              var_set(proc,kBMeasPId,kAnyChIdx,p->msgA[i].meas+1);
            else
              var_set(proc,kBLocPId,kAnyChIdx,0);
            
            label = "location";
            value = bloc;
            break;
            
          case kBMeasPId:
            
            printf("Setting: bmeas:%i %i meas:%i msg_idx:%i\n",bmeas,p->msgA[i].loc, p->msgA[i].meas, i);
            if( i < p->msgN )
              var_set(proc,kBLocPId,kAnyChIdx,p->msgA[i].loc);
            else
              var_set(proc,kBMeasPId,kAnyChIdx,1);
            
            label = "measure";
            value = bmeas;
            break;
        }
        
        if( i >= p->msgN )
          rc = proc_error(proc,kInvalidArgRC,"Invalid begin %s %i.",label,value);

        return rc;
      }

      rc_t _on_new_end_loc( proc_t* proc, inst_t* p, unsigned vid )
      {
        rc_t        rc    = kOkRC;
        unsigned    i     = 0;
        unsigned    emeas;
        unsigned    eloc;
        
        var_get( proc, kEMeasPId, kAnyChIdx, emeas );
        var_get( proc, kELocPId,  kAnyChIdx, eloc );

        p->end_msg_idx = kInvalidIdx;
        
        for(i=0; i<p->msgN; ++i)
          if( (vid==kELocPId && p->msgA[i].loc >= eloc) || (vid==kEMeasPId && p->msgA[i].meas>=emeas) )
          {
            p->end_msg_idx = i;
            break;            
          }

        // NOTE: we allow the end-loc/end-meas to be set to the loc/meas after the last measure
        
        switch( vid )
        {
          case kELocPId:
            if( i < p->msgN )
              var_set(proc,kEMeasPId,kAnyChIdx,p->msgA[i].meas);
            else
              var_set(proc,kEMeasPId,kAnyChIdx,p->msgA[p->msgN-1].meas+1);
            break;
            
          case kEMeasPId:
            if( i < p->msgN )
              var_set(proc,kELocPId,kAnyChIdx,p->msgA[i].loc);
            else
              var_set(proc,kELocPId,kAnyChIdx,p->msgA[p->msgN-1].loc+1);
            break;
        }

        return rc;
      }

      
      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t    rc   = kOkRC;        
        const char* score_fname = nullptr;
        unsigned bloc=0;
        unsigned eloc=0;
        unsigned bmeas=0;
        unsigned emeas=0;
        
        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kScoreFNamePId,    "fname",         kBaseSfxId, score_fname,
                                      kStoppingMsPId,    "stopping_ms",   kBaseSfxId, p->stopping_ms,
                                      kBLocPId,          "b_loc",         kBaseSfxId, bloc,
                                      kBMeasPId,         "b_meas",        kBaseSfxId, bmeas,
                                      kELocPId,          "e_loc",         kBaseSfxId, eloc,
                                      kEMeasPId,         "e_meas",        kBaseSfxId, emeas,
                                      kStartPId,         "start",         kBaseSfxId, p->start_trig_fl)) != kOkRC )
        {
          goto errLabel;
        }
        

        // load the score
        if((rc = _load_score( proc, p, score_fname )) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_register(proc,kAnyChIdx,
                              kStopPId,  "stop",  kBaseSfxId )) != kOkRC )
        {
          goto errLabel;
        }
        

        
        if((rc = var_register_and_set(proc,kAnyChIdx,
                                      kDoneFlPId,"done_fl", kBaseSfxId, false,
                                      kMaxLocPId,"loc_cnt", kBaseSfxId, p->maxLocId+1 )) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_alloc_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, p->recd_array )) != kOkRC )
        {
          goto errLabel;
        }

        p->midi_fld_idx = recd_type_field_index( p->recd_array->type, "midi");
        p->loc_fld_idx  = recd_type_field_index( p->recd_array->type, "loc");
        p->meas_fld_idx = recd_type_field_index( p->recd_array->type, "meas");
        p->piano_fld_idx= recd_type_field_index( p->recd_array->type, "piano_id");

        p->bVId = bloc!=0 ? (unsigned)kBLocPId : (bmeas !=0 ? (unsigned)kBMeasPId : kInvalidId);
        p->eVId = eloc!=0 ? (unsigned)kELocPId : (emeas !=0 ? (unsigned)kEMeasPId : kInvalidId);
        p->end_msg_idx = kInvalidIdx;

        p->midiChMsgA[kAllNotesOffMsgIdx]   = { .timeStamp={ .tv_sec=0, .tv_nsec=0}, .devIdx=kInvalidIdx, .portIdx=kInvalidIdx, .uid=0, .ch=0, .status=midi::kCtlMdId, .d0=midi::kAllNotesOffMdId,  .d1=0  };
        p->midiChMsgA[kResetAllCtlsMsgIdx]  = { .timeStamp={ .tv_sec=0, .tv_nsec=0}, .devIdx=kInvalidIdx, .portIdx=kInvalidIdx, .uid=0, .ch=0, .status=midi::kCtlMdId, .d0=midi::kResetAllCtlsMdId, .d1=0  };
        p->midiChMsgA[kDampPedalDownMsgIdx] = { .timeStamp={ .tv_sec=0, .tv_nsec=0}, .devIdx=kInvalidIdx, .portIdx=kInvalidIdx, .uid=0, .ch=0, .status=midi::kCtlMdId, .d0=midi::kSustainCtlMdId,   .d1=64  };
        p->midiChMsgA[kSostPedalDownMsgIdx] = { .timeStamp={ .tv_sec=0, .tv_nsec=0}, .devIdx=kInvalidIdx, .portIdx=kInvalidIdx, .uid=0, .ch=0, .status=midi::kCtlMdId, .d0=midi::kSostenutoCtlMdId, .d1=64  };

        p->midiMsgA[kAllNotesOffMsgIdx].midi   = p->midiChMsgA + kAllNotesOffMsgIdx;
        p->midiMsgA[kResetAllCtlsMsgIdx].midi  = p->midiChMsgA + kResetAllCtlsMsgIdx;
        p->midiMsgA[kDampPedalDownMsgIdx].midi = p->midiChMsgA + kDampPedalDownMsgIdx;
        p->midiMsgA[kSostPedalDownMsgIdx].midi = p->midiChMsgA + kSostPedalDownMsgIdx;

        if( eloc==0 && emeas==0 )
        {
          var_set(proc,kELocPId,kAnyChIdx,p->score_end_loc);
          var_set(proc,kEMeasPId,kAnyChIdx,p->score_end_meas+1 );
        }
        

        p->state = kIdleStateId;
        
      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        recd_array_destroy(p->recd_array);
        mem::release(p->msgA);
        mem::release(p->chMsgA);
        return rc;
      }

      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;

        if( proc->ctx->isInRuntimeFl )
        {
          switch( var->vid )
          {
            case kStartPId:
              p->start_trig_fl = true;
              break;
            
            case kStopPId:
              p->stop_trig_fl = true;
              break;
            
            case kBLocPId:
            case kBMeasPId:
              p->bVId = var->vid;
              break;
            
            case kELocPId:
            case kEMeasPId:
              p->eVId = var->vid;
              break;
            
          }
        }
        return rc;
      }
      

      rc_t _do_begin_stopping( proc_t* proc, inst_t* p, unsigned stopping_ms )
      {
        p->state = kStoppingStateId;
        p->stopping_sample_idx = p->sample_idx + int((proc->ctx->sample_rate * stopping_ms)/1000.0);
        proc_info(proc,"Stopping ...");
        return kOkRC;
      }

      rc_t _set_output_record( proc_t* proc, inst_t* p, rbuf_t* rbuf, const msg_t* m )
      {
        rc_t rc = kOkRC;
        
        recd_t* r = p->recd_array->recdA + rbuf->recdN;
        
        // if the output record array is full
        if( rbuf->recdN >= p->recd_array->allocRecdN )
        {
          rc = proc_error(proc,kBufTooSmallRC,"The internal record buffer overflowed. (buf recd count:%i).",p->recd_array->allocRecdN);
          goto errLabel;
        }
        
        recd_set( rbuf->type, nullptr, r, p->midi_fld_idx, m->midi );
        recd_set( rbuf->type, nullptr, r, p->loc_fld_idx,  m->loc  );
        recd_set( rbuf->type, nullptr, r, p->meas_fld_idx, m->meas );
        recd_set( rbuf->type, nullptr, r, p->piano_fld_idx,m->piano_id);
        rbuf->recdN += 1;

      errLabel:
        return rc;
      }

      rc_t _do_stop_now( proc_t* proc, inst_t* p, rbuf_t* rbuf )
      {
        rc_t rc = kOkRC;

        // copy the 'all-note-off','all-ctl-off' msg into output record array
        _set_output_record(proc,p,rbuf,p->midiMsgA + kAllNotesOffMsgIdx);
        _set_output_record(proc,p,rbuf,p->midiMsgA + kResetAllCtlsMsgIdx);
            
        p->state = kIdleStateId;

        // set the 'done' output flag
        var_set(proc, kDoneFlPId, kAnyChIdx, true );

        proc_info(proc,"Stopped.");
        
        return rc;
      }
      
      
      rc_t _on_start_clicked( proc_t* proc, inst_t* p, rbuf_t* rbuf )
      {
        rc_t rc       = kOkRC;
        unsigned bloc = 1;
        unsigned i    = 0;

        if( p->state != kIdleStateId )
          if((rc = _do_stop_now(proc,p,rbuf)) != kOkRC )
            goto errLabel;

        // BUG BUG BUG - using measure instead of loc because when we use loc
        // we go to the wrong place
        
        var_get( proc, kBLocPId,  kAnyChIdx, bloc );

        printf("Starting at loc:%i\n",bloc);

        // Rewind the current position to the begin location
        for(i=0; i<p->msgN; ++i)
          if( p->msgA[i].loc >= bloc )
          {
            p->sample_idx = p->msgA[i].sample_idx;
            p->msg_idx    = i;
            p->cur_meas   = p->msgA[i].meas;

            // if the damper pedal is down at the start location
            if( p->msgA[i].flags & kDampPedalDownFl )
              _set_output_record(proc,p,rbuf,p->midiMsgA + kDampPedalDownMsgIdx);

            // if the sostenuto pedal was put down at the start location
            if( p->msgA[i].flags & kSostPedalDownFl )            
              _set_output_record(proc,p,rbuf,p->midiMsgA + kSostPedalDownMsgIdx);
            
            proc_info(proc,"New current: msg_idx:%i meas:%i loc:%i %i",p->msg_idx, p->msgA[i].meas, p->msgA[i].loc, bloc );
            break;            
          }

        p->stopping_sample_idx = 0;
        p->note_cnt            = 0;
        p->state               = kPlayStateId;
        
      errLabel:
        return kOkRC;
      }

      rc_t _on_stop_clicked( proc_t* proc, inst_t* p, rbuf_t* rbuf )
      {
        // begin stopping with the stopping time set to 0.
        return _do_stop_now(proc,p,rbuf);
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t    rc      = kOkRC;
        rbuf_t* rbuf    = nullptr;

        
        // get the output variable
        if((rc = var_get(proc,kOutPId,kAnyChIdx,rbuf)) != kOkRC )
        {
          rc = proc_error(proc,kInvalidStateRC,"The score player '%s' does not have a validoutput buffer.",proc->label);
          goto errLabel;
        }

        rbuf->recdA = p->recd_array->recdA;
        rbuf->recdN = 0;

        // if the begin loc/meas was changed
        if( p->bVId != kInvalidId )
        {
          _on_new_begin_loc(proc,p,p->bVId);
          p->bVId = kInvalidId;
        }

        // if the end loc/meas was changed
        if( p->eVId != kInvalidId )
        {
          _on_new_end_loc(proc,p,p->eVId);
          p->eVId = kInvalidId;
        }

        // if the start button was clicked
        if( p->start_trig_fl )
        {
          _on_start_clicked(proc,p,rbuf);
          p->start_trig_fl = false;
        }

        // if the stop button was clicked
        if( p->stop_trig_fl )
        {
          _on_stop_clicked(proc,p,rbuf);
          p->stop_trig_fl = false;
        }

        // if in idle state then there is noting to d
        if( p->state == kIdleStateId )
          goto errLabel;

        // advance sample_idx to the end sample associated with this cycle
        p->sample_idx += proc->ctx->framesPerCycle;

        // transmit all msgs, beginning with the msg at p->msg_idx,  whose 'sample_idx' is <= p->sample_idx
        while( p->msg_idx < p->msgN && p->sample_idx >= p->msgA[p->msg_idx].sample_idx )
        {
          msg_t*  m = p->msgA + p->msg_idx;

          // if the end-loc was encountered
          if( p->state==kPlayStateId && p->end_msg_idx != kInvalidIdx && p->msg_idx > p->end_msg_idx )
          {
            _do_begin_stopping(proc,p,p->stopping_ms);
          }

          bool note_on_fl = midi::isNoteOn(m->midi->status, m->midi->d1);
          
          //if( note_on_fl )
          //{
          //  printf("sc:%i %i %i %s\n",m->meas,m->loc,m->midi->d0,midi::midiToSciPitch(m->midi->d0));
          //}
           
          // fill the output record with this msg but filter out note-on's when in stopping-state
          if( p->state == kPlayStateId || (p->state==kStoppingStateId && note_on_fl==false) )
          {
            _set_output_record(proc,p, rbuf, m );

            if( note_on_fl )
              p->note_cnt += 1;
            
            if( midi::isNoteOff(m->midi->status, m->midi->d1 ) && p->note_cnt > 0)
              p->note_cnt -= 1;
          }
              

          p->msg_idx += 1;

          // track the current measure
          if( m->meas > p->cur_meas )
          {
            proc_info(proc,"meas:%i",m->meas);
            p->cur_meas = m->meas;
          }
        } // end-while

        // if the end of the stopping state has been reached or if there are no more msg's in the score
        if( (p->state==kStoppingStateId && (p->note_cnt == 0 || p->sample_idx> p->stopping_sample_idx)) || p->msg_idx >= p->msgN )
        {
          proc_info(proc,"End-of-stopping: note_cnt:%i %s %s.",p->note_cnt,p->sample_idx> p->stopping_sample_idx ? "timed-out":"", p->msg_idx>=p->msgN ? "score-done":"");
          _do_stop_now(proc,p,rbuf);
        }
        

      errLabel:
        return rc;
      }
      
      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .notify  = std_notify<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    } // score_player


    //------------------------------------------------------------------------------------------------------------------
    //
    // multi_player
    //
    namespace multi_player
    {
      enum {
        kCfgFnamePId,
        kStartPlyrLabelPId,
        kStartPlyrSegIdPId,
        kStartPId,
        kPlayPlyrIdPId,
        kClearPId,
        kResetPId,
        kPlayNowPlyrIdPId,
        kDonePlyrIdPId,
        kOutPId        
      };

      enum {
        kNoteCnt = midi::kMidiChCnt*midi::kMidiNoteCnt,
        kCtlCnt  = midi::kMidiChCnt*midi::kMidiCtlCnt
      };

      typedef struct msg_str
      {
        unsigned uid;
        double   sec;
        unsigned sample_idx;
        unsigned meas;
        unsigned loc;
        midi::ch_msg_t midi;
      } msg_t;
      
      typedef struct player_str
      {
        unsigned id;       // player id
        char*    label;    // player label
        unsigned port_id;  // 'port_id' field value for all record generated by this player
        unsigned allocMsgN;//  Allocated memory for msgA[]
        unsigned msgN;     //  Count of records in msgA[]
        msg_t*   msgA;     //  msgA[ msgN ] -

        unsigned  next_msg_idx;  // Next message to emit from this player or kInvalidId if this player is not active
        unsigned  start_smp_idx; // 
        unsigned* keyM;          // keyM[ kMidiChCnt*kMidiNoteCnt ] of last velocity for each note
        unsigned* ctlM;          // ctlM[ kMIdiChCnt*kMidiCtlCnt  ] of last control value for each contrl

        unsigned cur_meas;
      } player_t;
      
      typedef struct
      {
        recd_array_t* recd_array;
        
        unsigned      playerN;
        player_t*     playerA;

        unsigned*     portIdA;    // array of unique port_id's among all players
        unsigned      portIdN;

        midi::ch_msg_t* noteOffM;  // noteOffMtx[ kMidiChCnt * kMidiNoteCnt ] - all possible note off messages
        midi::ch_msg_t* ctlOffM;   // ctlOffMtx[  kMidiChCnt * kMidiCtlCnt ]  - all possible ctl = 0 messages

        unsigned midi_fld_idx;
        unsigned loc_fld_idx;
        unsigned meas_fld_idx;
        unsigned port_fld_idx;

        unsigned global_smp_idx; // Global current sample idx - incremented on each call to _exec()
        bool     start_trig_fl;
        bool     clear_trig_fl;
        bool     reset_trig_fl;        
        unsigned play_excl_trig_fl;
        
      } inst_t;

      /*
        {
        "spirio_1": {
        "player_id": 3,
        "label": "gutim_4",
        "port_id": 0,
        "msgL": [
        {
        "uid": 0,
        "sec": 0.0,
        "ch": 0,
        "status": 144,
        "d0": 24,
        "d1": 5
        },
      */

      unsigned _player_id_to_index( inst_t* p, unsigned plyr_id )
      {
        for(unsigned i=0; i<p->playerN; ++i)
          if( p->playerA[i].id == plyr_id )
            return i;

        return kInvalidIdx;
      }

      unsigned _player_label_to_index( inst_t* p, const char* plyr_label )
      {
        for(unsigned i=0; i<p->playerN; ++i)
          if( textIsEqual(plyr_label,p->playerA[i].label) )
            return i;

        return kInvalidIdx;
      }

      unsigned _port_id_to_index( const inst_t* p,unsigned port_id )
      {
        for(unsigned i=0; i<p->portIdN; ++i)
          if( p->portIdA[i] == port_id )
            return i;
        return kInvalidIdx;
      }

      rc_t _parse_cfg( proc_t* proc, inst_t* p, const object_t* cfg )
      {
        rc_t rc = kOkRC;
        unsigned meas = 0;
        
        p->playerN        = cfg->child_count();
        p->playerA        = mem::allocZ<player_t>(p->playerN);
        p->portIdA        = mem::allocZ<unsigned>(p->playerN);
        p->portIdN        = 0;
        p->global_smp_idx = 0;
        p->noteOffM       = mem::allocZ<midi::ch_msg_t>(midi::kMidiChCnt*midi::kMidiNoteCnt);
        p->ctlOffM        = mem::allocZ<midi::ch_msg_t>(midi::kMidiChCnt*midi::kMidiCtlCnt);

        // fill in the note-off and ctl-off message matrices
        for(unsigned ch_idx=0; ch_idx<midi::kMidiChCnt; ++ch_idx)
        {
          unsigned base_ch_idx = ch_idx * midi::kMidiNoteCnt;
          for(unsigned i=0; i<midi::kMidiNoteCnt; ++i)
          {
            midi::ch_msg_t* m = p->noteOffM + (base_ch_idx + i);
            m->uid    = kInvalidId;
            m->ch     = ch_idx;
            m->status = midi::kNoteOnMdId;
            m->d0     = i;
            m->d1     =  0;
          }
          
          base_ch_idx = ch_idx * midi::kMidiCtlCnt;
          for(unsigned i=0; i<midi::kMidiCtlCnt; ++i)
          {
            midi::ch_msg_t* m = p->ctlOffM + (base_ch_idx + i);
            m->uid    = kInvalidId;
            m->ch     = ch_idx;
            m->status = midi::kCtlMdId;
            m->d0     = i;
            m->d1     = 0;
          }
          
        }

        for(unsigned i=0; i<p->playerN; ++i)
        {
          const object_t* plyr_pair = cfg->child_ele(i);
          const object_t* plyr_cfg = nullptr;

          if( !plyr_pair->is_pair() || (plyr_cfg = plyr_pair->pair_value()) == nullptr )
          {
            rc = proc_error(proc,kSyntaxErrorRC,"The player configuration for the pair at index '%i' could not be parsed.",i);
          }
          else
          {
            player_t*       plyr     = p->playerA + i;
            const char*     label    = nullptr;
            const object_t* cfg_msgL = nullptr;
          
            if((rc = plyr_cfg->getv("player_id",plyr->id,
                                    "label",label,
                                    "port_id",plyr->port_id,
                                    "msgL",cfg_msgL)) != kOkRC )
            {
              rc = proc_error(proc,rc,"Parse failed on player header parse in '%s",cwStringNullGuard(proc->label));
              goto errLabel;
            }

            plyr->label = mem::duplStr(label);

            plyr->allocMsgN = cfg_msgL->child_count();
            plyr->msgA      = mem::allocZ<msg_t>(plyr->allocMsgN);
            plyr->msgN      = 0;
            plyr->keyM      = mem::allocZ<unsigned>(kNoteCnt);
            plyr->ctlM      = mem::allocZ<unsigned>(kCtlCnt);
            plyr->next_msg_idx = kInvalidIdx;
            plyr->start_smp_idx = kInvalidIdx;

            if( _port_id_to_index(p,plyr->port_id ) == kInvalidIdx )
            {
              p->portIdA[ p->portIdN++ ] = plyr->port_id;
            }
            
          
            for(unsigned j=0; j<plyr->allocMsgN; ++j)
            {
              unsigned uid    = kInvalidId;
              double   sec    = 0.0;
              unsigned ch     = 0;
              unsigned status = 0;
              unsigned d0     = 0;
              unsigned d1     = 0;

              const object_t* cfg_msg = cfg_msgL->child_ele(j);

              if((rc = cfg_msg->getv("uid",uid,
                                     "sec",sec,
                                     "ch",ch,
                                     "status",status,
                                     "d0",d0,
                                     "d1",d1)) != kOkRC )
              {
                proc_error(proc,rc,"Error parsing msg index %i for player %s in %s.",j,cwStringNullGuard(label),cwStringNullGuard(proc->label));
                goto errLabel;
              }

              switch( status & 0xf0 )
              {
                case midi::kPbendMdId:
                  meas = midi::to14Bits(d0,d1);
                  break;
                
                case midi::kNoteOnMdId:
                case midi::kNoteOffMdId:
                case midi::kCtlMdId:
                  {
                    assert( plyr->msgN < plyr->allocMsgN );
                  
                    msg_t* m = plyr->msgA + plyr->msgN;
                    m->uid         = uid;
                    m->sec         = sec;
                    m->sample_idx  = (unsigned)(proc->ctx->sample_rate * sec);
                    m->meas        = meas;
                    m->loc         = kInvalidId;                    
                    m->midi.portIdx= plyr->port_id;
                    m->midi.ch     = ch;
                    m->midi.status = status;
                    m->midi.d0     = d0;
                    m->midi.d1     = d1;

                    time::fracSecondsToSpec(m->midi.timeStamp, sec );


                    plyr->msgN += 1;
                  }
                  break;
              }            
            }
          }
        }
      errLabel:
        return rc;
        
      }
      
      rc_t _parse_cfg_file( proc_t* proc, inst_t* p, const char* fname )
      {
        rc_t rc = kOkRC;
        char* fn = nullptr;;
        object_t* cfg = nullptr;
        
        if((fn = proc_expand_filename(proc,fname)) == nullptr )
        {
          rc = proc_error(proc,kOpFailRC,"The cfg file name '%s' could not be expanded.",cwStringNullGuard(fname));
          goto errLabel;
        }
          
        if((rc = objectFromFile( fn, cfg )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Unable to parse cfg from '%s'.",cwStringNullGuard(fn));
          goto errLabel;
        }

        if((rc = _parse_cfg( proc, p, cfg)) != kOkRC )
        {
          goto errLabel;
        }

      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Configuration file parsing failed on '%s' in '%s'.",cwStringNullGuard(fname),cwStringNullGuard(proc->label));

        mem::release(fn);

        if( cfg != nullptr )
          cfg->free();
        
        return rc;
      }

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t        rc                = kOkRC;
        const char* cfg_fname         = nullptr;
        const char* start_plyr_label  = nullptr;
        unsigned    start_plyr_seg_id = kInvalidId;
        bool        start_fl          = false;
        unsigned    play_plyr_id      = kInvalidId;
        bool        reset_fl          = false;
        bool        clear_fl          = false;
        unsigned    play_excl_plyr_id = kInvalidId;
        unsigned    done_plyr_id      = kInvalidId;

        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kCfgFnamePId,      "cfg_fname",   kBaseSfxId, cfg_fname,
                                      kStartPlyrLabelPId,"start_label", kBaseSfxId, start_plyr_label,
                                      kStartPlyrSegIdPId,"start_seg_id",kBaseSfxId, start_plyr_seg_id,
                                      kStartPId,         "start",       kBaseSfxId, start_fl,
                                      kPlayPlyrIdPId,    "play_id",     kBaseSfxId, play_plyr_id,
                                      kClearPId,         "clear",       kBaseSfxId, clear_fl,
                                      kResetPId,         "reset",       kBaseSfxId, reset_fl,
                                      kPlayNowPlyrIdPId, "play_excl_id",kBaseSfxId, play_excl_plyr_id,
                                      kDonePlyrIdPId,    "done_id",     kBaseSfxId, done_plyr_id)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = _parse_cfg_file(proc,p,cfg_fname)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_alloc_register_and_set(proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, p->recd_array )) != kOkRC )
        {
          goto errLabel;
        }

        
        p->midi_fld_idx = recd_type_field_index( p->recd_array->type, "midi");
        p->loc_fld_idx  = recd_type_field_index( p->recd_array->type, "loc");
        p->meas_fld_idx = recd_type_field_index( p->recd_array->type, "meas");
        p->port_fld_idx= recd_type_field_index( p->recd_array->type, "port_id");
        

      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        recd_array_destroy(p->recd_array);

        for(unsigned i=0;  i<p->playerN; ++i)
        {
          mem::release(p->playerA[i].label);
          mem::release(p->playerA[i].msgA);
          mem::release(p->playerA[i].keyM);
          mem::release(p->playerA[i].ctlM);
        }

        mem::release(p->noteOffM);
        mem::release(p->ctlOffM);
        mem::release(p->playerA);
        mem::release(p->portIdA);

        return rc;
      }

      rc_t _start_player( proc_t* proc, inst_t* p, unsigned plyr_idx )
      {
        rc_t rc = kOkRC;
        
        if( plyr_idx == kInvalidIdx)
        {
          rc = proc_error(proc,kInvalidArgRC,"An invalid player could not be started.");
          goto errLabel;
        }
        
        if( plyr_idx >= p->playerN )
        {
          rc = proc_error(proc,kInvalidArgRC,"'start' was requested on an out of range player index. %i >= %i",plyr_idx,p->playerN);
          goto errLabel;
        }

        p->playerA[ plyr_idx ].next_msg_idx = 0;
        p->playerA[ plyr_idx ].start_smp_idx = p->global_smp_idx;
        
      errLabel:
        return rc;
      }

      void _set_key_state( unsigned* mtx, unsigned rowN, const midi::ch_msg_t* m, unsigned d1 )
      {
        unsigned idx =  m->ch * rowN + m->d0;
        assert(idx < rowN*midi::kMidiChCnt );
        mtx[ idx ] = d1;
      }
      
      void _update_key_state( player_t* plyr, const midi::ch_msg_t* m )
      {
        switch( midi::removeCh(m->status) )
        {
          case midi::kNoteOnMdId:
            _set_key_state(plyr->keyM,midi::kMidiNoteCnt,m,m->d1);
            break;
            
          case midi::kNoteOffMdId:
            _set_key_state(plyr->keyM,midi::kMidiNoteCnt,m,0);
            break;
            
          case midi::kCtlMdId:
            _set_key_state(plyr->ctlM,midi::kMidiCtlCnt,m,m->d1);            
            break;
        }
      }

      rc_t _set_output_record( proc_t* proc, inst_t* p, player_t* plyr, rbuf_t* rbuf, midi::ch_msg_t* m, unsigned meas, unsigned  port_id, unsigned loc )
      {
        rc_t rc = kOkRC;
        
        recd_t* r = p->recd_array->recdA + rbuf->recdN;
        
        // if the output record array is full
        if( rbuf->recdN >= p->recd_array->allocRecdN )
        {
          rc = proc_error(proc,kBufTooSmallRC,"The internal record buffer overflowed. (buf recd count:%i).",p->recd_array->allocRecdN);
          goto errLabel;
        }

        _update_key_state( plyr, m );
        
        recd_set( rbuf->type, nullptr, r, p->midi_fld_idx, m );
        recd_set( rbuf->type, nullptr, r, p->loc_fld_idx,  loc  );
        recd_set( rbuf->type, nullptr, r, p->meas_fld_idx, meas );
        recd_set( rbuf->type, nullptr, r, p->port_fld_idx, port_id);
        
        rbuf->recdN += 1;

      errLabel:
        return rc;
      }
      

      rc_t _send_clear_player( proc_t* proc, inst_t* p, player_t* plyr, rbuf_t* rbuf )
      {
        rc_t rc = kOkRC;
        
        for(unsigned ch_idx=0; ch_idx<midi::kMidiChCnt; ++ch_idx)
        {
          unsigned ch_base_idx = ch_idx*midi::kMidiNoteCnt;
          
          for(unsigned note_idx=0; note_idx<midi::kMidiNoteCnt; ++note_idx)
          {
            unsigned idx = ch_base_idx + note_idx;
            assert( idx < kNoteCnt);              
            
            if( plyr->keyM[idx] > 0 )
            {
              _set_output_record(proc,p, plyr, rbuf, p->noteOffM + idx, kInvalidId, plyr->port_id, kInvalidId);
            }
          }


          ch_base_idx = ch_idx*midi::kMidiCtlCnt;
          
          for(unsigned ctl_idx=0; ctl_idx<midi::kMidiCtlCnt; ++ctl_idx)
          {
            unsigned idx = ch_base_idx + ctl_idx;
            assert( idx < kCtlCnt);              
            
            if( plyr->ctlM[idx] > 0 )
            {
              _set_output_record(proc,p, plyr, rbuf, p->ctlOffM + idx, kInvalidId, plyr->port_id, kInvalidId);
            }
          }
        }

        return rc;
      }

      rc_t _do_clear( proc_t* proc, inst_t* p, rbuf_t* rbuf )
      {
        rc_t rc = kOkRC;
        
        // clear the key and control matrices of all players
        for(unsigned i=0; i<p->playerN; ++i)
        {
          _send_clear_player(proc, p, p->playerA + i, rbuf );
          
          vop::zero(p->playerA[i].keyM,kNoteCnt);
          vop::zero(p->playerA[i].ctlM,kCtlCnt);

          // mark this player as stopped
          p->playerA[i].next_msg_idx  = kInvalidIdx;  
          p->playerA[i].start_smp_idx = kInvalidIdx;
        }

        return rc;
      }
      
      rc_t _do_reset( proc_t* proc, inst_t* p, rbuf_t* rbuf )
      {
        rc_t rc = kOkRC;
        unsigned ch = 0;

        // send all-note-off out each port
        for(unsigned i=0; i<p->portIdN; ++i)
        {
          // send reset-all-controls
          unsigned idx = ch * midi::kMidiCtlCnt + midi::kResetAllCtlsMdId;
          assert( idx < midi::kMidiChCnt * midi::kMidiCtlCnt );
          
          // Note: we only send for reset-all-controls for the first player - this should be enough.
          //_set_output_record(proc,p,p->playerA,rbuf,p->ctlOffM + idx,kInvalidId,p->playerA->port_id,kInvalidId);
          _set_output_record(proc,p,p->playerA,rbuf,p->ctlOffM + idx,kInvalidId,p->portIdA[i],kInvalidId);
          
          // send all-notes-off
          idx = ch * midi::kMidiCtlCnt + midi::kAllNotesOffMdId;
          assert( idx < midi::kMidiChCnt * midi::kMidiCtlCnt );
        
          // Note: we only send for all-notes-off for the first player - this should be enough.
          //_set_output_record(proc,p,p->playerA,rbuf,p->noteOffM + idx,kInvalidId,p->playerA->port_id,kInvalidId);
          _set_output_record(proc,p,p->playerA,rbuf,p->noteOffM + idx,kInvalidId,p->portIdA[i],kInvalidId);
        }
        
        p->global_smp_idx = 0;
        p->start_trig_fl = false;
        p->clear_trig_fl = false;
        p->reset_trig_fl = false;
        p->play_excl_trig_fl = false;
        
        rc = _do_clear(proc, p,rbuf);

        proc_info(proc,"%s reset",cwStringNullGuard(proc->label));
        
        return rc;
      }      

      rc_t _on_start_trigger( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        const char* plyr_label = nullptr;
        unsigned plyr_seg_id = kInvalidId;
        unsigned plyr_idx = kInvalidIdx;

        // attempt to get the starting player index from the start_seg_id
        if( var_get(proc,kStartPlyrSegIdPId,kAnyChIdx,plyr_seg_id) == kOkRC )
        {
          if( plyr_seg_id != kInvalidId )
            if((plyr_idx = _player_id_to_index(p,plyr_seg_id)) == kInvalidIdx )
            {
              rc = proc_error(proc,kInvalidArgRC,"The requested start player id:'%i' was not found.",plyr_seg_id);
            }
        }

        if( plyr_idx == kInvalidIdx )
        {
          // attempt to get the starting player index from the start_label
          if( var_get(proc,kStartPlyrLabelPId,kAnyChIdx,plyr_label) == kOkRC )
          {
            if( textLength(plyr_label) == 0 )
            {
              if(( plyr_idx = _player_label_to_index(p,plyr_label)) == kInvalidIdx )
              {
                rc = proc_error(proc,kInvalidArgRC,"The requested start player:'%s' was not found.",plyr_label);
              }
            }
          }          
        }

        // if a starting player index was found
        if( plyr_idx == kInvalidIdx )
        {
          rc = proc_error(proc,kInvalidArgRC,"A valid start player was not found for '%s'.",cwStringNullGuard(proc->label));
        }
        else
        {
          rc = _start_player(proc,p,plyr_idx);
        }
        
        return rc;
      }
      
      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
                
        if( proc->ctx->isInRuntimeFl )
        {
          switch( var->vid )
          {
            case kStartPlyrLabelPId:
              break;
              
            case kStartPlyrSegIdPId:
              break;
            
            case kStartPId:  // start the start-player id
              p->start_trig_fl = true;
              proc_info(proc,"%s : start",cwStringNullGuard(proc->label));            
              break;
            
            case kPlayPlyrIdPId:  // start the requested player
              {
                unsigned plyr_id  = kInvalidId;
                if(var_get(var,plyr_id) == kOkRC )
                  _start_player( proc, p, _player_id_to_index(p,plyr_id) );
                proc_info(proc,"%s : play id:%i",cwStringNullGuard(proc->label),plyr_id);
              }            
              break;

            case kClearPId:
              p->clear_trig_fl = true;
              proc_info(proc,"%s : clear",cwStringNullGuard(proc->label));              
              break;
              
            case kResetPId:
              p->reset_trig_fl = true;
              proc_info(proc,"%s : reset",cwStringNullGuard(proc->label));
              break;
            
            case kPlayNowPlyrIdPId:
              p->play_excl_trig_fl = true;
              proc_info(proc,"%s : play_excl",cwStringNullGuard(proc->label));
              break;
          }
        }
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t    rc      = kOkRC;
        rbuf_t* rbuf    = nullptr;

        
        // get the output variable
        if((rc = var_get(proc,kOutPId,kAnyChIdx,rbuf)) != kOkRC )
        {
          rc = proc_error(proc,kInvalidStateRC,"The multi-player '%s' does not have a validoutput buffer.",proc->label);
          goto errLabel;
        }

        rbuf->recdA = p->recd_array->recdA;
        rbuf->recdN = 0;
        
        p->global_smp_idx += proc->ctx->framesPerCycle;

        // do play-excl first because an early call to _do_reset() will set play_excl_trig_fl to false
        if( p->play_excl_trig_fl  )
        {
          unsigned play_excl_id = kInvalidId;
          
          if( var_get(proc,kPlayNowPlyrIdPId,kAnyChIdx,play_excl_id) == kOkRC )
          {
          
            _do_reset(proc, p, rbuf );

            if( play_excl_id != kInvalidId )
              _start_player(proc, p, _player_id_to_index(p,play_excl_id));
          }
          
          p->play_excl_trig_fl = false;
        }
        
        if( p->clear_trig_fl )
        {
          _do_clear(proc, p,rbuf);
          p->clear_trig_fl = false;
        }
        
        if( p->reset_trig_fl )
        {
          _do_reset(proc,p,rbuf );
          p->reset_trig_fl = false;
        }
        
        if( p->start_trig_fl)
        {
          _on_start_trigger(proc,p);
          p->start_trig_fl = false;
        }
        
        for(unsigned i=0; i<p->playerN; ++i)
        {
          player_t* plyr           = p->playerA + i;
          unsigned  player_smp_idx = p->global_smp_idx - plyr->start_smp_idx;
          
          if( plyr->next_msg_idx != kInvalidIdx && plyr->next_msg_idx < plyr->msgN && player_smp_idx >= plyr->msgA[ plyr->next_msg_idx ].sample_idx )
          {
            msg_t* msg = plyr->msgA + plyr->next_msg_idx;
            
            if((rc = _set_output_record(proc,p, plyr, rbuf, &msg->midi, msg->meas, plyr->port_id, msg->loc )) != kOkRC )
            {
              proc_error(proc,rc,"Player output failed on '%s'.",cwStringNullGuard(proc->label));
              goto errLabel;
            }

            plyr->next_msg_idx += 1;

            //printf("%i %i\n",plyr->id,plyr->next_msg_idx);
            
            if( plyr->next_msg_idx >= plyr->msgN )
            {
              //printf("DONE:%i\n",plyr->id);
              var_set(proc,kDonePlyrIdPId,kAnyChIdx,plyr->id);
              
              
              plyr->next_msg_idx = kInvalidIdx;
              plyr->start_smp_idx = kInvalidIdx;
              
            }

          }
        }

      errLabel:

        
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .notify  = std_notify<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    

    
    //------------------------------------------------------------------------------------------------------------------
    //
    // vel_table
    //
    namespace vel_table
    {
      enum {
        kVelTblFnamePId,
        kVelTblLabelPId,
        kInPId,
        kOutPId,
        kRecdBufNPId
      };
      
      typedef struct vel_tbl_str
      {
        unsigned* tblA;
        unsigned  tblN;
        char*     label;
        struct vel_tbl_str* link;
      } vel_tbl_t;
      
      typedef struct
      {
        vel_tbl_t*    velTblL;
        vel_tbl_t*    activeVelTbl;
        unsigned      i_midi_fld_idx;
        unsigned      i_score_vel_fld_idx;
        unsigned      o_midi_fld_idx;
        
        recd_array_t*   recd_array;  // output record array        
        midi::ch_msg_t* midiA;       // midiA[midiN] output MIDI msg array
        unsigned        midiN; 
      } inst_t;

      rc_t _load_vel_table_file( proc_t* proc, inst_t* p, const char* vel_tbl_fname )
      {
        rc_t            rc    = kOkRC;
        object_t*       cfg   = nullptr;
        const object_t* tblL  = nullptr;
        unsigned        tblN  = 0;
        char*           fname = nullptr;

        if( vel_tbl_fname == nullptr || textLength(vel_tbl_fname)==0  )
        {
          rc = proc_error(proc,kInvalidArgRC,"The velocity table filename is blank.");
          goto errLabel;
        }

        if((fname = proc_expand_filename( proc, vel_tbl_fname )) == nullptr )
        {
          rc = proc_error(proc,kOpFailRC,"The velocity table filename (%s) is invalid.",vel_tbl_fname);
          goto errLabel;
        }
        
        if((rc = objectFromFile(fname,cfg)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Velocity table file parse failed.");
          goto errLabel;
        }

        if((rc = cfg->getv("tables",tblL)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Velocity table file has no 'tables' field.");
          goto errLabel;
        }

        tblN = tblL->child_count();

        for(unsigned i=0; i<tblN; ++i)
        {
          const object_t* tbl        = tblL->child_ele(i);
          const object_t* velListCfg = nullptr;
          vel_tbl_t*      vt         = nullptr;          
          const char*     label      = nullptr;

          if((rc = tbl->getv("table",velListCfg,
                             "name",label)) != kOkRC )
          {
            rc = proc_error(proc,rc,"Velocity table at index %i failed.",i);
            goto errLabel;
          }

          vt         = mem::allocZ<vel_tbl_t>();
          vt->link   = p->velTblL;
          p->velTblL = vt;          
          vt->tblN   = velListCfg->child_count();
          vt->label  = mem::duplStr(label);

          // if the table is empty
          if( vt->tblN == 0 )
          {
            rc = proc_error(proc,rc,"The velocity table named '%s' appears to be blank.",cwStringNullGuard(label));
            continue;
          }

          vt->tblA = mem::allocZ<unsigned>(vt->tblN);

          for(unsigned j=0; j<vt->tblN; ++j)
          {
            const object_t* intCfg;
            
            if((intCfg = velListCfg->child_ele(j)) == nullptr )
            {
              rc = proc_error(proc,rc,"Access to the integer value at index %i failed on vel. table '%s'.",j,cwStringNullGuard(label));
              goto errLabel;
            }
            
            if((rc = intCfg->value(vt->tblA[j])) != kOkRC )
            {              
              rc = proc_error(proc,rc,"Parse failed on integer value at index %i in vel. table '%s'.",j,cwStringNullGuard(label));
              goto errLabel;
            }            
          }
          
        }
        

      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Score velocity table file load failed on '%s'.",cwStringNullGuard(vel_tbl_fname));          

        if( cfg != nullptr )
          cfg->free();
        
        mem::release(fname);

        return rc;
      }
      
      rc_t _activate_vel_table( proc_t* proc, inst_t* p, const char* vel_tbl_label )
      {
        for(vel_tbl_t* vt = p->velTblL; vt!=nullptr; vt=vt->link)
          if( textIsEqual(vt->label,vel_tbl_label))
          {
            p->activeVelTbl = vt;
            return kOkRC;
          }

        proc_warn(proc,"The requested velocity table '%s' was not found on the score instance '%s:%i'.",vel_tbl_label,proc->label, proc->label_sfx_id);
        
        return kOkRC;
      }

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t    rc   = kOkRC;        

        const char* vel_tbl_fname = nullptr;
        const char* vel_tbl_label = nullptr;
        const rbuf_t* rbuf = nullptr;
        rbuf_t* o_rbuf = nullptr;
        unsigned recdBufN = 128;
        
        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kVelTblFnamePId,   "vel_tbl_fname", kBaseSfxId, vel_tbl_fname,
                                      kVelTblLabelPId,   "vel_tbl_label", kBaseSfxId, vel_tbl_label,
                                      kRecdBufNPId,      "recdbufN",      kBaseSfxId, recdBufN,
                                      kInPId,            "in",            kBaseSfxId, rbuf)) != kOkRC )
        {
          goto errLabel;
        }

        // load p->velTblL from the vel table file
        if((rc = _load_vel_table_file( proc, p, vel_tbl_fname )) != kOkRC )
        {
          goto errLabel;
        }

        // activate the selected velocity table
        if((rc = _activate_vel_table( proc, p, vel_tbl_label )) != kOkRC )
        {
          goto errLabel;
        }

        // get the record field index for the incoming record
        if((p->i_midi_fld_idx = recd_type_field_index( rbuf->type, "midi")) == kInvalidIdx )
        {
          rc = proc_error(proc,kInvalidArgRC,"The incoming record does not have a 'midi' field.");
          goto errLabel;                          
        }

        
        // create one output record buffer
        if((rc = var_alloc_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, p->recd_array )) != kOkRC )
        {
          goto errLabel;
        }

        // create the internal record array
        //if((rc = recd_array_create( p->recd_array, rbuf->type, nullptr,  rbuf->maxRecdN )) != kOkRC )
        //{
        //  rc = proc_error(proc,rc,"The internal record array create failed.");
        //  goto errLabel;                                    
        //}
        
        // get the record field index for the outgoing record
        if((p->o_midi_fld_idx = recd_type_field_index( p->recd_array->type, "midi")) == kInvalidIdx )
        {
          rc = proc_error(proc,kInvalidArgRC,"The outgoing record does not have a 'midi' field.");
          goto errLabel;                          
        }

        
        p->midiN = p->recd_array->allocRecdN;
        p->midiA = mem::allocZ<midi::ch_msg_t>(p->midiN);

        // If the velocity table is being fed by the score follower then there may be a 'score_vel' field in the input record.
        p->i_score_vel_fld_idx = recd_type_field_index( rbuf->type, "score_vel");

      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        vel_tbl_t* vt=p->velTblL;
        
        while( vt!=nullptr )
        {
          vel_tbl_t* vt0 = vt->link;
          mem::release(vt->label);
          mem::release(vt->tblA);
          mem::release(vt);
          vt = vt0;
        }

        mem::release(p->midiA);
        recd_array_destroy(p->recd_array);
        return rc;
      }

      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;

        const rbuf_t* i_rbuf = nullptr;
        rbuf_t*       o_rbuf = nullptr;

        if((rc = var_get(proc,kInPId,kAnyChIdx,i_rbuf)) != kOkRC )
          goto errLabel;

        if((rc = var_get(proc,kOutPId,kAnyChIdx,o_rbuf)) != kOkRC )
          goto errLabel;

        
        // for each incoming record
        for(unsigned i=0; i<i_rbuf->recdN; ++i)
        {
          const recd_t*         i_r = i_rbuf->recdA + i;          
          const midi::ch_msg_t* i_m = nullptr;
          
          // verify that there is space in the output array
          if( i >= p->midiN || i >= p->recd_array->allocRecdN )
          {
            rc = proc_error(proc,kBufTooSmallRC,"The velocity table MIDI processing buffers overflow (%i).",i);
            goto errLabel;
          }

          // Get pointers to the output records
          recd_t*         o_r = p->recd_array->recdA + i;
          midi::ch_msg_t* o_m = p->midiA + i;

          // get a pointer to the incoming MIDI record
          if((rc = recd_get(i_rbuf->type,i_r,p->i_midi_fld_idx,i_m)) != kOkRC )
          {
            rc = proc_error(proc,rc,"Record 'midi' field read failed.");
            goto errLabel;
          }

          // copy the incoming MIDI record to the output array
          *o_m = *i_m;

          // if this is a note on
          if( midi::isNoteOn(i_m->status,i_m->d1) )
          {

            // if the 'score_vel' field was not given
            if( p->i_score_vel_fld_idx == kInvalidIdx )
            {
              // and the velocity is valid
              if( i_m->d1 >= p->activeVelTbl->tblN )
              {
                rc = proc_error(proc,kInvalidArgRC,"The pre-mapped velocity value %i is outside of the range (%i) of the velocity table '%s'.",i_m->d1,p->activeVelTbl->tblN,cwStringNullGuard(p->activeVelTbl->label));
                goto errLabel;
              }
              
              // map the velocity through the active table
              o_m->d1 = p->activeVelTbl->tblA[ i_m->d1 ];
            }
            else  // ... a 'score_vel' exists
            {
              unsigned score_vel = -1;

              // get the score_vel
              if((rc = recd_get(i_rbuf->type,i_r,p->i_score_vel_fld_idx,score_vel)) != kOkRC )
              {
                rc = proc_error(proc,kOpFailRC,"'score_velocity access failed in velocity table.");
                goto errLabel;
              }

              // if the score_vel is valid (it won't be if this note was not tracked in the score)
              if( score_vel != (unsigned)-1 )
              {
                // verify that the 'score_vel' is inside the range of the table
                if(score_vel >= p->activeVelTbl->tblN )
                {
                  rc = proc_error(proc,kInvalidArgRC,"The pre-mapped score velocity value %i is outside of the range (%i) of the velocity table '%s'.",score_vel,p->activeVelTbl->tblN,cwStringNullGuard(p->activeVelTbl->label));
                  goto errLabel;                  
                }

                // apply the score_vel to the map
                o_m->d1 = p->activeVelTbl->tblA[ score_vel ];
              }
              
            }

            //printf("%i %i %s\n",i_m->d1,o_m->d1,p->activeVelTbl->label);
          }

          // update the MIDI pointer in the output record 
          recd_set(o_rbuf->type,nullptr,o_r,p->o_midi_fld_idx, o_m );
        }

        //printf("RECDN:%i\n",i_rbuf->recdN);
        
        o_rbuf->recdA = p->recd_array->recdA;
        o_rbuf->recdN = i_rbuf->recdN;        
        
      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Vel table exec failed.");
                          
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .notify  = std_notify<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    

    //------------------------------------------------------------------------------------------------------------------
    //
    // preset_select
    //
    namespace preset_select
    {
      enum {
        kInPId,
        kInitCfgPId,
        kXfCntPId,
        kFNamePId,
        kLocPId,
        kOutIdxPId,
        kPresetLabelPId,
      };
      
      typedef struct
      {
        const char*          preset_proc_label; // proc containing preset label->value mapping
        unsigned             xf_cnt;        // count of transform processors 
        preset_sel::handle_t psH;           // location->preset map
        unsigned             loc_fld_idx;   // 
        unsigned             loc;           // Last received location
        unsigned             out_idx;       // Current transform processor index (0:xf_cnt)
        unsigned             presetN;       // Count of preset labels. Same as preset_count(psH).
        unsigned             preset_idx;    // Index (0:presetN) of last selected preset.
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t        rc    = kOkRC;        
        const char* fname = nullptr;
        rbuf_t*     rbuf;
        const object_t* cfg = nullptr;
        char* exp_fname = nullptr;
        
        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kInitCfgPId,  "cfg",     kBaseSfxId, cfg,
                                      kInPId,       "in",      kBaseSfxId, rbuf,
                                      kFNamePId,    "fname",   kBaseSfxId, fname,
                                      kLocPId,      "loc",     kBaseSfxId, p->loc)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_register_and_set(proc,kAnyChIdx,kPresetLabelPId,"preset_label", kBaseSfxId, "")) != kOkRC )
        {
          goto errLabel;
        }
                                      

        if((exp_fname = proc_expand_filename(proc,fname)) == nullptr )
        {
          rc = proc_error(proc,kOpFailRC,"Preset filename expansion failed.");
          goto errLabel;
        }

        // create the cwPresetSel object
        if(cfg==nullptr || (rc = preset_sel::create(p->psH,cfg)) != kOkRC )
        {
          rc = proc_error(proc,kOpFailRC,"The preset select object could not be initialized.");
          goto errLabel;
        }

        // read in the loc->preset map file
        if((rc = preset_sel::read(p->psH,exp_fname)) != kOkRC )
        {
          rc = proc_error(proc,rc,"The preset_sel data file '%s' could not be read.",cwStringNullGuard(exp_fname));
          goto errLabel;
        }

        // The location is coming from a 'record', get the location field.
        if((p->loc_fld_idx  = recd_type_field_index( rbuf->type, "loc")) == kInvalidIdx )
        {
          rc = proc_error(proc,kInvalidArgRC,"The 'in' record does not have a 'loc' field.");
          goto errLabel;
        }

        p->presetN = preset_count(p->psH);
        
      errLabel:
        mem::release(exp_fname);
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        preset_sel::destroy(p->psH);

        return rc;
      }

      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;
        rbuf_t* in_rbuf = nullptr;
        unsigned loc = kInvalidIdx;

        if((rc = var_get(proc,kInPId,kAnyChIdx,in_rbuf)) != kOkRC)
          goto errLabel;
        
        // for each incoming record
        for(unsigned i=0; i<in_rbuf->recdN; ++i)
        {

          // get the 'loc' field
          if((rc = recd_get( in_rbuf->type, in_rbuf->recdA+i, p->loc_fld_idx, loc)) != kOkRC )
          {
            rc = proc_error(proc,rc,"The 'loc' field read failed.");
            goto errLabel;
          }
        }

        if( loc != kInvalidIdx )
        {        
          //rbuf_t*                   xf_rbuf    = nullptr;
          const preset_sel::frag_t* frag       = nullptr;
          unsigned                  preset_idx = kInvalidIdx;
          
          // lookup the fragment associated with the location
          if( preset_sel::track_loc( p->psH, loc, frag ) && frag != nullptr )
          {
            // get the preset index associated with the current frag
            if((preset_idx = fragment_play_preset_index(p->psH, frag )) == kInvalidIdx )
            {
              rc = proc_error(proc,kInvalidArgRC,"The current frag does not have valid preset associated with it.");
              goto errLabel;
            }

            // validate the preset index
            if( preset_idx >= p->presetN )
            {
              rc = proc_error(proc,kAssertFailRC,"The selected preset index %i is out of range.",preset_idx);
              goto errLabel;
            }
            

            if( preset_idx != p->preset_idx )
            {
            
              if( preset_idx != kInvalidIdx )
              {
                //printf("PRE-SEL:loc:%i %i %s\n",loc,preset_idx,preset_sel::preset_label(p->psH,preset_idx));
                if((rc = var_set(proc, kPresetLabelPId, kAnyChIdx, preset_sel::preset_label(p->psH,preset_idx))) != kOkRC )
                  goto errLabel;
              }
            }
          }
        
        }
        
      errLabel:
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .notify  = std_notify<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    

    //------------------------------------------------------------------------------------------------------------------
    //
    // gutim_ps
    //
    namespace gutim_ps
    {
      enum {
        kPolyCntPId,
        kInitCfgPId,
        kPresetMapCfgPId,
        kFNamePId,
        kLocCntPId,
        kInPId,
        kLocPId,
        kResetPId,
        kPriManualSelPId,
        kSecManualSelPId,
        kPerNoteFlPId,
        kPerLocFlPId,
        kDryChordFlPId,
        
        kPriProbFlPId,
        kPriUniformFlPId,
        kPriDryOnPlayFlPId,
        kPriAllowAllFlPId,
        kPriDryOnSelFlPId,

        kInterpFlPId,
        kInterpDistPId,
        kInterpRandFlPId,
        
        kSecProbFlPId,
        kSecUniformFlPId,
        kSecDryOnPlayFlPId,
        kSecAllowAllFlPId,
        kSecDryOnSelFlPId,


        
        kMinPId,
        kMidiInPId = kMinPId,        
        kWndSmpCntPId,
        kCeilingPId,
        kExpoPId,
        kThreshPId,
        kUprPId,
        kLwrPId,
        kMixPId,
        kCIGainPId,
        kCOGainPId,
        kDryGainPId,
        kMaxPId
      };

      enum {
        kPresetVarN = kMaxPId - kMinPId,
        kMaxChN = 2 // all output variables are assume stereo values
      };
      
      typedef enum {
        kNoPresetValTId,
        kUIntPresetValTId,
        kCoeffPresetValTId,
      } value_tid_t;

      typedef struct loc_str
      {
        unsigned pri_preset_idx;
        unsigned sec_preset_idx;
        unsigned note_cnt;
        unsigned note_idx;
        unsigned rand;
      } loc_t;

      typedef struct var_cfg_str
      {
        const char* var_label;     // gutim_ps var label
        unsigned    var_pid;       // gutim_ps var pid
        const char* cls_label;     // preset class proc label
        const char* cls_var_label; // preset class proc var label
        value_tid_t tid;           // gutim_ps var data type
      } var_cfg_t;
                        
      typedef struct 
      {
        value_tid_t tid; // k???PresetValTId
        union {
          unsigned uint;
          coeff_t  coeff;
        } u;
      } preset_value_t;

      typedef struct preset_var_str
      {
        preset_value_t chA[ kMaxChN ];
      } preset_var_t;

      typedef struct
      {
        const char*    ps_label;
        const char*    cls_label;
        preset_var_t   varA[ kPresetVarN ];        
      } preset_t;


      // One var_cfg record for each transform parameter that gutim_ps outputs
      var_cfg_t _var_cfgA[] = {
        { "wnd_smp_cnt", kWndSmpCntPId, "pv_analysis", "wndSmpN", kUIntPresetValTId },
        { "ceiling",     kCeilingPId,   "spec_dist",   "ceiling", kCoeffPresetValTId },
        { "expo",        kExpoPId,      "spec_dist",   "expo",    kCoeffPresetValTId },
        { "thresh",      kThreshPId,    "spec_dist",   "thresh",  kCoeffPresetValTId },
        { "upr",         kUprPId,       "spec_dist",   "upr",     kCoeffPresetValTId },
        { "lwr",         kLwrPId,       "spec_dist",   "lwr",     kCoeffPresetValTId },
        { "mix",         kMixPId,       "spec_dist",   "mix",     kCoeffPresetValTId },
        { "c_igain",     kCIGainPId,    "compressor",  "igain",   kCoeffPresetValTId },
        { "c_ogain",     kCOGainPId,    "compressor",  "ogain",   kCoeffPresetValTId },
        { "dry_gain",    kDryGainPId,   "gutim_ps",    "dry_gain",kCoeffPresetValTId },
        { nullptr,       kMaxPId,       nullptr,       nullptr,   kNoPresetValTId },
        
      };

      // One record for each of the gutim presets. preset_t.varA[] contains the value for each preset 
      preset_t _presetA[] = {
        { "dry","dry",{} },
        { "a","a",{} },
        { "b","b",{} },
        { "c","c",{} },
        { "d","d",{} },
        { "f1","f_1",{} },
        { "f2","f_2",{} },
        { "f3","f_3",{} },
        { "f4","f_4",{} },
        { "g","g",{} },
        { "ga","g_a",{} },
        { "g1a","g_1_a",{} },
        { "g1d","g_1_d",{} },
      };

      typedef struct
      {
        unsigned             polyN;
        preset_sel::handle_t psH;          // location->preset map
        unsigned             loc_fld_idx;
        unsigned             base[ kMaxPId ];  // base PId's for the poly var's: kMinPId - kMaxPId)
        unsigned             psPresetCnt;

        preset_t* presetA;   // presetA[ presetN ] Preset variable values associated with each of the preset labels (e.g. a,b,c, ... g,ga,g1a,g1d)
        unsigned  presetN;    //                    
        
        const preset_sel::frag_t* cur_frag;
        unsigned                  cur_pri_preset_idx;
        unsigned                  cur_sec_preset_idx;
        coeff_t                   cur_interp_dist;

        bool per_note_fl;
        bool per_loc_fl;
        bool dry_chord_fl;
        bool pri_prob_fl;
        bool pri_uniform_fl;
        bool pri_dry_on_play_fl;
        bool pri_allow_all_fl;
        bool pri_dry_on_sel_fl;

        bool sec_prob_fl;
        bool sec_uniform_fl;
        bool sec_dry_on_play_fl;
        bool sec_allow_all_fl;
        bool sec_dry_on_sel_fl;

        bool    interp_fl;
        bool    interp_rand_fl;

        list_t*  manual_sel_list;
        unsigned cur_manual_pri_preset_idx;
        unsigned cur_manual_sec_preset_idx;

        loc_t*   locA;
        unsigned locN;
        unsigned dry_preset_idx;
        
      } inst_t;

      void _init_loc_array( inst_t* p )
      {
        if( p->locN > 0 && p->locA == nullptr )
          p->locA = mem::allocZ<loc_t>(p->locN);

        for(unsigned i=0; i<p->locN; ++i)
        {
          p->locA[i].pri_preset_idx = kInvalidIdx;
          p->locA[i].sec_preset_idx = kInvalidIdx;
          p->locA[i].note_cnt       = 0;
          p->locA[i].note_idx       = kInvalidIdx;
          p->locA[i].rand           = rand();
        }
      }

      const char* _preset_index_to_label( inst_t* p, unsigned preset_idx )
      {
        const char* label = "<none>";
        assert( preset_idx == kInvalidIdx || preset_idx < p->presetN );
        
        if( preset_idx != kInvalidIdx && preset_idx < p->presetN )
          label = _presetA[  preset_idx ].ps_label;
        return label;
      }

      rc_t _create_manual_select_list( proc_t* proc, inst_t* p )
      {
        rc_t           rc           = kOkRC;
        const char*    var_labelA[] = { "pri_manual_sel", "sec_manual_sel" };
        const unsigned var_labelN   = sizeof(var_labelA)/sizeof(var_labelA[0]);

        p->cur_manual_pri_preset_idx = kInvalidIdx;
        p->cur_manual_sec_preset_idx = kInvalidIdx;
        
        // create the list of values for the 'manual_sel' variable
        if((rc = list_create(p->manual_sel_list, p->presetN+1 )) != kOkRC )
          goto errLabel;

        if((rc = list_append(p->manual_sel_list,"auto",kInvalidIdx)) != kOkRC )
          goto errLabel;
        
        for(unsigned i=0; i<p->presetN; ++i)
          if((rc = list_append( p->manual_sel_list, _preset_index_to_label(p,i), i)) != kOkRC )
            goto errLabel;
        

        for(unsigned i=0; i<var_labelN; ++i)
        {
          variable_t* var = nullptr;

          if((rc = var_find(proc, var_labelA[i], kBaseSfxId, kAnyChIdx, var )) != kOkRC )
          {
            rc = proc_error(proc,rc,"The '%s' variable could not be found.",var_labelA[i]);
            goto errLabel;
          }

          var->value_list = p->manual_sel_list;
        }
        
      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"The 'gutim_ps' manual selection list create failed.");
        return rc;
      }
      
      template< typename T >
      rc_t _read_class_preset_value( proc_t* proc, preset_t* preset, var_cfg_t* var_cfg,  unsigned ch_idx, T& val_ref )
      {
        rc_t rc;
        
        if((rc = class_preset_value( proc->ctx, var_cfg->cls_label, preset->cls_label, var_cfg->cls_var_label, ch_idx, val_ref )) != kOkRC )
        {
          rc = proc_error(proc,rc,"The preset value could not be accessed for the preset '%s' from '%s:%s' ch:%i.",preset->cls_label,var_cfg->cls_label,var_cfg->cls_var_label,ch_idx);
          goto errLabel;
        }

      errLabel:
        return rc;
      }

      
      rc_t _read_class_preset_value( proc_t* proc, preset_t* preset, var_cfg_t* var_cfg,  unsigned var_idx )
      {

        rc_t     rc                = kOkRC;
        unsigned chN               = 0;
        bool     preset_has_var_fl = false;

        //
        // TODO: It should be an error if a given preset does not always reference a variable that it may reference in another preset
        // This would imply that the value is simply left in it's current state - which might lead to unpredicitable results.
        //
        // Therefore class_preset_has_var() should always be true.
        //
        
        if((rc = class_preset_has_var( proc->ctx, var_cfg->cls_label, preset->cls_label, var_cfg->cls_var_label, preset_has_var_fl )) != kOkRC )
        {
          rc = proc_error(proc,rc,"The class preset variable list could not be accessed for the preset '%s' from '%s:%s'.",preset->cls_label,var_cfg->cls_label,var_cfg->cls_var_label);
          goto errLabel;          
        }

        if( preset_has_var_fl )
        {
          if((rc = class_preset_value_channel_count( proc->ctx, var_cfg->cls_label, preset->cls_label, var_cfg->cls_var_label, chN )) != kOkRC )
          {
            rc = proc_error(proc,rc,"The class preset channel count could not be accessed for the preset '%s' from '%s:%s'.",preset->cls_label,var_cfg->cls_label,var_cfg->cls_var_label);
            goto errLabel;
          }

          if( chN > kMaxChN  )
          {          
            rc = proc_error(proc,rc,"Thethe preset '%s' from '%s:%s' has more channels (%i) than can be processed (%i).",preset->cls_label,var_cfg->cls_label,var_cfg->cls_var_label,chN,kMaxChN);
            goto errLabel;
          }
        }

        // We always set all the preset->varA[].chA[] values - even if a
        // particular preset does not specify multiple channels.
        for(unsigned i=0; i<kMaxChN; ++i)
        {
          // If the preset specifies fewer channels than are required (kMaxChN) then fill the extra
          // channels value from the last available channel specified in the preset
          unsigned ch_idx = i<chN ? i : chN-i;

          preset_value_t *v = &preset->varA[var_idx].chA[i];

          // if this preset does not reference this variable
          v->tid = preset_has_var_fl ? var_cfg->tid : kNoPresetValTId;
          
          switch( v->tid )
          {
            case kNoPresetValTId:
              break;
              
            case kUIntPresetValTId:
              rc = _read_class_preset_value( proc, preset, var_cfg, ch_idx, v->u.uint );
              break;
              
            case kCoeffPresetValTId:
              rc = _read_class_preset_value( proc, preset, var_cfg, ch_idx, v->u.coeff );
              break;
              
            default:
              rc = proc_error(proc,kInvalidDataTypeRC,"An invalid variable value data type (%i) was encountered.",var_cfg->tid);
          }
        }

        if(rc != kOkRC )
        {
          rc = proc_error(proc,rc,"The preset value for the variable '%s' for preset '%s' from '%s:%s' could not be accessed.", var_cfg->var_label, preset->cls_label, var_cfg->cls_label, var_cfg->cls_var_label );
          goto errLabel;
        }
        

      errLabel:
        return rc;

      }

      rc_t _create_and_fill_preset_array( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        p->presetA = _presetA;
        p->presetN = sizeof(_presetA)/sizeof(_presetA[0]);

        // BUG BUG BUG: what is the point of storing both psPresetCnt and presetN if they are the same value?
        assert( p->presetN == p->psPresetCnt );

        // for each preset
        for(unsigned i=0; i< p->presetN; ++i)
        {
          preset_t* preset = p->presetA + i;

          // for each value of interest in this preset
          for(unsigned j=0; _var_cfgA[j].var_label!=nullptr; ++j)
          {
            assert( j < kPresetVarN );

            // get the value of the preset variable
            if((rc = _read_class_preset_value( proc, preset, _var_cfgA + j, j)) != kOkRC )
            {
              goto errLabel;
            }
          }
        }
        
      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Preset value initialization failed.");
        return rc;        
      }

      rc_t _apply_preset_no_interp(proc_t* proc, inst_t* p, unsigned voice_idx, unsigned preset_idx)
      {
        rc_t rc = kOkRC;

        if( preset_idx == kInvalidIdx || preset_idx >= p->presetN )
        {
          rc = proc_error(proc,kInvalidArgRC,"The primary preset is invalid.");
          goto errLabel;
        }
        
        for(unsigned var_idx=0; _var_cfgA[var_idx].var_label!=nullptr; ++var_idx)
        {
          const var_cfg_t* var_cfg = _var_cfgA + var_idx;
          
          assert( preset_idx < p->presetN );
          
          for(unsigned ch_idx=0; ch_idx<kMaxChN; ++ch_idx )
          {            
            const preset_value_t* v = p->presetA[ preset_idx ].varA[ var_idx ].chA + ch_idx;
            
            variable_t* varb;
            var_find(proc, p->base[var_cfg->var_pid] + voice_idx,ch_idx, varb);
            
            switch( v->tid )
            {
              case kNoPresetValTId:
                // this preset does not reference this variable
                break;
                
              case kUIntPresetValTId:
                //printf("PS: %i %s %s %i\n",ch_idx,var_cfg->var_label,varb->label,v->u.uint);
                var_set(proc, p->base[var_cfg->var_pid] + voice_idx, ch_idx, v->u.uint );
                break;

              case kCoeffPresetValTId:
                //printf("PS: %i %s %s %f\n",ch_idx,var_cfg->var_label,varb->label,v->u.coeff);
                var_set(proc, p->base[var_cfg->var_pid] + voice_idx, ch_idx, v->u.coeff );
                break;

              default:
                rc = proc_error(proc,kInvalidArgRC,"Unknown preset value type:%i on %s.",v->tid,cwStringNullGuard(var_cfg->var_label));
                goto errLabel;
            }
          }
        }

      errLabel:
        return rc;
      }


      rc_t _apply_preset_with_interp(proc_t* proc, inst_t* p, unsigned voice_idx, unsigned pri_preset_idx, unsigned sec_preset_idx)
      {
        rc_t rc = kOkRC;

        if( pri_preset_idx == kInvalidIdx || pri_preset_idx >= p->presetN )
        {
          rc = proc_error(proc,kInvalidArgRC,"The primary preset is invalid.");
          goto errLabel;
        }
        
        if( sec_preset_idx == kInvalidIdx || sec_preset_idx >= p->presetN )
        {
          rc = proc_error(proc,kInvalidArgRC,"The secondary preset is invalid.");
          goto errLabel;
        }
        
        for(unsigned var_idx=0; _var_cfgA[var_idx].var_label!=nullptr; ++var_idx)
        {
          const var_cfg_t* var_cfg = _var_cfgA + var_idx;
          
          assert( pri_preset_idx < p->presetN );
          
          for(unsigned ch_idx=0; ch_idx<kMaxChN; ++ch_idx )
          {
            
            const preset_value_t* c0 = p->presetA[ pri_preset_idx ].varA[ var_idx ].chA + ch_idx;
            const preset_value_t* c1 = p->presetA[ sec_preset_idx ].varA[ var_idx ].chA + ch_idx;
            
            switch( var_cfg->tid )
            {
              case kNoPresetValTId:
                // this preset does not reference this variable
                break;
                
              case kUIntPresetValTId:
                {
                  uint_t v0 = std::min(c0->u.uint,c1->u.uint);
                  uint_t v1 = std::max(c0->u.uint,c1->u.uint);
                  uint_t v = (unsigned)(v0 + p->cur_interp_dist * (v1 - v0));
                  var_set(proc, p->base[var_cfg->var_pid] + voice_idx, ch_idx, v );
                }
                break;

              case kCoeffPresetValTId:
                {
                  coeff_t v0 = std::min(c0->u.coeff,c1->u.coeff);
                  coeff_t v1 = std::max(c0->u.coeff,c1->u.coeff);
                  coeff_t v = v0 + p->cur_interp_dist * (v1 - v0);
                  var_set(proc, p->base[var_cfg->var_pid] + voice_idx, ch_idx, v );
                }
                break;

              default:
                rc = proc_error(proc,kInvalidArgRC,"Unknown preset value type:%i on %s.",var_cfg->tid,cwStringNullGuard(var_cfg->var_label));
                goto errLabel;
            }
          }
        }

      errLabel:
        return rc;
      }

      void _report_preset( proc_t* proc, inst_t* p, const char* label, unsigned preset_idx )
      {
        cwLogPrint("%s : ",label);
        
        if( p->cur_frag == nullptr )
        {
          cwLogPrint("No location (frag) selected.");
          goto errLabel;
        }
        
        if( preset_idx == kInvalidIdx )
        {
          cwLogPrint("No preset selected.");
          goto errLabel;
        }

        for(unsigned i=0; i<p->cur_frag->probDomN; ++i)
        {
          bool fl = p->cur_frag->probDomA[i].index == preset_idx;
          cwLogPrint("%s%s%s ",fl?"(":"",p->presetA[ preset_idx ].ps_label,fl?")":"");
        }

      errLabel:
        return;
      }
      
      void _print_preset( proc_t* proc, inst_t* p )
      {
        _report_preset( proc, p, "Pri", p->cur_pri_preset_idx );
        
        if( p->cur_sec_preset_idx != kInvalidIdx )
          _report_preset( proc, p, "Sec", p->cur_sec_preset_idx );
      }

      // apply the preset assoc'd with p->cur_pri_preset_idx and p->cur_sec_preset_idx
      rc_t _apply_preset( proc_t* proc, inst_t* p, const midi::ch_msg_t* m, unsigned voice_idx, unsigned pri_preset_idx, unsigned sec_preset_idx )
      {
        rc_t rc = kOkRC;
        pri_preset_idx = p->cur_manual_pri_preset_idx == kInvalidIdx ? pri_preset_idx : p->cur_manual_pri_preset_idx;
        sec_preset_idx = p->cur_manual_sec_preset_idx == kInvalidIdx ? sec_preset_idx : p->cur_manual_sec_preset_idx;

        if( pri_preset_idx == kInvalidIdx )
        {
          rc = proc_error(proc,kInvalidStateRC,"No current preset has been selected.");
          goto errLabel;
        }

        if( !p->interp_fl || sec_preset_idx == kInvalidIdx )
        {
          rc = _apply_preset_no_interp(proc, p, voice_idx, pri_preset_idx);
        }
        else
        {
          rc = _apply_preset_with_interp(proc, p, voice_idx, pri_preset_idx, sec_preset_idx);
        }

      errLabel:
        if( rc != kOkRC )
          proc_error(proc,rc,"Preset application failed.");
        return rc;
      }

      rc_t _update_cur_preset_idx( proc_t* proc, inst_t* p, const preset_sel::frag_t* f )
      {
        if( f == nullptr )
          return proc_error(proc,kInvalidArgRC,"Cannot update current selected preset if no location value has been set.");
        
        rc_t     rc             = kOkRC;
        unsigned flags          = 0;

        p->cur_pri_preset_idx = kInvalidIdx;
        p->cur_sec_preset_idx = kInvalidIdx;
        

        var_get(proc, kInterpFlPId, kAnyChIdx, p->interp_fl);

        if( p->pri_prob_fl )
          flags += preset_sel::kUseProbFl;
        
        if( p->pri_uniform_fl )
          flags += preset_sel::kUniformFl;
        
        if( p->pri_dry_on_play_fl )
          flags += preset_sel::kDryOnPlayFl;
        
        if( p->pri_allow_all_fl )
          flags += preset_sel::kAllowAllFl;
        
        if( p->pri_dry_on_sel_fl )
          flags += preset_sel::kDryOnSelFl;
        
        p->cur_pri_preset_idx = prob_select_preset_index( p->psH, f, flags );

        if( p->interp_fl )
        {
          flags = 0;

          if( p->sec_prob_fl )
            flags += preset_sel::kUseProbFl;
        
          if( p->sec_uniform_fl )
            flags += preset_sel::kUniformFl;
        
          if( p->sec_dry_on_play_fl )
            flags += preset_sel::kDryOnPlayFl;
        
          if( p->sec_allow_all_fl )
            flags += preset_sel::kAllowAllFl;
        
          if( p->sec_dry_on_sel_fl )
            flags += preset_sel::kDryOnSelFl;
        
          p->cur_sec_preset_idx = prob_select_preset_index( p->psH, f, flags, p->cur_pri_preset_idx );

          if( p->interp_rand_fl )
            p->cur_interp_dist = std::max(0.0f,std::min(1.0f, (coeff_t)rand() / RAND_MAX ));
          else
            var_get(proc, kInterpDistPId, kAnyChIdx, p->cur_interp_dist );
        }

        proc_info(proc,"Preset:%s%s%s",_preset_index_to_label(p,p->cur_pri_preset_idx), p->interp_fl ? "->":" ",_preset_index_to_label(p,p->cur_sec_preset_idx));
        return rc;
      }
      

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t            rc          = kOkRC;        
        const char*     fname       = nullptr;
        unsigned        loc         = kInvalidIdx;
        rbuf_t*         rbuf        = nullptr;
        char*           exp_fname   = nullptr;
        const object_t* cfg         = nullptr;
        bool            resetFl     = false;

        p->dry_preset_idx = kInvalidIdx;
          
        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kInitCfgPId,      "cfg",            kBaseSfxId, cfg,        // TODO: clean up the contents of this CFG
                                      kInPId,           "in",             kBaseSfxId, rbuf,
                                      kFNamePId,        "fname",          kBaseSfxId, fname,
                                      kLocCntPId,       "loc_cnt",        kBaseSfxId, p->locN,
                                      kLocPId,          "loc",            kBaseSfxId, loc,
                                      kResetPId,        "reset",          kBaseSfxId, resetFl,
                                      kPriManualSelPId, "pri_manual_sel", kBaseSfxId, p->cur_manual_pri_preset_idx,
                                      kSecManualSelPId, "sec_manual_sel", kBaseSfxId, p->cur_manual_sec_preset_idx,
                                      kPerNoteFlPId,    "per_note_fl",    kBaseSfxId, p->per_note_fl,
                                      kPerLocFlPId,     "per_loc_fl",     kBaseSfxId, p->per_loc_fl,
                                      kDryChordFlPId,   "dry_chord_fl",   kBaseSfxId, p->dry_chord_fl,

                                      kPriProbFlPId,     "pri_prob_fl",        kBaseSfxId, p->pri_prob_fl,                                      
                                      kPriUniformFlPId,  "pri_uniform_fl",     kBaseSfxId, p->pri_uniform_fl,
                                      kPriDryOnPlayFlPId,"pri_dry_on_play_fl", kBaseSfxId, p->pri_dry_on_play_fl,
                                      kPriAllowAllFlPId, "pri_allow_all_fl",   kBaseSfxId, p->pri_allow_all_fl,
                                      kPriDryOnSelFlPId, "pri_dry_on_sel_fl",  kBaseSfxId, p->pri_dry_on_sel_fl,
                                      
                                      kInterpFlPId,      "interp_fl",          kBaseSfxId, p->interp_fl,
                                      kInterpDistPId,    "interp_dist",        kBaseSfxId, p->cur_interp_dist,
                                      kInterpRandFlPId,  "interp_rand_fl",     kBaseSfxId, p->interp_rand_fl,
        
                                      kSecProbFlPId,     "sec_prob_fl",        kBaseSfxId, p->sec_prob_fl,                                      
                                      kSecUniformFlPId,  "sec_uniform_fl",     kBaseSfxId, p->sec_uniform_fl,
                                      kSecDryOnPlayFlPId,"sec_dry_on_play_fl", kBaseSfxId, p->sec_dry_on_play_fl,
                                      kSecAllowAllFlPId, "sec_allow_all_fl",   kBaseSfxId, p->sec_allow_all_fl,
                                      kSecDryOnSelFlPId, "sec_dry_on_sel_fl",  kBaseSfxId, p->sec_dry_on_sel_fl )) != kOkRC )
        {
          goto errLabel;
        }

        if( (p->polyN = var_mult_count(proc,"midi_in")) == kInvalidCnt || p->polyN == 0 )
        {
          rc = proc_error(proc,kInvalidArgRC,"The 'midi_in' must be connected to a 'mult' source with at least one 'mult' instance.");
          goto errLabel;
        }
        
        if((exp_fname = proc_expand_filename(proc,fname)) == nullptr )
        {
          rc = proc_error(proc,kOpFailRC,"Preset filename expansion failed.");
          goto errLabel;
        }

        // create the cwPresetSel object
        if(cfg==nullptr || (rc = preset_sel::create(p->psH,cfg)) != kOkRC )
        {
          rc = proc_error(proc,kOpFailRC,"The preset select object could not be initialized.");
          goto errLabel;
        }

        p->dry_preset_idx = preset_sel::dry_preset_index(p->psH);

        // read in the loc->preset map file
        if((rc = preset_sel::read(p->psH,exp_fname)) != kOkRC )
        {
          rc = proc_error(proc,rc,"The preset_sel data file '%s' could not be read.",cwStringNullGuard(exp_fname));
          goto errLabel;
        }

        //preset_sel::report(p->psH);
        //preset_sel::report_presets(p->psH);
        
        // The location is coming from a 'record', get the location field.
        if((p->loc_fld_idx  = recd_type_field_index( rbuf->type, "loc")) == kInvalidIdx )
        {
          proc_warn(proc,"The incoming record to the 'gutim_ps' object does not have a 'loc' field. Score tracking is disabled.");
          //rc = proc_error(proc,kInvalidArgRC,"The 'in' record does not have a 'loc' field.");
          //goto errLabel;
        }


        // Initialize the base pid's for each of the poly variables
        for(unsigned i=kMinPId; i<kMaxPId; ++i)
          p->base[i] = kMinPId + (i*p->polyN);

        // register the poly variables
        for(unsigned i=0; i<p->polyN; ++i)
        {
          if((rc = var_register(proc, kAnyChIdx, p->base[ kMidiInPId ] + i, "midi_in", kBaseSfxId + i )) != kOkRC )
          {
            rc = proc_error(proc,kInvalidArgRC,"The 'midi_in' registration failed.");
            goto errLabel;          
          }
          
          for(unsigned ch_idx=0; ch_idx<kMaxChN; ++ch_idx)
          {
            if((rc = var_register(proc, ch_idx,
                                  p->base[ kMidiInPId ]    + i, "midi_in",     kBaseSfxId + i,
                                  p->base[ kWndSmpCntPId ] + i, "wnd_smp_cnt", kBaseSfxId + i,
                                  p->base[ kCeilingPId ]   + i, "ceiling",     kBaseSfxId + i,
                                  p->base[ kExpoPId ]      + i, "expo",        kBaseSfxId + i,
                                  p->base[ kThreshPId ]    + i, "thresh",      kBaseSfxId + i,
                                  p->base[ kUprPId ]       + i, "upr",         kBaseSfxId + i,
                                  p->base[ kLwrPId ]       + i, "lwr",         kBaseSfxId + i,
                                  p->base[ kMixPId ]       + i, "mix",         kBaseSfxId + i,
                                  p->base[ kCIGainPId ]    + i, "c_igain",     kBaseSfxId + i,
                                  p->base[ kCOGainPId ]    + i, "c_ogain",     kBaseSfxId + i,
                                  p->base[ kDryGainPId ]   + i, "dry_gain",    kBaseSfxId + i )) != kOkRC )
            {
              goto errLabel;
            }
          }
        }

        p->psPresetCnt = preset_count(p->psH);  // get the count of preset class (~13)

        // Get the values for all the presets required by the transform parameter variables
        if((rc = _create_and_fill_preset_array( proc, p )) != kOkRC )
          goto errLabel;
          

        // create the 'manual_sel' list based on the available preset labels
        if((rc = _create_manual_select_list(proc, p )) != kOkRC )
          goto errLabel;

        // initialize locA[]
        _init_loc_array(p);

      errLabel:
        mem::release(exp_fname);
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        preset_sel::destroy(p->psH);
        
        list_destroy(p->manual_sel_list);
        mem::release(p->locA);
        
        return rc;
      }

      rc_t _exec_note_on( proc_t* proc, inst_t* p, const midi::ch_msg_t* m, unsigned voice_idx )
      {
        rc_t     rc               = kOkRC;
        bool     per_note_fl      = false;
        bool     per_loc_fl       = false;
        bool     chord_dry_fl     = false;
        bool     apply_dry_fl     = false;
        bool     update_preset_fl = false;
        unsigned loc_idx          = kInvalidIdx;
        unsigned pri_preset_idx   = p->cur_pri_preset_idx;
        unsigned sec_preset_idx   = p->cur_sec_preset_idx;

        if((rc = var_get(proc,kPerLocFlPId,kAnyChIdx,per_loc_fl)) != kOkRC)
          goto errLabel;

        // if we are selecting a new preset per location
        if( per_loc_fl && p->locN > 0 )
        {
          
          unsigned loc      = m->devIdx;
          unsigned note_idx = m->portIdx;
          unsigned note_cnt = m->uid;
          assert( loc < p->locN );

          // if this is the first note received for this location
          if( p->locA[ loc ].note_cnt == 0 )
          {
            p->locA[ loc ].note_cnt = note_cnt;
            loc_idx                 = loc;
            update_preset_fl        = true;
          }
          else // ... select the preset based on the preset previously picked for this location
          {
            pri_preset_idx = p->locA[ loc ].pri_preset_idx;
            sec_preset_idx = p->locA[ loc ].sec_preset_idx;
          }

          p->locA[ loc ].note_idx += 1;

          
          var_get(proc,kDryChordFlPId,kAnyChIdx,chord_dry_fl);

          apply_dry_fl =  chord_dry_fl && (((note_idx % 2)==0) == (p->locA[ loc ].rand > RAND_MAX/2));
                      
        }
        else 
        {
          if((rc = var_get(proc,kPerNoteFlPId,kAnyChIdx,per_note_fl)) != kOkRC )
            goto errLabel;

          // if we are selecting presets per note
          if( per_note_fl )
            update_preset_fl = true;
        }

        // if a new preset should be selected
        if( update_preset_fl )
        {
          if((rc = _update_cur_preset_idx( proc, p, p->cur_frag )) != kOkRC )
            goto errLabel;

          // if this is the first note for this 'loc' then cache the selected presets
          if( loc_idx != kInvalidIdx )
          {
            p->locA[ loc_idx ].pri_preset_idx = p->cur_pri_preset_idx;
            p->locA[ loc_idx ].sec_preset_idx = p->cur_sec_preset_idx;
          }
        } 

        if( apply_dry_fl )
        {
          pri_preset_idx = p->dry_preset_idx;
          sec_preset_idx = p->dry_preset_idx;
        }
        
        rc = _apply_preset( proc, p, m, voice_idx, pri_preset_idx, sec_preset_idx );

      errLabel:
        return rc;
      }
      
      rc_t _exec_midi_in( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        mbuf_t* mbuf = nullptr;
        
        for(unsigned voice_idx=0; voice_idx<p->polyN; ++voice_idx)
        {
          // get the input MIDI buffer
          if((rc = var_get(proc,p->base[ kMidiInPId ]+voice_idx,kAnyChIdx,mbuf)) != kOkRC )
            goto errLabel;

          for(unsigned j=0; j<mbuf->msgN; ++j)
          {
            const midi::ch_msg_t* m = mbuf->msgA + j;
            
            // if this is a note-on msg
            if( m->status == midi::kNoteOnMdId && m->d1 > 0 )
            {
              _exec_note_on(proc,p,m,voice_idx);
            }         
          }
        }
      errLabel:
        return rc;
      }

      rc_t _exec_on_new_fragment( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        bool per_note_fl = false;
        bool per_loc_fl = false;;
        var_get(proc,kPerNoteFlPId,kAnyChIdx,per_note_fl);
        var_get(proc,kPerLocFlPId,kAnyChIdx,per_loc_fl);
        
        // if we are not assigning presets per note - then select p->cur_pri/sec_preset_idx for all following notes
        if( per_note_fl == false && per_loc_fl == false )
          if((rc = _update_cur_preset_idx( proc, p, p->cur_frag )) != kOkRC )            
            goto errLabel;
        
      errLabel:
        return rc;
      }

      rc_t _exec_track_loc( proc_t* proc, inst_t* p )
      {

        rc_t     rc      = kOkRC;
        rbuf_t*  in_rbuf = nullptr;
        unsigned loc     = kInvalidIdx;

        // if score tracking is disabled
        if( p->loc_fld_idx == kInvalidIdx )
          goto errLabel;

        if((rc = var_get(proc,kInPId,kAnyChIdx,in_rbuf)) != kOkRC)
          goto errLabel;
        
        // for each incoming MIDI record
        for(unsigned i=0; i<in_rbuf->recdN; ++i)
        {
          unsigned tmp;
          // get the 'loc' field
          if((rc = recd_get( in_rbuf->type, in_rbuf->recdA+i, p->loc_fld_idx, tmp)) != kOkRC )
          {
            rc = proc_error(proc,rc,"The 'loc' field read failed.");
            goto errLabel;
          }
          if( tmp != kInvalidIdx )
            loc = tmp;
        }

        // if a location value was received
        if( loc != kInvalidIdx )
        {        
          const preset_sel::frag_t* frag       = nullptr;
          
          // if this location is associated with a new set of preset selections ...
          if( preset_sel::track_loc( p->psH, loc, frag ) && frag != nullptr )
          {
            // p->cur_frag maintains a reference to the preset selections
            p->cur_frag = frag;

            //proc_info(proc,"ps LOC:%i ",loc);
            //cwLogPrint("LOC:%i ",loc);
            //fragment_report( p->psH, frag );
            
            rc = _exec_on_new_fragment(proc,p);
          }
        
        }
      errLabel:
        return rc;
      }

      rc_t _update_manual_preset_index( inst_t* p, variable_t* var, unsigned& preset_idx_ref )
      {
        rc_t rc = kOkRC;
        unsigned list_idx;
        
        if((rc = var_get(var,list_idx)) != kOkRC )
          goto errLabel;


        if((rc = list_ele_value(p->manual_sel_list,list_idx,preset_idx_ref)) != kOkRC )
          goto errLabel;
        

      errLabel:
        if( rc != kOkRC )
          preset_idx_ref = kInvalidIdx;
        
        
        return rc;
      }

      
      rc_t _update_ui_state( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;

        // if the ui has not yet been initialized then there is nothing to do
        if( var->ui_var == NULL )
          return rc;
        
        switch(var->vid)
        {
          case kPriManualSelPId:
            if((rc = _update_manual_preset_index(p,var,p->cur_manual_pri_preset_idx)) != kOkRC )
              rc = proc_error(proc,rc,"Manual primary selected preset index update failed.");
            break;

          case kSecManualSelPId:         
            if((rc = _update_manual_preset_index(p,var,p->cur_manual_sec_preset_idx)) != kOkRC )
              rc = proc_error(proc,rc,"Manual secondary selected preset index update failed.");
            break;
            
          case kPriProbFlPId:
            var_get(var,p->pri_prob_fl);
            var_send_to_ui_enable(proc, kPriUniformFlPId,   kAnyChIdx, p->pri_prob_fl );
            var_send_to_ui_enable(proc, kPriDryOnPlayFlPId, kAnyChIdx, p->pri_prob_fl );
            var_send_to_ui_enable(proc, kPriAllowAllFlPId,  kAnyChIdx, p->pri_prob_fl && p->pri_uniform_fl );
            var_send_to_ui_enable(proc, kPriDryOnSelFlPId,  kAnyChIdx, p->pri_prob_fl && p->pri_uniform_fl && p->pri_allow_all_fl );
            break;
            
          case kPriUniformFlPId:
            var_get(var,p->pri_uniform_fl);
            var_send_to_ui_enable(proc, kPriAllowAllFlPId,  kAnyChIdx, p->pri_uniform_fl );
            var_send_to_ui_enable(proc, kPriDryOnSelFlPId,  kAnyChIdx, p->pri_uniform_fl && p->pri_allow_all_fl );
            break;
            
          case kPriDryOnPlayFlPId:
            var_get(var,p->pri_dry_on_play_fl);
            break;
            
          case kPriAllowAllFlPId:
            var_get(var,p->pri_allow_all_fl);
            var_send_to_ui_enable(proc, kPriDryOnSelFlPId,  kAnyChIdx, p->pri_allow_all_fl );
            break;
            
          case kPriDryOnSelFlPId:
            var_get(var,p->pri_dry_on_sel_fl);
            break;

          case kInterpFlPId:
            var_get(var,p->interp_fl);
            var_send_to_ui_enable(proc, kSecProbFlPId,    kAnyChIdx, p->interp_fl );            
            var_send_to_ui_enable(proc, kInterpRandFlPId, kAnyChIdx, p->interp_fl );            
            var_send_to_ui_enable(proc, kInterpDistPId,   kAnyChIdx, p->interp_fl & (!p->interp_rand_fl) );                        
            break;
            
          case kInterpDistPId:
            var_get(var,p->cur_interp_dist);
            break;
            
          case kInterpRandFlPId:
            var_get(var,p->interp_rand_fl);
            var_send_to_ui_enable(proc, kInterpDistPId, kAnyChIdx, p->interp_fl & (!p->interp_rand_fl) );            
            break;
            
          case kSecProbFlPId:
            var_get(var,p->sec_prob_fl);
            var_send_to_ui_enable(proc, kSecUniformFlPId,   kAnyChIdx, p->sec_prob_fl );
            var_send_to_ui_enable(proc, kSecDryOnPlayFlPId, kAnyChIdx, p->sec_prob_fl );
            var_send_to_ui_enable(proc, kSecAllowAllFlPId,  kAnyChIdx, p->sec_prob_fl && p->sec_uniform_fl);
            var_send_to_ui_enable(proc, kSecDryOnSelFlPId,  kAnyChIdx, p->sec_prob_fl && p->sec_uniform_fl && p->sec_allow_all_fl );
            break;
            
          case kSecUniformFlPId:
            var_get(var,p->sec_uniform_fl);
            var_send_to_ui_enable(proc, kSecAllowAllFlPId,  kAnyChIdx, p->sec_uniform_fl );
            var_send_to_ui_enable(proc, kSecDryOnSelFlPId,  kAnyChIdx, p->sec_uniform_fl && p->sec_allow_all_fl );
            break;
            
          case kSecDryOnPlayFlPId:
            var_get(var,p->sec_dry_on_play_fl);            
            break;
            
          case kSecAllowAllFlPId:
            var_get(var,p->sec_allow_all_fl);
            var_send_to_ui_enable(proc, kSecDryOnSelFlPId,  kAnyChIdx, p->sec_allow_all_fl );
            break;
            
          case kSecDryOnSelFlPId:
            var_get(var,p->sec_dry_on_sel_fl);
            break;
        }

        return rc;
      }
      
      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;

        _update_ui_state( proc, p, var );

        if( var->vid == kResetPId   )
        {
          if( p->psH.isValid() )
            track_loc_reset( p->psH);
          
          _init_loc_array(p);

          proc_info(proc,"GUTIM ps reset.");
        }
        
        //errLabel:
        return rc;
      }

      
      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc;

        if((rc = _exec_track_loc(proc,p)) != kOkRC )
          goto errLabel;

        if((rc = _exec_midi_in(proc,p)) != kOkRC )
          goto errLabel;

        
      errLabel:
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .notify  = std_notify<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // Score Follower
    //
    namespace score_follower
    {

      enum
      {
        kInPId,
        kDynTblFnamePId,
        kFnamePId,
        kScoreWndCntPId,
        kMidiWndCntPId,
        kPrintFlPId,
        kBacktrackFlPId,        
        kLocPId,
        kOutPId,
      };
      
      typedef struct
      {
        cw::dyn_ref_tbl::handle_t    dynRefH;
        cw::score_parse::handle_t    scParseH;
        cw::sfscore::handle_t scoreH;
        cw::score_follower::handle_t sfH;
        unsigned midi_field_idx;
        unsigned loc_field_idx;
        
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t                   rc                   = kOkRC;        
        rbuf_t*                in_rbuf              = nullptr;
        const char*            score_fname          = nullptr;
        const char*            dyn_tbl_fname        = nullptr;
        bool                   printParseWarningsFl = true;
        cw::score_follower::args_t args;

        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kInPId,         "in",            kBaseSfxId, in_rbuf,
                                      kFnamePId,      "fname",         kBaseSfxId, score_fname,
                                      kDynTblFnamePId,"dyn_ref_fname", kBaseSfxId, dyn_tbl_fname,
                                      kScoreWndCntPId,"score_wnd",     kBaseSfxId, args.scoreWndLocN,
                                      kMidiWndCntPId, "midi_wnd",      kBaseSfxId, args.midiWndLocN,
                                      kPrintFlPId,    "print_fl",      kBaseSfxId, args.trackPrintFl,
                                      kBacktrackFlPId,"back_track_fl", kBaseSfxId, args.trackResultsBacktrackFl )) != kOkRC )
        {
          goto errLabel;
        }

        // get the input record 'midi' field index
        if((p->midi_field_idx = recd_type_field_index( in_rbuf->type, "midi")) == kInvalidIdx )
        {
          rc = proc_error(proc,rc,"The input record type on '%s:%i' does not have a 'midi' field.",cwStringNullGuard(proc->label),proc->label_sfx_id);
          goto errLabel;          
        }

        // get the input record 'loc' field index
        if((p->loc_field_idx = recd_type_field_index( in_rbuf->type, "loc")) == kInvalidIdx )
        {
          rc = proc_error(proc,rc,"The input record type on '%s:%i' does not have a 'loc' field.",cwStringNullGuard(proc->label),proc->label_sfx_id);
          goto errLabel;          
        }

        // parse the dynamics reference array
        if((rc = dyn_ref_tbl::create(p->dynRefH,dyn_tbl_fname)) != kOkRC )
        {
          rc = proc_error(proc,rc,"The reference dynamics array parse failed.");
          goto errLabel;
        }
        
        // parse the score
        if((rc = create( p->scParseH, score_fname, proc->ctx->sample_rate, p->dynRefH, printParseWarningsFl )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Score parse failed.");
          goto errLabel;
        }
        
        // create the SF score
        if((rc = create( p->scoreH, p->scParseH, printParseWarningsFl)) != kOkRC )
        {
          rc = proc_error(proc,rc,"SF Score create failed.");
          goto errLabel;
        }

        args.enableFl = true;
        args.scoreH   = p->scoreH;

        // create the score follower
        if((rc = create( p->sfH, args )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Score follower create failed.");
          goto errLabel;          
        }

      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        destroy(p->sfH);
        destroy(p->scoreH);
        destroy(p->scParseH);
        destroy(p->dynRefH);

        return rc;
      }

      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;

        unsigned sample_idx = proc->ctx->cycleIndex * proc->ctx->framesPerCycle;
        double   sec        = ((double)sample_idx) / proc->ctx->sample_rate;
        rbuf_t*  rbuf       = nullptr;
        unsigned result_recd_idx = kInvalidIdx;

        if((rc = var_get(proc,kInPId,kAnyChIdx,rbuf)) != kOkRC)
          goto errLabel;

        // for each incoming record
        for(unsigned i=0; i<rbuf->recdN; ++i)
        {
          bool match_fl= false;
          midi::ch_msg_t* m = nullptr;

          if((rc = recd_get( rbuf->type, rbuf->recdA+i, p->midi_field_idx, m)) != kOkRC )
          {
            rc = proc_error(proc,rc,"The 'midi' field read failed.");
            goto errLabel;
          }
                    
          if((rc = exec( p->sfH, sec, sample_idx, m->uid, m->status, m->d0,m->d1, match_fl )) != kOkRC )
          {
            rc = proc_error(proc,rc,"Score follower exec failed.");
            goto errLabel;
          }

          if( match_fl )
            result_recd_idx = i;

        }

        if( result_recd_idx != kInvalidIdx )
        {
          unsigned        resultIdxN       = 0;
          const unsigned* resultIdxA       = current_result_index_array(p->sfH, resultIdxN );
          const sftrack::result_t* resultA = cw::score_follower::track_result(p->sfH);
          
          for(unsigned i=0; i<resultIdxN; ++i)
          {
            const sftrack::result_t* r = resultA + resultIdxA[i];                
            const sfscore::event_t* e = event(p->scoreH, r->scEvtIdx );

            // store the performance data in the score
            set_perf( p->scoreH, r->scEvtIdx, r->sec, r->pitch, r->vel, r->cost );
            
            if( i+1 == resultIdxN )
            {
              //recd_set( rbuf->type, rbuf->recdA + result_recd_idx, p->loc_field_idx, e->oLocId );
              var_set( proc, kLocPId, kAnyChIdx, e->oLocId );
            }
            
          }

          var_set( proc, kOutPId, kAnyChIdx, rbuf );
        }
        
        
      errLabel:
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .notify  = std_notify<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    } // score_follower    


    //------------------------------------------------------------------------------------------------------------------
    //
    // Score Follower 2
    //
    namespace score_follower_2
    {

      enum
      {
        kInPId,
        kFnamePId,
        kBegLocPId,
        kEndLocPId,
        kMaxLocPId,
        kResetTrigPId,
        kEnableFlPId,
        kDVelPId,
        kPrintFlPId,
        kOutPId,
      };
      
      typedef struct
      {
        cw::perf_score::handle_t       scoreH;
        cw::score_follow_2::handle_t   sfH;
        unsigned                       i_midi_field_idx;
        //unsigned                       o_midi_field_idx;
        unsigned                       loc_field_idx;
        unsigned                       meas_field_idx;
        unsigned                       vel_field_idx;
        recd_array_t*                  recd_array;  // output record array
        unsigned                       cur_loc_id;
        bool                           enable_fl;
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t                   rc                   = kOkRC;        
        rbuf_t*                in_rbuf              = nullptr;
        const char*            c_score_fname        = nullptr;
        char*                  score_fname          = nullptr;
        unsigned               beg_loc_id = kInvalidId;
        unsigned               end_loc_id = kInvalidId;
        bool                   reset_trig_fl = false;
        float dvel = 1.0;
        cw::score_follow_2::args_t sf_args = {
          .pre_affinity_sec = 1.0,
          .post_affinity_sec = 3.0,
          .min_affinity_loc_cnt = 2,
          .pre_wnd_sec = 2.0,
          .post_wnd_sec = 5.0,
          .min_wnd_loc_cnt = 2,
          .decay_coeff = 0.995,
          .d_sec_err_thresh_lo = 0.4,
          .d_loc_thresh_lo = 3,
          .d_sec_err_thresh_hi = 1.5,
          .d_loc_thresh_hi = 7,
          .d_loc_stats_thresh = 3,
          .rpt_fl = true
        };

        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kInPId,         "in",            kBaseSfxId, in_rbuf,
                                      kFnamePId,      "score_fname",   kBaseSfxId, c_score_fname,
                                      kBegLocPId,     "b_loc",         kBaseSfxId, beg_loc_id,
                                      kEndLocPId,     "e_loc",         kBaseSfxId, end_loc_id,
                                      kResetTrigPId,  "reset_trigger", kBaseSfxId, reset_trig_fl,
                                      kEnableFlPId,   "enable_fl",     kBaseSfxId, p->enable_fl,
                                      kDVelPId,       "dvel",          kBaseSfxId, dvel,
                                      kPrintFlPId,    "print_fl",      kBaseSfxId, sf_args.rpt_fl )) != kOkRC )
        {
          goto errLabel;
        }
        if((score_fname = proc_expand_filename( proc, c_score_fname )) == nullptr )
        {
          rc = proc_error(proc,kOpFailRC,"Unable to expand the score filename '%s'.",cwStringNullGuard(c_score_fname));
          goto errLabel;
        }

        // create the SF score
        if((rc = create( p->scoreH, score_fname)) != kOkRC )
        {
          rc = proc_error(proc,rc,"SF Score create failed.");
          goto errLabel;
        }

        // create the score follower
        if((rc = create( p->sfH, sf_args, p->scoreH )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Score follower create failed.");
          goto errLabel;          
        }

        if((rc = reset( p->sfH, beg_loc_id, end_loc_id )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Score follower reset failed.");
          goto errLabel;
        }

        if((rc = var_register_and_set(proc,kAnyChIdx,
                                      kMaxLocPId,"loc_cnt", kBaseSfxId, max_loc_id(p->sfH) )) != kOkRC )
        {
          goto errLabel;
        }
        
        if((rc = var_alloc_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, in_rbuf->type, p->recd_array )) != kOkRC )
        {
          goto errLabel;
        }
    
        
        p->i_midi_field_idx = recd_type_field_index( in_rbuf->type, "midi");
        //p->o_midi_field_idx = recd_type_field_index( p->recd_array->type, "midi");
        p->loc_field_idx    = recd_type_field_index( p->recd_array->type, "loc");
        p->meas_field_idx   = recd_type_field_index(p->recd_array->type, "meas");
        p->vel_field_idx    = recd_type_field_index( p->recd_array->type, "score_vel");

      errLabel:
        mem::release(score_fname);
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        recd_array_destroy(p->recd_array);
        destroy(p->sfH);
        destroy(p->scoreH);

        return rc;
      }

      rc_t _reset_sf( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        unsigned beg_loc_id;
        unsigned end_loc_id;

        if((rc = var_get(proc,kBegLocPId,kAnyChIdx,beg_loc_id)) != kOkRC )
        {
          rc = proc_error(proc,rc,"beg_loc read failed.");
          goto errLabel;
        }
        
        if((rc = var_get(proc,kEndLocPId,kAnyChIdx,end_loc_id)) != kOkRC )
        {
          rc = proc_error(proc,rc,"end_loc read failed.");
          goto errLabel;
        }

        if((rc = reset(p->sfH,beg_loc_id,end_loc_id)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Score follower reset failed..");
          goto errLabel;          
        }

        p->cur_loc_id = kInvalidId;

        //proc_info(proc,"SF (%s) reset:%i %i",proc->label, beg_loc_id,end_loc_id);

      errLabel:

        if( rc != kOkRC )
          rc = proc_error(proc,rc,"SF reset failed on %i %i",beg_loc_id,end_loc_id);
        return rc;        
      }

      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        switch( var->vid )
        {
          case kResetTrigPId:
            rc = _reset_sf(proc,p);
            break;

          case kEnableFlPId:
            var_get(var,p->enable_fl);
            proc_info(proc,"SF (%s) ENABLE = %i",cwStringNullGuard(proc->label),p->enable_fl);
            break;
        }
        return rc;
      }


      rc_t _set_output_record( proc_t* proc, inst_t* p, rbuf_t* rbuf, const recd_t* base, unsigned loc_id, unsigned meas_numb, unsigned vel )
      {
        rc_t rc = kOkRC;
        
        recd_t* r = p->recd_array->recdA + rbuf->recdN;
        
        // if the output record array is full
        if( rbuf->recdN >= p->recd_array->allocRecdN )
        {
          rc = proc_error(proc,kBufTooSmallRC,"The internal record buffer overflowed. (buf recd count:%i).",p->recd_array->allocRecdN);
          goto errLabel;
        }
        
        recd_set( rbuf->type, base, r, p->loc_field_idx,  loc_id  );
        recd_set( rbuf->type, base, r, p->meas_field_idx, meas_numb );
        recd_set( rbuf->type, base, r, p->vel_field_idx,  vel );
        rbuf->recdN += 1;

      errLabel:
        return rc;
      }

      float _dvel( unsigned actual_vel, unsigned score_vel )
      {
        float v1 = actual_vel;
        float v2 = score_vel;

        v1 = (v1/127.0f);
        v2 = (v2/25.0f);

        //proc_info(proc,"%i %i",actual_vel,score_vel);

        return 1.0f + std::max(1.0f, std::min(0.0f, v1<v2 ? v2-v1 : v1-v2));
          
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;

        unsigned      sample_idx      = proc->ctx->cycleIndex * proc->ctx->framesPerCycle;
        double        sec             = ((double)sample_idx) / proc->ctx->sample_rate;
        const rbuf_t* i_rbuf          = nullptr;
        rbuf_t*       o_rbuf          = nullptr;

        if((rc = var_get(proc,kInPId,kAnyChIdx,i_rbuf)) != kOkRC )
          goto errLabel;

        if((rc = var_get(proc,kOutPId,kAnyChIdx,o_rbuf)) != kOkRC )
          goto errLabel;

        o_rbuf->recdA = p->recd_array->recdA;
        o_rbuf->recdN = 0;

        if( !p->enable_fl )
          goto errLabel;

        
        // if the score follower is disabled - there is nothing to do
        if( p->enable_fl )
        {
          // for each incoming record
          for(unsigned i=0; i<i_rbuf->recdN; ++i)
          {
            midi::ch_msg_t* m         = nullptr;
            unsigned        loc_id    = kInvalidId;
            unsigned        score_vel = -1;
            unsigned        meas_numb = -1;

            if((rc = recd_get( i_rbuf->type, i_rbuf->recdA+i, p->i_midi_field_idx, m)) != kOkRC )
            {
              rc = proc_error(proc,rc,"The 'midi' field read failed.");
              goto errLabel;
            }

            if( midi::isNoteOn( m->status, m->d1 ) )
            {

              //printf("%s : %i %i %i %i\n",proc->label,m->uid,m->status,m->d0,m->d1);
            
              if((rc = on_new_note( p->sfH, m->uid, sec, m->d0, m->d1, loc_id, meas_numb, score_vel )) != kOkRC )
              {
                rc = proc_error(proc,rc,"Score follower note processing failed.");
                goto errLabel;              
              }

              if( loc_id != kInvalidId )
              {
                if( loc_id != p->cur_loc_id )
                {
                  p->cur_loc_id = loc_id;

                  //float dvel = _dvel(m->d1,score_vel);
                  //var_set(proc,kDVelPId,kAnyChIdx,dvel);
                  //proc_info(proc,"DVEL::%f",dvel);
                            
                  //proc_info(proc,"sf (%s) LOC:%i",proc->label,loc_id);
                  //printf("sf (%s) LOC:%i\n",proc->label,loc_id);
                }
              }
            }

            _set_output_record( proc, p, o_rbuf, i_rbuf->recdA+i, loc_id, meas_numb, score_vel );
            //_set_output_record( proc, p, o_rbuf, nullptr, loc_id, meas_numb, score_vel );
          
          }
        }
        do_exec(p->sfH);
        
      errLabel:
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .notify  = std_notify<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    } // score_follower_2


    //------------------------------------------------------------------------------------------------------------------
    //
    // gutim_ctl
    //
    namespace gutim_ctl
    {
      enum {
        kInPId,
        kResetLocPId,
        kSpirioRcvrPId,
        kSfRcvrPId,
        kCfgFnamePId,
        kSpirioPlyrPId,
        kSpirioBegLocPId,
        kSpirioEndLocPId,
        kSpirioStartFlPId,
        kMidiEnaAPId,
        kMidiEnaBPId,
        kSfABegLocPId,
        kSfAEndLocPId,
        kSfBBegLocPId,
        kSfBEndLocPId
      };

      typedef struct
      {
        unsigned id;                // unique id
        char*    seg_label;         // text label
        unsigned beg_loc;           // starting location of this segment 
        unsigned end_loc;           // ending location of this segment
        unsigned player_id;         // id of player who plays this segment
        char*    player_label;      // label of player who plays this segment
        unsigned piano_id;          // piano id of piano used to play this segment
        char*    piano_label;       // label associated with piano_id 
        bool     defer_midi_ena_fl; // true if the MIDI enable settings should be deferred until the end of the segment
        unsigned midi_ena_a;        // 1 if MIDI should be enabled on port A during this segment
        unsigned midi_ena_b;        // 1 if MIDI should be enabled on port B during this segment
      } seg_t;

      
      typedef struct
      {
        seg_t*   segA;
        unsigned segN;
        unsigned loc_fld_idx;
        unsigned spirio_player_id;

        unsigned cur_seg_idx;
        unsigned cur_loc_id;
      } inst_t;

      rc_t _load_from_cfg( proc_t* proc, inst_t* p, const object_t* cfg, const char* spirio_player_label )
      {
        rc_t rc = kOkRC;
        const object_t* cfg_list = nullptr;

        // get the 'list' element from the root
        if((rc = cfg->getv("list", cfg_list)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Unable to locate the 'list' field gutim_ctl cfg.");
          goto errLabel;
        }

        if( !cfg_list->is_list() )
        {
          rc = proc_error(proc,rc,"Expected gutim_ctl list to be of type 'list'.");
          goto errLabel;
        }

        p->segN  = cfg_list->child_count();
        p->segA = mem::allocZ<seg_t>(p->segN);

        // for each gutim_ctl cfg record in the cfg file
        for(unsigned i=0; i<p->segN; ++i)
        {
          const object_t* list_ele;
          if((list_ele = cfg_list->child_ele(i)) == nullptr || !list_ele->is_dict() )
          {
            rc = proc_error(proc,kSyntaxErrorRC,"The gutim_ctl cfg record index '%i' is not a valid dictionary.",i);
            goto errLabel;
          }

          seg_t* r = p->segA + i;
          
          if((rc = list_ele->getv("id",r->id,
                                  "seg_label",r->seg_label,
                                  "beg_loc",r->beg_loc,
                                  "end_loc",r->end_loc,
                                  "player_id",r->player_id,
                                  "player_label",r->player_label,
                                  "piano_id",r->piano_id,
                                  "piano_label",r->piano_label,
                                  "defer_midi_ena_fl",r->defer_midi_ena_fl,
                                  "enable_midi_a",r->midi_ena_a,
                                  "enable_midi_b",r->midi_ena_b)) != kOkRC )
          {
            rc = proc_error(proc,rc,"Parsing failed on gutim_ctl cfg index '%i'.");
            goto errLabel;
          }

          r->player_label = mem::duplStr(r->player_label);
          r->piano_label  = mem::duplStr(r->piano_label);
          r->seg_label    = mem::duplStr(r->seg_label);

          // set p->spirio_player_id from the spirio_player_label argument
          if( textIsEqual(r->player_label,spirio_player_label) )
          {
            if(p->spirio_player_id == kInvalidId )
              p->spirio_player_id = r->player_id;
            else
            {
              if( p->spirio_player_id != r->player_id )
              {
                rc = proc_error(proc,kInvalidStateRC,"The Spirio player_id is inconsistent in the gutim_ctl cfg. file at beg loc:%i end loc:%i.",r->beg_loc,r->end_loc);
                goto errLabel;
              }
            }
          }
          
        }

      errLabel:

        if( rc == kOkRC && p->spirio_player_id == kInvalidId )
          rc = proc_error(proc,kInvalidStateRC,"The Spirio player id was not found in the gutim_ctl cfg. file.");
        
        return rc;
        
      }

      rc_t _parse_cfg_file( proc_t* proc, inst_t* p, const char* fname, const char* spirio_player_label )
      {
        rc_t rc = kOkRC;
        char* fn = nullptr;;
        object_t* cfg = nullptr;
        
        if((fn = proc_expand_filename(proc,fname)) == nullptr )
        {
          rc = proc_error(proc,kOpFailRC,"The gutim_ctl file name '%s' could not be expanded.",cwStringNullGuard(fname));
          goto errLabel;
        }
          
        if((rc = objectFromFile( fn, cfg )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Unable to parse gutim_ctl cfg from '%s'.",cwStringNullGuard(fn));
          goto errLabel;
        }

        if((rc = _load_from_cfg( proc, p, cfg, spirio_player_label)) != kOkRC )
        {
          goto errLabel;
        }

      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"gutim_ctl reference file parsing failed on '%s'.",cwStringNullGuard(fname));

        mem::release(fn);

        if( cfg != nullptr )
          cfg->free();
        
        return rc;
      }

      unsigned _seg_id_to_seg_idx( inst_t* p, unsigned seg_id )
      {
        for(unsigned i=0; i<p->segN; ++i)
          if( p->segA[i].id == seg_id )
            return i;
        return kInvalidIdx;
      }
      
      unsigned _loc_to_seg_idx( inst_t* p, unsigned loc )
      {
        for(unsigned i=0; i<p->segN; ++i)
          if( p->segA[i].beg_loc <= loc and loc <= p->segA[i].end_loc )
            return i;
        return kInvalidIdx;
      }

      
      bool _is_loc_in_seg( inst_t* p, unsigned seg_idx, unsigned loc_id )
      {
        
        if( seg_idx == kInvalidIdx || seg_idx > p->segN )
          return false;

        return p->segA[ seg_idx ].beg_loc <= loc_id && loc_id <= p->segA[ seg_idx ].end_loc;
      }


      rc_t _on_loc( proc_t* proc, inst_t* p, unsigned loc_id, bool reset_fl )
      {
        rc_t rc = kOkRC;

        // if the location is changing
        if( loc_id != kInvalidId && loc_id != p->cur_loc_id )
        {

          // if the new location is not in the current segment
          if( !_is_loc_in_seg(p,p->cur_seg_idx,loc_id) )
          {
            unsigned seg_idx;
            
            if((seg_idx = _loc_to_seg_idx(p,loc_id)) == kInvalidIdx )
            {
              proc_warn(proc,"An invalid location (%i) was received by the gutim_ctl.",loc_id);
              goto errLabel;
            }

            // the new segment should not be the same as the previous segment
            if( seg_idx == p->cur_seg_idx )
            {
              proc_warn(proc,"The gutim_ctl unexpectedly did not change segments.");
            }

            // if the segment recd is changing
            if( seg_idx != p->cur_seg_idx )
            {
              const seg_t* r = p->segA + seg_idx;
              
              // and the loc matches the first loc in the segment and the segment is a spirio segment ....
              if( r->beg_loc == loc_id && r->player_id == p->spirio_player_id  )
              {
                // ... then update the spirio player with the segment that it should play
                var_set(proc,kSpirioBegLocPId,kAnyChIdx,r->beg_loc);
                var_set(proc,kSpirioEndLocPId,kAnyChIdx,r->end_loc);

                if( !reset_fl )
                  var_set(proc,kSpirioStartFlPId,kAnyChIdx,true);
              }

              // update the score followers
              unsigned this_sf_beg_locPId;
              unsigned this_sf_end_locPId;
              unsigned next_sf_beg_locPId;
              unsigned next_sf_end_locPId;

              switch( r->piano_id )
              {
                case 0: // this=A next=B
                  this_sf_beg_locPId = kSfABegLocPId;
                  this_sf_end_locPId = kSfAEndLocPId;
                  next_sf_beg_locPId = kSfBBegLocPId;
                  next_sf_end_locPId = kSfBEndLocPId;
                  break;
                  
                case 1: // this=B next=A
                  this_sf_beg_locPId = kSfBBegLocPId;
                  this_sf_end_locPId = kSfBEndLocPId;
                  next_sf_beg_locPId = kSfABegLocPId;
                  next_sf_end_locPId = kSfAEndLocPId;
                  break;

                default:
                  rc = proc_error(proc,kInvalidArgRC,"An unknown piano_id was encoutered in the gutim_ctl.");
                  goto errLabel;
              }

              // set the current SF to the incoming loc_id
              var_set(proc,this_sf_beg_locPId,kAnyChIdx,loc_id);
              var_set(proc,this_sf_end_locPId,kAnyChIdx,r->end_loc);
              printf("GUTIM_CTL: cur segment %s : player:%s piano:%s : loc:%i %i \n",r->seg_label,r->player_label,r->piano_label,loc_id,r->end_loc);

              // if not deferring MIDI enable then apply the MIDI enable values for this segment
              if( !r->defer_midi_ena_fl )
              {
                var_set(proc,kMidiEnaAPId,kAnyChIdx,r->midi_ena_a);
                var_set(proc,kMidiEnaBPId,kAnyChIdx,r->midi_ena_b);
                printf("GUTIM_CTL: midi_ena: %i %i\n",r->midi_ena_a,r->midi_ena_b);
              }

              // setup the next segments score follower
              if( seg_idx + 1 < p->segN )
              {
                const seg_t* s = p->segA + seg_idx + 1;
                // set the next SF to beg/end of the next segment
                var_set(proc,next_sf_beg_locPId,kAnyChIdx,s->beg_loc);
                var_set(proc,next_sf_end_locPId,kAnyChIdx,s->end_loc);
                printf("GUTIM_CTL: next segment %s : player:%s piano:%s : loc:%i %i \n",s->seg_label,s->player_label,s->piano_label,s->beg_loc,s->end_loc);
              }              

              
              p->cur_seg_idx = seg_idx;
            } 
          } // if segment is changing
          
          p->cur_loc_id = loc_id;

          // if MIDI enable was deferred to the end of the segment
          if( p->cur_seg_idx != kInvalidIdx && p->cur_seg_idx < p->segN && p->segA[ p->cur_seg_idx ].defer_midi_ena_fl && loc_id == p->segA[ p->cur_seg_idx ].end_loc)
          {
            var_set(proc,kMidiEnaAPId,kAnyChIdx,p->segA[ p->cur_seg_idx ].midi_ena_a);
            var_set(proc,kMidiEnaBPId,kAnyChIdx,p->segA[ p->cur_seg_idx ].midi_ena_b);
            printf("GUTIM_CTL: defered MIDI enable: a:%i b:%i\n",p->segA[ p->cur_seg_idx ].midi_ena_a,p->segA[ p->cur_seg_idx ].midi_ena_b);
          }
          
        } // if loc is changing
        

      errLabel:
        return rc;
      }
      
      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t          rc                = kOkRC;
        const rbuf_t* i_rbuf            = nullptr;
        unsigned      reset_loc         = kInvalidId;
        const char*   cfg_fname         = nullptr;
        const char*   spirio_plyr_label = nullptr;
        unsigned      midi_ena_a        = 0;
        unsigned      midi_ena_b        = 0;
        
        p->cur_seg_idx      = kInvalidIdx;
        p->cur_loc_id       = kInvalidId;
        p->spirio_player_id = kInvalidId;
        
        if((rc = var_register_and_get(proc, kAnyChIdx,
                                      kInPId,        "in",                  kBaseSfxId, i_rbuf,
                                      kCfgFnamePId,  "cfg_fname",           kBaseSfxId, cfg_fname,
                                      kSpirioPlyrPId,"spirio_player_label", kBaseSfxId, spirio_plyr_label,
                                      kMidiEnaAPId,  "midi_ena_a",          kBaseSfxId, midi_ena_a,
                                      kMidiEnaBPId,  "midi_ena_b",          kBaseSfxId, midi_ena_b,
                                      kResetLocPId,  "reset_loc",           kBaseSfxId, reset_loc
              )) != kOkRC )

        {
          goto errLabel;
        }
        
        if((rc = var_register(proc, kAnyChIdx,
                              kSpirioRcvrPId,   "spirio_recover_id", kBaseSfxId,
                              kSfRcvrPId,       "sf_recover_id",  kBaseSfxId,
                              kSpirioBegLocPId, "spirio_beg_loc", kBaseSfxId,
                              kSpirioEndLocPId, "spirio_end_loc", kBaseSfxId,
                              kSpirioStartFlPId,"spirio_start_fl",kBaseSfxId,
                              kSfABegLocPId,    "sf_a_beg_loc",   kBaseSfxId,
                              kSfAEndLocPId,    "sf_a_end_loc",   kBaseSfxId,
                              kSfBBegLocPId,    "sf_b_beg_loc",   kBaseSfxId,
                              kSfBEndLocPId,    "sf_b_end_loc",   kBaseSfxId )) != kOkRC )
        {
          goto errLabel;
        }

        if((p->loc_fld_idx  = recd_type_field_index( i_rbuf->type, "loc")) == kInvalidIdx )
        {
          proc_error(proc,kInvalidArgRC,"The  input record does not have a 'loc' field.");
          goto errLabel;
        }

        if( textLength(cfg_fname) == 0 )
        {
          rc = proc_error(proc,kInvalidArgRC,"No configuration file was provided to the gutim_ctl.");
          goto errLabel;
        }

        if((rc = _parse_cfg_file(proc, p, cfg_fname, spirio_plyr_label )) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = _on_loc( proc, p, reset_loc, true )) != kOkRC )
        {
          proc_error(proc,rc,"'%s' initial reset failed.",cwStringNullGuard(proc->label));
          goto errLabel;
        }

      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        for(unsigned i=0; i<p->segN; ++i)
        {
          mem::release(p->segA[i].piano_label);
          mem::release(p->segA[i].player_label);          
          mem::release(p->segA[i].seg_label);          
        }

        mem::release(p->segA);

        return rc;
      }


      rc_t _on_spirio_recover( proc_t* proc, inst_t* p, unsigned seg_id )
      {
        rc_t     rc      = kOkRC;
        unsigned seg_idx = kInvalidIdx;
        
        if(seg_id == kInvalidId || (seg_idx = _seg_id_to_seg_idx(p,seg_id)) == kInvalidIdx)
        {
          rc = proc_error(proc,kInvalidArgRC,"The spirio recovery id '%i' is not valid in '%s'.",seg_id,cwStringNullGuard(proc->label));
          goto errLabel;
        }

        rc = _on_loc( proc, p, p->segA[ seg_idx ].beg_loc, false );

      errLabel:
        return rc;
      }

      rc_t _on_sf_recover( proc_t* proc, inst_t* p, unsigned seg_id )
      {
        rc_t     rc      = kOkRC;
        unsigned seg_idx = kInvalidIdx;
        unsigned cur_midi_pid = kInvalidId;

        if(seg_id == kInvalidId || (seg_idx = _seg_id_to_seg_idx(p,seg_id)) == kInvalidIdx)
        {
          rc = proc_error(proc,kInvalidArgRC,"The spirio recovery id '%i' is not valid in '%s'.",seg_id,cwStringNullGuard(proc->label));
          goto errLabel;
        }


        if( p->cur_seg_idx < p->segN )
        {
          switch( p->segA[ p->cur_seg_idx].piano_id )
          {
            case 0: // this=A next=B
              cur_midi_pid = kMidiEnaAPId;
              break;
              
            case 1: // this=B next=A
              cur_midi_pid = kMidiEnaBPId;
              break;
              
            default:
              rc = proc_error(proc,kInvalidArgRC,"An unknown piano_id was encoutered in the gutim_ctl.");
          }
        }
        
        // setup as though we are jumping to the recovery segment
        rc = _on_loc( proc, p, p->segA[ seg_idx ].beg_loc, true );


        // disable MIDI on the channel currently receiving it
        var_set(proc,cur_midi_pid,kAnyChIdx,0);

      errLabel:
        return rc;
      }

      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;

        switch( var->vid )
        {
          case kResetLocPId:
            {
              unsigned loc_id;
              // invalidate cur_loc_id and cur_seg_idx to force all outputs to be updated
              p->cur_loc_id = kInvalidId;
              p->cur_seg_idx = kInvalidIdx;
              if(var_get(var,loc_id) == kOkRC )
                _on_loc(proc,p,loc_id,true);
            }
            break;

          case kSpirioRcvrPId:
            {
              unsigned seg_id = kInvalidId;
              if(var_get(var,seg_id) == kOkRC )
                _on_spirio_recover(proc,p,seg_id);              
            }
            break;
            
          case kSfRcvrPId:
            {
              unsigned seg_id = kInvalidId;
              if( var_get(var,seg_id) == kOkRC)
                _on_sf_recover(proc,p,seg_id);
            }
            break;
        }
        
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;
        const rbuf_t* rbuf = nullptr;

        if((rc = var_get(proc,kInPId,kAnyChIdx,rbuf)) != kOkRC )
        {
          goto errLabel;
        }

        for(unsigned i=0; i<rbuf->recdN; ++i)
        {
          unsigned loc_id;
          if((rc = recd_get(rbuf->type, rbuf->recdA + i, p->loc_fld_idx, loc_id)) != kOkRC )
          {
            goto errLabel;
          }

          _on_loc(proc,p,loc_id,false);
          
        }
        
      errLabel:
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .notify  = std_notify<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    } // gutim_ctl

    //------------------------------------------------------------------------------------------------------------------
    //
    // gutim_sf_ctl
    //
    namespace gutim_sf_ctl
    {
      enum {
        kCfgFnamePId,
        kGotoLocPId,
        kManualLocPId,
        
        kSfAPId,
        kSfBPId,
        
        kBLocAPId,
        kELocAPId,
        kResetAPId,
        kEnableAPId,
        
        kBLocBPId,
        kELocBPId,
        kResetBPId,
        kEnableBPId,
      };

      enum {
        kSfCnt = 2
      };

      typedef struct loc_str
      {
        unsigned sf_id;
        unsigned beg_loc_id;
        unsigned end_loc_id;
        bool     has_score_fl;
      } seg_t;
      
      typedef struct
      {
        seg_t*   segA;
        unsigned segN;
        unsigned loc_a_fld_idx;
        unsigned loc_b_fld_idx;
      } inst_t;

      

      rc_t _load_from_cfg( proc_t* proc, inst_t* p, const object_t* cfg )
      {
        rc_t rc = kOkRC;
        const object_t* cfg_list = nullptr;
        unsigned prv_beg_loc_id = kInvalidId;
        unsigned prv_end_loc_id = kInvalidId;

        // get the 'list' element from the root
        if((rc = cfg->getv("list", cfg_list)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Unable to locate the 'list' field gutim_ctl cfg.");
          goto errLabel;
        }

        if( !cfg_list->is_list() )
        {
          rc = proc_error(proc,rc,"Expected gutim_ctl list to be of type 'list'.");
          goto errLabel;
        }

        p->segN = cfg_list->child_count();
        p->segA = mem::allocZ<seg_t>(p->segN);

        for(unsigned i=0; i<p->segN; ++i)
        {
          const object_t*  ele = cfg_list->child_ele(i);
          seg_t* r = p->segA + i;
          if((rc = ele->getv("sf_id",r->sf_id,
                             "beg_loc",r->beg_loc_id,
                             "end_loc",r->end_loc_id,
                             "has_score_fl",r->has_score_fl)) != kOkRC )
          {
            goto errLabel;
          }

          if( r->sf_id >= kSfCnt )
          {
            rc = proc_error(proc,kInvalidArgRC,"The 'sf_id' value must be be less than the count of score follower reset ports.");
            goto errLabel;
          }

          if( prv_beg_loc_id != kInvalidId )
          {
            if( prv_beg_loc_id > r->beg_loc_id )
            {
              rc = proc_error(proc,kInvalidArgRC,"The segments are out of order on begin location.");
              goto errLabel;
            }            
          }
          
          if( prv_end_loc_id != kInvalidId )
          {
            // there can be no gap in location id's between consecutive segments
            if( r->beg_loc_id != prv_end_loc_id+1 )
            {
              rc = proc_error(proc,kInvalidArgRC,"There is a location gap between segment indexes %i and %i.",i,i-1);
              goto errLabel;
            }
          }

          
          
          prv_beg_loc_id = r->beg_loc_id;
          prv_end_loc_id = r->end_loc_id;

        }

        

      errLabel:
        return rc;
      }
           
      rc_t _parse_cfg_file( proc_t* proc, inst_t* p, const char* fname )
      {
        rc_t rc = kOkRC;
        char* fn = nullptr;;
        object_t* cfg = nullptr;
        
        if((fn = proc_expand_filename(proc,fname)) == nullptr )
        {
          rc = proc_error(proc,kOpFailRC,"The gutim_ctl file name '%s' could not be expanded.",cwStringNullGuard(fname));
          goto errLabel;
        }
          
        if((rc = objectFromFile( fn, cfg )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Unable to parse gutim_ctl cfg from '%s'.",cwStringNullGuard(fn));
          goto errLabel;
        }

        if((rc = _load_from_cfg( proc, p, cfg)) != kOkRC )
        {
          goto errLabel;
        }

      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"gutim_ctl reference file parsing failed on '%s'.",cwStringNullGuard(fname));

        mem::release(fn);

        if( cfg != nullptr )
          cfg->free();
        
        return rc;
      }
           
      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t          rc         = kOkRC;        
        unsigned      goto_loc   = kInvalidId;
        unsigned      manual_loc = kInvalidId;
        const char*   cfg_fname  = nullptr;
        const rbuf_t* rbuf       = nullptr;
        
        if((rc = var_register_and_get(proc, kAnyChIdx,
                                      kCfgFnamePId, "cfg_fname", kBaseSfxId, cfg_fname,
                                      kGotoLocPId,  "goto_loc",  kBaseSfxId, goto_loc,
                                      kManualLocPId,"manual_loc",kBaseSfxId, manual_loc,
                                      kSfAPId,     "sf_a",     kBaseSfxId, rbuf,
                                      kSfBPId,     "sf_b",     kBaseSfxId, rbuf )) != kOkRC )

        {
          goto errLabel;
        }
        
        if((rc = var_register(proc, kAnyChIdx,
                              kBLocAPId,    "b_loc_a",   kBaseSfxId,
                              kELocAPId,    "e_loc_a",   kBaseSfxId,
                              kResetAPId,   "reset_a",   kBaseSfxId,
                              kEnableAPId,  "enable_a",  kBaseSfxId,
                              kBLocBPId,    "b_loc_b",   kBaseSfxId,
                              kELocBPId,    "e_loc_b",   kBaseSfxId,
                              kResetBPId,   "reset_b",   kBaseSfxId,
                              kEnableBPId,  "enable_b",  kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = _parse_cfg_file(proc,p,cfg_fname)) != kOkRC )
        {
          goto errLabel;
        }

        p->loc_a_fld_idx = kInvalidIdx;
        p->loc_b_fld_idx = kInvalidIdx;

      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        mem::release(p->segA);

        return rc;
      }

      void _reset_sf( proc_t* proc, const seg_t* r, unsigned beg_loc_id )
      {
        switch( r->sf_id )
        {
          case 0:
            var_set(proc,kBLocAPId,kAnyChIdx,beg_loc_id);
            var_set(proc,kELocAPId,kAnyChIdx,r->end_loc_id);
            var_set(proc,kResetAPId,kAnyChIdx,true);
            var_set(proc,kEnableAPId,kAnyChIdx,r->has_score_fl);
            //printf("Set A:%i %i\n",beg_loc_id,r->end_loc_id);
            break;
                
          case 1:
            var_set(proc,kBLocBPId,kAnyChIdx,beg_loc_id);
            var_set(proc,kELocBPId,kAnyChIdx,r->end_loc_id);
            var_set(proc,kResetBPId,kAnyChIdx,true);
            var_set(proc,kEnableBPId,kAnyChIdx,r->has_score_fl);
            //printf("Set B:%i %i\n",beg_loc_id,r->end_loc_id);
            break;
                
          default:
            proc_error(proc,kInvalidArgRC,"The SF id '%i' is not valid in '%s'.",r->sf_id,cwStringNullGuard(proc->label));
            break;
        }
      }

      rc_t _on_loc( proc_t* proc, inst_t* p, unsigned seg_idx, unsigned beg_loc_id )
      {
        rc_t rc = kOkRC;

        if( seg_idx >= p->segN )
        {
          proc_info(proc,"END OF LAST SEGMENT in on_loc.");
          return kOkRC;
        }

        if( beg_loc_id < p->segA[ seg_idx ].beg_loc_id || p->segA[ seg_idx].end_loc_id < beg_loc_id )
          proc_warn(proc,"The 'beg_loc_id' is not in the requested segment in _on_loc() : %s.",cwStringNullGuard(proc->label));
        
        
        // flag array used to track which SF's have been reset
        bool updA[ kSfCnt ];        
        for(unsigned i=0; i<kSfCnt; ++i)
          updA[i] = 0;

        // The SF which contains the specified loc is set to begin on 'beg_loc_id'.
        const seg_t* cur_loc = p->segA + seg_idx;
        unsigned loc_id_cnt = 1;
        updA[ cur_loc->sf_id ] = true;
        _reset_sf(proc,cur_loc,beg_loc_id);

        // All the other SF's must now be set to their next segment following 'beg_loc_id'.
        for(unsigned j=seg_idx+1; j<p->segN && loc_id_cnt<kSfCnt; ++j)
        {
          // if the SF assigned to segA[j] has not been reset 
          if( !updA[ p->segA[j].sf_id ] )
          {
            // ... then reset it to the beginning of segA[j].
            _reset_sf(proc,p->segA + j, p->segA[j].beg_loc_id);
            updA[ p->segA[j].sf_id ] = true;
            loc_id_cnt += 1;
          }
        }   
        return rc;
      }

      rc_t _on_any_loc( proc_t* proc, inst_t* p, unsigned beg_loc_id )
      {
        rc_t rc = kOkRC;
        
        // scan to the first segment containing beg_loc_id
        for(unsigned i=0; i<p->segN; ++i)
          if( p->segA[i].beg_loc_id <= beg_loc_id && beg_loc_id <= p->segA[i].end_loc_id )
          {
            rc = _on_loc(proc,p,i,beg_loc_id);
            break;
          }

        return rc;
      }

      rc_t _check_for_end_loc( proc_t* proc, inst_t* p, unsigned cur_loc_id )
      {

        // if cur_loc_id is the end_loc of a segment ...
        for(unsigned i=0; i<p->segN; ++i)
          if( p->segA[i].end_loc_id == cur_loc_id )
          {
            // then reset all SF's to their next segment
            if( i+1 < p->segN )
            {
              // advance to the start of the next segment
              _on_loc(proc,p,i+1,p->segA[i+1].beg_loc_id);
            }
            else
            {
              proc_info(proc,"END OF LAST SEGMENT while advancing.");
            }
          }

        return kOkRC;
      }


      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        if( var->vid == kGotoLocPId || var->vid == kManualLocPId )
        {
          unsigned loc_id;
          if( var_get(var,loc_id) == kOkRC )
          {
            rc = _on_any_loc(proc,p,loc_id);
            //printf("gutim_sf_ctl: GOTO:%i\n",loc_id);
          }
        }
        return rc;
      }

      rc_t _handle_sf_loc( proc_t* proc, inst_t* p, unsigned vid, unsigned& loc_fld_idx_ref )
      {
        rc_t rc = kOkRC;
        const rbuf_t* rbuf;
        var_get(proc,vid,kAnyChIdx,rbuf);

        // if the location field index has not yet been set
        if( loc_fld_idx_ref==kInvalidIdx && rbuf != nullptr )
        {
          if((loc_fld_idx_ref = recd_type_field_index(rbuf->type,"loc")) == kInvalidIdx )
          {
            rc = proc_error(proc,kInvalidArgRC,"The incoming 'loc' record does not have a 'loc' field on '%s'.",cwStringNullGuard(proc->label));
            goto errLabel;
          }
        }

        // if the location field index is valid
        if( loc_fld_idx_ref != kInvalidIdx )
        {
          for(unsigned i=0; i<rbuf->recdN; ++i)
          {
            unsigned loc_id;

            // get the location id
            if((rc = recd_get(rbuf->type, rbuf->recdA + i, loc_fld_idx_ref, loc_id)) != kOkRC )
            {
              rc = proc_error(proc,rc,"Error accessing 'loc' field on '%s'.",cwStringNullGuard(proc->label));
              goto errLabel;
            }

            // if loc_id is an 'end_loc' then reset all sf's to their next segment
            if((rc = _check_for_end_loc(proc,p,loc_id)) != kOkRC )
              goto errLabel;
          }
          
        }

      errLabel:
        return rc;
        
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;
        _handle_sf_loc(proc,p,kSfAPId,p->loc_a_fld_idx);
        _handle_sf_loc(proc,p,kSfBPId,p->loc_b_fld_idx);
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .notify  = std_notify<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    // gutim_sf_ctl


    //------------------------------------------------------------------------------------------------------------------
    //
    // gutim_spirio_ctl
    //
    namespace gutim_spirio_ctl
    {
      enum {
        kCfgFnamePId,
        kResetPId,
        kSegIdInPId,
        kMidiInAPId,
        kMidiInBPId,
        kSegIdOutPId,
        kPedalRlsPId
      };

      enum {
        kPianoStateDetTypeId,
        kSequenceDetTypeId
      };

      enum {
        kMidiPortCnt = 2
      };

      typedef midi_detect::state_t event_t;
      
      typedef struct recd_str
      {
        unsigned id;
        char*    label;
        unsigned piano_id;
        unsigned trig_seg_id;        // 
        unsigned det_type_id;        // See k???DetTypeId above
        unsigned det_id;             // det_id provided by midi_detector::piano or midi_detector::seq
        unsigned min_ms;             // wait at least this long after the detector is activated to trigger the Spirio
        unsigned max_ms;             // wait at most this long after the detector is activated to trigger the Spirio
        unsigned min_cycle_cnt;      // derived from min_ms
        unsigned max_cycle_cnt;      // derived from max_ms (set to 0 to disable time-out)
        unsigned pdetEvtN;
        event_t* pdetEvtA;
        unsigned sdetEvtN;
        event_t* sdetEvtA;
      } recd_t;
      
      typedef struct
      {
        unsigned recdN;
        recd_t*  recdA;
        unsigned ped_release_thresh;
        unsigned midi_a_fld_idx;
        unsigned midi_b_fld_idx;
        unsigned armed_recd_idx;
        unsigned armed_cycle_cnt;
        unsigned defer_detect_fl;

        midi_detect::piano::handle_t pnoDetA[ kMidiPortCnt ];
        midi_detect::seq::handle_t   seqDetA[ kMidiPortCnt ];

        
      } inst_t;

      rc_t _setup_detector( proc_t* proc, inst_t* p, recd_t* r )
      {
        rc_t rc = kOkRC;
        
        switch( r->det_type_id )
        {
          case kPianoStateDetTypeId:
            if((rc = setup_detector(p->pnoDetA[ r->piano_id ],r->pdetEvtA,r->pdetEvtN,r->det_id)) != kOkRC )
            {
              rc = proc_error(proc,rc,"The piano state detector setup failed on spirio player record: '%s'.",cwStringNullGuard(r->label));
              goto errLabel;
            }
            break;

          case kSequenceDetTypeId:
            if((rc = setup_detector(p->seqDetA[ r->piano_id ],r->sdetEvtA,r->sdetEvtN,r->pdetEvtA,r->pdetEvtN,r->det_id)) != kOkRC )
            {
              rc = proc_error(proc,rc,"The sequence state detector setup failed on spirio player record: '%s'.",cwStringNullGuard(r->label));
              goto errLabel;
            }
            break;

          default:
            rc = proc_error(proc,kInvalidArgRC,"Unknown detector type.");
        }

      errLabel:
        return rc;
        
      }


      rc_t _create_detectors( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        
        for(unsigned i=0; i<kMidiPortCnt; ++i)
        {
          if((rc = create( p->pnoDetA[i], p->recdN, p->ped_release_thresh )) != kOkRC )
          {
            rc = proc_error(proc,rc,"MIDI piano state detector create failed.");
            goto errLabel;
          }

          if((rc = create( p->seqDetA[i], p->recdN, p->ped_release_thresh )) != kOkRC )
          {
          rc = proc_error(proc,rc,"MIDI sequence detector create failed.");
          goto errLabel;
          }
        }

        for(unsigned i=0; i<p->recdN; ++i)
        {
          recd_t* r = p->recdA + i;
          if((rc = _setup_detector(proc,p,r)) != kOkRC )
          {
            goto errLabel;
          }
        }

      errLabel:
        return rc;
        
      }

      

      rc_t _reset_detector( proc_t* proc, inst_t* p, unsigned recd_idx )
      {
        rc_t rc = kOkRC;
        recd_t* r = nullptr;

        if( recd_idx == kInvalidIdx )
          goto errLabel;

        if( recd_idx >= p->recdN )
        {
          rc = proc_error(proc,kInvalidArgRC,"An invalid detector index was encountered.");
          goto errLabel;
        }
        
        r = p->recdA + recd_idx;
        
        switch( r->det_type_id )
        {
          case kPianoStateDetTypeId:
            if((rc = reset(p->pnoDetA[r->piano_id])) != kOkRC )
            {
              rc = proc_error(proc,rc,"The piano state detector reset failed on spirio player record: '%s'.",cwStringNullGuard(r->label));
              goto errLabel;
            }
            break;

          case kSequenceDetTypeId:
            if((rc = reset(p->seqDetA[r->piano_id])) != kOkRC )
            {
              rc = proc_error(proc,rc,"The sequence state detector reset failed on spirio player record: '%s'.",cwStringNullGuard(r->label));
              goto errLabel;
            }
            break;

          default:
            rc = proc_error(proc,kInvalidArgRC,"Unknown detector type.");
        }

      errLabel:
        return rc;
      }

      rc_t _arm_detector( proc_t* proc, inst_t* p, unsigned recd_idx )
      {
        rc_t    rc = kOkRC;
        recd_t* r = nullptr;

        if( recd_idx == kInvalidIdx )
          goto errLabel;

        if( recd_idx >= p->recdN )
        {
          rc = proc_error(proc,kInvalidArgRC,"An invalid detector index was encountered.");
          goto errLabel;
        }
        
        r = p->recdA + recd_idx;
        
        switch( r->det_type_id )
        {
          case kPianoStateDetTypeId:
            break;

          case kSequenceDetTypeId:
            if((rc = arm_detector(p->seqDetA[r->piano_id],r->det_id)) != kOkRC )
            {
              rc = proc_error(proc,rc,"The sequence state detector activation failed on spirio player record: '%s'.",cwStringNullGuard(r->label));
              goto errLabel;
            }
            break;

          default:
            rc = proc_error(proc,kInvalidArgRC,"Unknown detector type.");
        }

      errLabel:
        return rc;
      }


      rc_t _is_detector_triggered( proc_t* proc, inst_t* p, bool& is_trig_fl_ref )
      {
        rc_t    rc = kOkRC;
        recd_t* r = nullptr;
        
        is_trig_fl_ref = false;

        // if no detector is activated then there is nothing to do
        if( p->armed_recd_idx == kInvalidIdx )
          goto errLabel;

        // validate the armed detector index
        if( p->armed_recd_idx >= p->recdN )
        {
          rc = proc_error(proc,kInvalidArgRC,"An invalid detector index was encountered.");
          goto errLabel;
        }
        
        r = p->recdA + p->armed_recd_idx;

        // if the trigger was deferred and the min time has expired then issue the trigger
        if( p->defer_detect_fl && r->min_cycle_cnt <= p->armed_cycle_cnt && p->armed_cycle_cnt <= r->max_cycle_cnt && r->max_cycle_cnt != 0 )
        {
          is_trig_fl_ref = true;
          p->defer_detect_fl = false;
          goto errLabel;
        }

        // check if the external detector has triggered
        switch( r->det_type_id )
        {
          case kPianoStateDetTypeId:
            if((rc = is_any_state_matched(p->pnoDetA[r->piano_id],r->det_id,is_trig_fl_ref)) != kOkRC )
              rc = proc_error(proc,rc,"Piano state detector match test failed.");            
            break;

          case kSequenceDetTypeId:
            is_trig_fl_ref = is_detector_triggered(p->seqDetA[r->piano_id]);
            break;

          default:
            rc = proc_error(proc,kInvalidArgRC,"Unknown detector type.");
        }

        // if the external detector has triggered but we have not yet passed the
        // min time since the detector was activated then defer reporting the trigger
        if( is_trig_fl_ref && p->armed_cycle_cnt < r->min_cycle_cnt )
        {
          p->defer_detect_fl = true;
          is_trig_fl_ref = false;
        }

        // If the detector was activated but it has not triggered in 'max_cycle_cnt' cycles 
        // then force the trigger.
        if( r->max_cycle_cnt != 0 && p->armed_cycle_cnt > r->max_cycle_cnt )
        {
          proc_info(proc,"'%s' Spirio playback timed out.  Trigger forced.",cwStringNullGuard(r->label));
          
          is_trig_fl_ref = true;
          p->defer_detect_fl = false;              
          
        }

        if( p->armed_recd_idx != kInvalidIdx )
          p->armed_cycle_cnt += 1;
        
      errLabel:
        return rc;
      }

      rc_t _on_midi( proc_t* proc, inst_t* p, unsigned piano_id, const midi::ch_msg_t* m )
      {
        rc_t rc = kOkRC;
        if((rc = on_midi(p->pnoDetA[piano_id],m,1)) != kOkRC )
        {
          rc = proc_error(proc,rc,"MIDI update failed on the piano state detector.");
          goto errLabel;
        }
            
        if((rc = on_midi(p->seqDetA[piano_id],m,1)) != kOkRC )
        {
          rc = proc_error(proc,rc,"MIDI update failed on the MIDI sequence detector.");
          goto errLabel;
        }
            
      errLabel:
        return rc;
      }

      rc_t _parse_cfg_event_list( proc_t* proc, const object_t* cfg_evtL, recd_t* r, unsigned det_type_id, event_t*& evtA_ref, unsigned& evtN_ref )
      {
        rc_t rc;

        evtN_ref = cfg_evtL->child_count();
        evtA_ref = mem::allocZ<event_t>(evtN_ref);
        
          
        for(unsigned j=0; j<evtN_ref; ++j)
        {
          const object_t* cfg_evt = cfg_evtL->child_ele(j);
          event_t* evt = evtA_ref + j;
            
          if((rc = cfg_evt->getv("ch",evt->ch,
                                 "status",evt->status,
                                 "d0",evt->d0)) != kOkRC )
          {
            rc = proc_error(proc,rc,"Parse failed on spirio player record '%s' on event index %i.",cwStringNullGuard(r->label),j);
              goto errLabel;
          }
          
          evt->order = kInvalidId;

          switch( det_type_id )
          {
          
            case kSequenceDetTypeId:
              if((rc = cfg_evt->getv("order",evt->order)) != kOkRC )
              {
                rc = proc_error(proc,rc,"Parse failed on spirio player record '%s' on 'order' field at event index %i.",cwStringNullGuard(r->label),j);
                goto errLabel;
              };
              break;

            case kPianoStateDetTypeId:
              if((rc = cfg_evt->getv("release_fl",evt->release_fl)) != kOkRC )
              {
                rc = proc_error(proc,rc,"Parse failed on spirio player record '%s' on 'release_tl' field at event index %i.",cwStringNullGuard(r->label),j);
                goto errLabel;
              };
              break;
          }
        }

      errLabel:
        return rc;
      }
/*
{
  "list": [
    {
      "id": 0,
      "trig_seg_id": 3,
      "seg_label": "gutim_4",
      "spirio_label": "spirio_1",
      "piano_id": 0,
      "piano_label": "A",
      "min_ms": 0,
      "max_ms": 2000,
      "type": "p_det",
      "pdetL": [
        {
          "ch": 0,
          "status": 144,
          "d0": 43,
          "release_fl": true
        }
      ],
      "sdetL": []
    },
    {
  
 */
      
      rc_t _load_from_cfg( proc_t* proc, inst_t* p, const object_t* cfg )
      {
        rc_t rc = kOkRC;
        const object_t* cfg_list = nullptr;

        // get the 'list' element from the root
        if((rc = cfg->getv("list", cfg_list)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Unable to locate the 'list' field gutim_ctl cfg.");
          goto errLabel;
        }

        if( !cfg_list->is_list() )
        {
          rc = proc_error(proc,rc,"Expected gutim_ctl list to be of type 'list'.");
          goto errLabel;
        }

        p->recdN           = cfg_list->child_count();
        p->recdA           = mem::allocZ<recd_t>(p->recdN);
        p->armed_recd_idx  = kInvalidIdx;
        p->armed_cycle_cnt = 0;
        p->defer_detect_fl = false;

        // for each detection location
        for(unsigned i=0; i<p->recdN; ++i)
        {
          const object_t* ele        = cfg_list->child_ele(i);
          recd_t*         r          = p->recdA + i;
          const char*     type_str   = nullptr;
          const object_t* cfg_pdetEvtL = nullptr;
          const object_t* cfg_sdetEvtL = nullptr;
          const char*     label;
          
          if((rc = ele->getv("id",r->id,
                             "trig_seg_id",r->trig_seg_id,
                             "spirio_label",label,
                             "piano_id",r->piano_id,
                             "min_ms",r->min_ms,
                             "max_ms",r->max_ms,
                             "type", type_str )) != kOkRC )
          {
            rc = proc_error(proc,rc,"Error parsing detector cfg at index %i.",i);
            goto errLabel;
          }

          if((rc = ele->getv_opt("pdetL",cfg_pdetEvtL,
                                 "sdetL",cfg_sdetEvtL)) != kOkRC )
          {
            
            rc = proc_error(proc,rc,"Error parsing optional fields from detector cfg at index %i.",i);
            goto errLabel;
          }

          if( r->piano_id == kInvalidId || r->piano_id >= kMidiPortCnt )
          {
            rc = proc_error(proc,kInvalidArgRC,"A piano id (%i) was found to be greater than the MIDI port count (%i).",r->piano_id,kMidiPortCnt );
            goto errLabel;
          }

          if( textIsEqual(type_str,"p_det") )
            r->det_type_id = kPianoStateDetTypeId;
          else
          {
            if( textIsEqual(type_str,"s_det"))
              r->det_type_id = kSequenceDetTypeId;
            else
            {
              rc = proc_error(proc,kInvalidArgRC,"The event type '%s' is not recognized as a gutim spirio player event type in '%s'.", cwStringNullGuard(type_str),cwStringNullGuard(label));
              goto errLabel;
            }
          }

          if( r->min_ms > r->max_ms )
          {
            rc = proc_error(proc,kInvalidArgRC,"The 'min_ms' cannot be larger than the 'max_ms' in the spirio player event '%s'.",cwStringNullGuard(label));
            goto errLabel;
          }
          
          r->min_cycle_cnt = (unsigned)((r->min_ms * proc->ctx->sample_rate) / (proc->ctx->framesPerCycle * 1000));
          r->max_cycle_cnt = (unsigned)((r->max_ms * proc->ctx->sample_rate) / (proc->ctx->framesPerCycle * 1000));

          assert( r->max_cycle_cnt==0 || r->min_cycle_cnt <= r->max_cycle_cnt);

          r->label = mem::duplStr(label);


          // both the piano and seq detectors may have piano-state parameters
          if((rc = _parse_cfg_event_list( proc, cfg_pdetEvtL, r, kPianoStateDetTypeId, r->pdetEvtA, r->pdetEvtN )) != kOkRC )
          {
            goto errLabel;
          }

          // only seq-detectors use seq parameters
          if( r->det_type_id == kSequenceDetTypeId )
          {
            if((rc = _parse_cfg_event_list( proc, cfg_sdetEvtL, r, kSequenceDetTypeId, r->sdetEvtA, r->sdetEvtN )) != kOkRC )
            {
              goto errLabel;
            }
          }
        }

        if((rc = _create_detectors(proc,p)) != kOkRC )
          goto errLabel;

      errLabel:
        return rc;
      }

      rc_t _parse_cfg_file( proc_t* proc, inst_t* p, const char* fname )
      {
        rc_t rc = kOkRC;
        char* fn = nullptr;;
        object_t* cfg = nullptr;
        
        if((fn = proc_expand_filename(proc,fname)) == nullptr )
        {
          rc = proc_error(proc,kOpFailRC,"The cfg file name '%s' could not be expanded.",cwStringNullGuard(fname));
          goto errLabel;
        }
          
        if((rc = objectFromFile( fn, cfg )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Unable to parse cfg from '%s'.",cwStringNullGuard(fn));
          goto errLabel;
        }

        if((rc = _load_from_cfg( proc, p, cfg)) != kOkRC )
        {
          goto errLabel;
        }

      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Configuration file parsing failed on '%s' in '%s'.",cwStringNullGuard(fname),cwStringNullGuard(proc->label));

        mem::release(fn);

        if( cfg != nullptr )
          cfg->free();
        
        return rc;
      }

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t          rc          = kOkRC;        
        const char*   cfg_fname   = nullptr;
        bool          reset_fl    = false;
        unsigned      seg_id_in   = kInvalidId;
        const rbuf_t* midi_a_rbuf = nullptr;
        const rbuf_t* midi_b_rbuf = nullptr;
        unsigned      seg_id_out  = kInvalidId;
        
        if((rc = var_register_and_get(proc, kAnyChIdx,
                                      kCfgFnamePId, "cfg_fname", kBaseSfxId, cfg_fname,
                                      kResetPId,    "reset",     kBaseSfxId, reset_fl,
                                      kSegIdInPId,  "seg_id_in", kBaseSfxId, seg_id_in,
                                      kMidiInAPId,  "midi_in_a", kBaseSfxId, midi_a_rbuf,
                                      kMidiInBPId,  "midi_in_b", kBaseSfxId, midi_b_rbuf,
                                      kSegIdOutPId, "seg_id_out",kBaseSfxId, seg_id_out,
                                      kPedalRlsPId, "ped_rls",   kBaseSfxId, p->ped_release_thresh)) != kOkRC )

        {
          goto errLabel;
        }

        if((rc = _parse_cfg_file(proc,p,cfg_fname)) != kOkRC )
        {
          goto errLabel;
        }

        if(( p->midi_a_fld_idx = recd_type_field_index(midi_a_rbuf->type,"midi")) == kInvalidIdx )
        {
          rc = proc_error(proc,kInvalidArgRC,"The MIDI A input record does not contain the field 'midi' in '%s'.",cwStringNullGuard(proc->label));
          goto errLabel;
        }

        if(( p->midi_b_fld_idx = recd_type_field_index(midi_b_rbuf->type,"midi")) == kInvalidIdx )
        {
          rc = proc_error(proc,kInvalidArgRC,"The MIDI B input record does not contain the field 'midi' in '%s'.",cwStringNullGuard(proc->label));
          goto errLabel;
        }
        
        
      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        for(unsigned i=0; i<p->recdN; ++i)
        {
          mem::release(p->recdA[i].label);
          mem::release(p->recdA[i].pdetEvtA);
          mem::release(p->recdA[i].sdetEvtA);
        }

        for(unsigned i=0; i<kMidiPortCnt; ++i)
        {
          destroy(p->pnoDetA[i]);
          destroy(p->seqDetA[i]);
            
        }

        mem::release(p->recdA);

        return rc;
      }

      rc_t _handle_seg_id( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        unsigned seg_id = kInvalidId;
        
        if((rc = var_get(proc,kSegIdInPId,kAnyChIdx,seg_id)) != kOkRC )
          goto errLabel;
        
        // check each record to see if this seg activates any detectors
        for(unsigned j=0; j<p->recdN; ++j)
        {
          if( p->recdA[j].trig_seg_id == seg_id )
          {

            if((rc =_arm_detector(proc, p, j )) != kOkRC )
              goto errLabel;
                      
            p->armed_recd_idx = j;
            p->armed_cycle_cnt = 0;
            p->defer_detect_fl = false;

            proc_info(proc,"SPIRIO DETECTOR ACTIVATED:%s",cwStringNullGuard(p->recdA[ p->armed_recd_idx].label));
              
            break;
          }
        }
        
      errLabel:
        return rc;       
      }

      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        if( proc->ctx->isInRuntimeFl )
        {
          switch( var->vid )
          {
            case kSegIdInPId:
              _handle_seg_id(proc,p);
              break;
              
            case kResetPId:
              p->armed_recd_idx = kInvalidIdx;
              break;
          }
        }
        return rc;
      }

      

      rc_t _handle_midi( proc_t* proc, inst_t* p, unsigned vid, unsigned midi_fld_idx )
      {
        rc_t            rc   = kOkRC;
        const rbuf_t*   rbuf = nullptr;
        midi::ch_msg_t* m    = nullptr;
        
        if((rc = var_get(proc,vid,kAnyChIdx,rbuf)) != kOkRC )
          goto errLabel;

        for(unsigned i=0; i<rbuf->recdN; ++i)
        {
          if((rc = recd_get( rbuf->type, rbuf->recdA+i, midi_fld_idx, m)) != kOkRC )
          {
            rc = proc_error(proc,rc,"The 'midi' field read failed.");
            goto errLabel;
          }

          switch(vid)
          {
            case kMidiInAPId:
              _on_midi(proc,p,0,m);
              break;
              
            case kMidiInBPId:
              _on_midi(proc,p,1,m);
              break;
              
            default:
              assert(0);
          }

         
        }
        
      errLabel:
        return rc;
      }


      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;
        bool trig_fl = false;
        
        if((rc = _handle_midi(proc,p,kMidiInAPId,p->midi_a_fld_idx)) != kOkRC )
          goto errLabel;

        if((rc = _handle_midi(proc,p,kMidiInBPId,p->midi_b_fld_idx)) != kOkRC )
          goto errLabel;

        if((rc = _is_detector_triggered(proc,p,trig_fl )) != kOkRC )
          goto errLabel;
           
        if( trig_fl )
        {
          proc_info(proc,"SPIRIO DETECTOR TRIGGERED:%s",cwStringNullGuard(p->recdA[ p->armed_recd_idx].label));
          var_set(proc,kSegIdOutPId,kAnyChIdx,p->recdA[ p->armed_recd_idx ].trig_seg_id);
          p->armed_recd_idx = kInvalidIdx;
        }

      errLabel:
        
        return rc;
        
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .notify  = std_notify<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    } // gutim_spirio_ctl

    //------------------------------------------------------------------------------------------------------------------
    //
    // gutim_pgm_ctl
    //
    namespace gutim_pgm_ctl
    {
      enum {
        kCfgFnamePId,
        
        kSfLocPId,
        kGotoSegPId,
        kPlayNowPId,
        kRecoverPId,
        kResetPId,
        kLocOutPId,
        
        kBegLocSfAPId,
        kEndLocSfAPId,
        kResetSfAPId,
        kEnableSfAPId,

        kBegLocSfBPId,
        kEndLocSfBPId,
        kResetSfBPId,
        kEnableSfBPId,

        kSimPlayPId,
        kSimResetPId,
        kSimClearPId,
        
        kSprPlayPId,
        kSprResetPId,
        kSprPlayNowPId
        
      };

      enum {
        kSfAId,
        kSfBId
      };
      
      enum {
        kPlayCmdTId,
        kSfCmdTId
      };

      enum {
        kSpirioPlyrId,
        kSimulPlyrId
      };

      typedef struct sf_cmd_str
      {
        unsigned sf_id; //
        unsigned beg_loc;
        unsigned end_loc;
        bool enable_fl;
      } sf_cmd_t;

      typedef struct plyr_cmd_str
      {
        unsigned plyr_id;      // kSpirioPlyrId | kSimulPlyrId
        unsigned seg_id;       // segment to play
        char*    seg_label;    // segment label
        char*    person_label; // pianist_id
        unsigned person_seg_num; // seg_num counts the segments for this player (first segment==1, 2nd segment==2, ...)
      } plyr_cmd_t;
      
      typedef struct cmd_str
      {
        unsigned tid;              // kPlayCmdTId | kSfCmdTId
        unsigned active_sf_id;     // this is the sf_id of the SF that is active during this segment
        union {
          sf_cmd_t   sf;
          plyr_cmd_t plyr;
        } u;
      } cmd_t;
      
      typedef struct ctl_str
      {
        unsigned seg_id;       // segment during which the state initiated by these commands will be active
        unsigned loc_id;       // location where these commands will be applied
        unsigned active_sf_id; // the SF that is active during this segment
        cmd_t*   cmdA;
        unsigned cmdN;
      } ctl_t;
      
      
      typedef struct
      {
        ctl_t*   ctlA;
        unsigned ctlN;
        unsigned sf_loc_fld_idx;
        
        unsigned last_ctl_idx;
        unsigned last_loc_id;
      } inst_t;
/*
  {
  "ctlL": [
    {
      "loc_id": 0,
      "seg_id": 0,
      "active_sf_id": 1,
      "cmdL": [
        {
          "type": "play",
          "seg_type": "simul",
          "seg_label": "gutim_1",
          "seg_id": 0,
          "piano_id": 1,
          "player_label": "N",
          "bloc": 0,
          "eloc": 44
        },
        {
          "type": "sf",
          "sf_id": 0,
          "bloc": 45,
          "eloc": 103,
          "enable_fl": true
        },
        {
          "type": "sf",
          "sf_id": 1,
          "bloc": 0,
          "eloc": 44,
          "enable_fl": true
        }
      ]
    },
    ...
    }
 */
      
      
      rc_t _parse_sf_cmd( proc_t* proc, inst_t* p, sf_cmd_t* cmd, const object_t* cfg_cmd )
      {
        rc_t rc = kOkRC;
        if((rc = cfg_cmd->getv("sf_id",cmd->sf_id,
                               "bloc",cmd->beg_loc,
                               "eloc",cmd->end_loc,
                               "enable_fl",cmd->enable_fl)) != kOkRC )
        {
          rc = proc_error(proc,rc,"SF cmd cfg. parsing failed.");
          goto errLabel;
        }

        if( cmd->sf_id != kSfAId && cmd->sf_id != kSfBId )
        {
          rc = proc_error(proc,kInvalidArgRC,"The SF id must be either 0 or 1 not '%i'.",cmd->sf_id);
          goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      unsigned _seg_type_label_to_plyr_id( proc_t* proc, const char* seg_type_label, unsigned& tid_ref )
      {
        rc_t rc = kOkRC;
        
        tid_ref = kInvalidId;
        
        if( textIsEqual(seg_type_label,"spirio") )
          tid_ref = kSpirioPlyrId;
        else
        {
          if( textIsEqual(seg_type_label,"simul") )
            tid_ref = kSimulPlyrId;
          else
          {
            tid_ref = kInvalidId;
            rc = proc_error(proc,kInvalidArgRC,"Unknown seg type label:'%s'.",cwStringNullGuard(seg_type_label));
          }
        }

        return rc;
      }

      rc_t _parse_plyr_cmd( proc_t* proc, inst_t* p, plyr_cmd_t* plyr_cmd, const object_t* cfg_cmd )
      {
        rc_t        rc             = kOkRC;
        const char* seg_type_label = nullptr;
        const char* seg_label      = nullptr;
        const char* person_label   = nullptr;
        

        if((rc = cfg_cmd->getv("seg_type",seg_type_label,
                               "seg_label",seg_label,
                               "seg_id",plyr_cmd->seg_id,    // segment id of the segment to playback
                               "player_seg_num",plyr_cmd->person_seg_num, 
                               "player_label",person_label)) != kOkRC )
        {
          goto errLabel;
        }

        plyr_cmd->seg_label    = mem::duplStr(seg_label);
        plyr_cmd->person_label = mem::duplStr(person_label);
        
        if((rc = _seg_type_label_to_plyr_id(proc,seg_type_label,plyr_cmd->plyr_id)) == kInvalidId )
        {
          goto errLabel;
        }
        
      errLabel:
        
        return rc;
      }

      rc_t _cmd_type_label_to_plyr_id( proc_t* proc, const char* cmd_type_label, unsigned& tid_ref )
      {
        rc_t rc = kOkRC;
        tid_ref = kInvalidId;
        
        if( textIsEqual(cmd_type_label,"sf") )
          tid_ref = kSfCmdTId;
        else
        {
          if( textIsEqual(cmd_type_label,"play") )
            tid_ref = kPlayCmdTId;
          else
          {
            tid_ref = kInvalidId;
            rc = proc_error(proc,kInvalidArgRC,"Unknown cmd type: '%s'.",cwStringNullGuard(cmd_type_label));
          }
            
        }

        return rc;
      }

            
      
      rc_t _parse_cfg( proc_t* proc, inst_t* p, const object_t* cfg )
      {
        rc_t rc = kOkRC;

        const object_t* cfg_ctlL = nullptr;
        
        if((rc = cfg->getv("ctlL",cfg_ctlL)) != kOkRC )
        {
          goto errLabel;
        }

        p->ctlN             = cfg_ctlL->child_count();
        p->ctlA             = mem::allocZ<ctl_t>(p->ctlN);
        p->last_ctl_idx = kInvalidIdx;
        p->last_loc_id  = kInvalidId;

        for(unsigned ctl_idx=0; ctl_idx<p->ctlN; ++ctl_idx)
        {
          const object_t* cfg_ctl  = cfg_ctlL->child_ele(ctl_idx);
          const object_t* cfg_cmdL = nullptr;
          ctl_t*          ctl      = p->ctlA + ctl_idx;
          
          if((rc = cfg_ctl->getv("loc_id",ctl->loc_id,
                                 "seg_id",ctl->seg_id,
                                 "active_sf_id",ctl->active_sf_id,
                                 "cmdL",cfg_cmdL)) != kOkRC )
          {
            goto errLabel;
          }

          ctl->cmdN = cfg_cmdL->child_count();
          ctl->cmdA = mem::allocZ<cmd_t>(ctl->cmdN);

          for(unsigned cmd_idx=0; cmd_idx<ctl->cmdN; ++cmd_idx)
          {            
            const char*     cmd_type_label = nullptr;
            const object_t* cfg_cmd        = cfg_cmdL->child_ele(cmd_idx);
            cmd_t*          cmd            = ctl->cmdA + cmd_idx;

            if((rc = cfg_cmd->getv("type",cmd_type_label)) != kOkRC )
            {
              goto errLabel;
            }

            if((rc = _cmd_type_label_to_plyr_id(proc, cmd_type_label,cmd->tid)) != kOkRC )
            {
              goto errLabel;
            }

            switch( cmd->tid )
            {
              case kSfCmdTId:
                rc = _parse_sf_cmd(proc,p,&cmd->u.sf,cfg_cmd);
                break;
                
              case kPlayCmdTId:
                rc = _parse_plyr_cmd(proc,p,&cmd->u.plyr,cfg_cmd);
                break;

              default:
                rc = proc_error(proc,kInvalidArgRC,"Unknown cmd type in '%s'.",cwStringNullGuard(proc->label));
                goto errLabel;
            }            
          }
        }
      errLabel:
        return rc;
      }
      
      rc_t _parse_cfg_file( proc_t* proc, inst_t* p, const char* fname )
      {
        rc_t rc = kOkRC;
        char* fn = nullptr;;
        object_t* cfg = nullptr;
        
        if((fn = proc_expand_filename(proc,fname)) == nullptr )
        {
          rc = proc_error(proc,kOpFailRC,"The cfg file name '%s' could not be expanded.",cwStringNullGuard(fname));
          goto errLabel;
        }
          
        if((rc = objectFromFile( fn, cfg )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Unable to parse cfg from '%s'.",cwStringNullGuard(fn));
          goto errLabel;
        }

        if((rc = _parse_cfg( proc, p, cfg)) != kOkRC )
        {
          goto errLabel;
        }

      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Configuration file parsing failed on '%s' in '%s'.",cwStringNullGuard(fname),cwStringNullGuard(proc->label));

        mem::release(fn);

        if( cfg != nullptr )
          cfg->free();
        
        return rc;
      }

      rc_t _on_goto_seg( proc_t* proc, inst_t* p ); // forward declaration

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t          rc        = kOkRC;        
        const char*   cfg_fname = nullptr;
        const rbuf_t* rbuf      = nullptr;
        unsigned      starting_seg_id = kInvalidId;
        
        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kSfLocPId,   "sf_loc",   kBaseSfxId, rbuf,
                                      kGotoSegPId, "goto_seg", kBaseSfxId, starting_seg_id,
                                      kCfgFnamePId,"cfg_fname",kBaseSfxId, cfg_fname)) != kOkRC )
        {
           goto errLabel;
        }

        if((rc = var_register(proc,kAnyChIdx,
                              kGotoSegPId,   "goto_seg",kBaseSfxId,
                              kPlayNowPId,   "play_now",kBaseSfxId,
                              kRecoverPId,   "recover", kBaseSfxId,
                              kResetPId,     "reset",   kBaseSfxId,
                              kLocOutPId,    "loc_out", kBaseSfxId,

                              kBegLocSfAPId, "sf_a_beg_loc",   kBaseSfxId,
                              kEndLocSfAPId, "sf_a_end_loc",   kBaseSfxId,
                              kResetSfAPId,  "sf_a_reset_fl",  kBaseSfxId,
                              kEnableSfAPId, "sf_a_enable_fl", kBaseSfxId,
                              
                              kBegLocSfBPId, "sf_b_beg_loc",   kBaseSfxId,
                              kEndLocSfBPId, "sf_b_end_loc",   kBaseSfxId,
                              kResetSfBPId,  "sf_b_reset_fl",  kBaseSfxId,
                              kEnableSfBPId, "sf_b_enable_fl", kBaseSfxId,

                              kSimPlayPId,   "sim_play_id",    kBaseSfxId,
                              kSimResetPId,  "sim_reset_fl",   kBaseSfxId,
                              kSimClearPId,  "sim_clear_fl",   kBaseSfxId,
                              
                              kSprPlayPId,   "spr_play_id",    kBaseSfxId,
                              kSprResetPId,  "spr_reset_fl",   kBaseSfxId,
                              kSprPlayNowPId,"spr_play_now",   kBaseSfxId )) != kOkRC )
        {
          goto errLabel;
        }
                              
        if((rc = _parse_cfg_file(proc,p,cfg_fname)) != kOkRC )
        {
          goto errLabel;
        }
        
        if(( p->sf_loc_fld_idx = recd_type_field_index(rbuf->type,"loc")) == kInvalidIdx )
        {
          rc = proc_error(proc,kInvalidArgRC,"The 'sf_loc' record does not contain the field 'loc' in '%s'.",cwStringNullGuard(proc->label));
          goto errLabel;
        }

        if( starting_seg_id != kInvalidId )
        {
          _on_goto_seg(proc,p);
        }

      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;


        for(unsigned i=0; i<p->ctlN; ++i)
        {
          for(unsigned j=0; j<p->ctlA[i].cmdN; ++j)
          {
            if(  p->ctlA[i].cmdA[j].tid == kPlayCmdTId )
            {
              mem::release(p->ctlA[i].cmdA[j].u.plyr.seg_label);
              mem::release(p->ctlA[i].cmdA[j].u.plyr.person_label);
            }
          }
          mem::release(p->ctlA[i].cmdA);
        }
        mem::release(p->ctlA);

        return rc;
      }



      rc_t _apply_sf_cmd( proc_t* proc, inst_t* p, const sf_cmd_t& cmd )
      {
        rc_t rc = kOkRC;
        
        switch( cmd.sf_id )
        {
          case kSfAId:
            var_set(proc,kBegLocSfAPId,kAnyChIdx,cmd.beg_loc);
            var_set(proc,kEndLocSfAPId,kAnyChIdx,cmd.end_loc);
            var_set(proc,kEnableSfAPId,kAnyChIdx,cmd.enable_fl);
            var_set(proc,kResetSfAPId,kAnyChIdx,true);
            break;
            
          case kSfBId:
            var_set(proc,kBegLocSfBPId,kAnyChIdx,cmd.beg_loc);
            var_set(proc,kEndLocSfBPId,kAnyChIdx,cmd.end_loc);
            var_set(proc,kEnableSfBPId,kAnyChIdx,cmd.enable_fl);
            var_set(proc,kResetSfBPId,kAnyChIdx,true);
            break;
            
          default:
            rc  = proc_error(proc,kInvalidStateRC,"An invalid SF id (%i) was encountered.",cmd.sf_id);
        }

        return rc;
      }

      rc_t _apply_plyr_cmd( proc_t* proc, inst_t* p, const plyr_cmd_t& cmd, bool play_now_fl )
      {
        rc_t rc = kOkRC;

        switch( cmd.plyr_id )
        {
          case kSpirioPlyrId:
            var_set(proc,play_now_fl ? kSprPlayNowPId : kSprPlayPId, kAnyChIdx, cmd.seg_id);

            if( play_now_fl )
              var_set(proc,kSimClearPId, kAnyChIdx, true);
            
            break;
            
          case kSimulPlyrId:
            var_set(proc,kSimPlayPId,kAnyChIdx,cmd.seg_id);
            break;
            
          default:
            rc = proc_error(proc,kInvalidStateRC,"An invalid player id (%i) was encountered.",cmd.plyr_id);
        }
        
        return rc;
      }
      
      rc_t _apply_ctl_record( proc_t* proc, inst_t* p, unsigned ctl_idx, bool exec_play_fl, bool play_now_fl )
      {
        rc_t         rc                = kOkRC;
        const ctl_t* ctl               = nullptr;
        unsigned     play_cmd_idx      = kInvalidIdx;
        unsigned     active_sf_cmd_idx = kInvalidIdx;
        
        if( ctl_idx >= p->ctlN )
        {
          rc = proc_error(proc,kInvalidArgRC,"The requested ctl index %i is out of range %i.",ctl_idx,p->ctlN);
          goto errLabel;
        }

        ctl = p->ctlA + ctl_idx;
        
        for(unsigned i=0; i<ctl->cmdN; ++i)
        {
          switch( ctl->cmdA[i].tid )
          {
            case kSfCmdTId:
              if((rc = _apply_sf_cmd(proc,p,ctl->cmdA[i].u.sf)) != kOkRC )
                goto errLabel;
              
              if( ctl->cmdA[i].u.sf.sf_id == ctl->active_sf_id )
                active_sf_cmd_idx = i;
              break;
              
            case kPlayCmdTId:
              if( exec_play_fl )
              {
                if((rc = _apply_plyr_cmd(proc,p,ctl->cmdA[i].u.plyr,play_now_fl)) != kOkRC )
                  goto errLabel;
              }
              play_cmd_idx = i;
              break;
          }
        }

        p->last_ctl_idx = ctl_idx;
        
      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Control execution failed in '%s'.",cwStringNullGuard(proc->label));
        else
        {
          if( ctl != nullptr && play_cmd_idx != kInvalidIdx && active_sf_cmd_idx != kInvalidIdx )
          {
            const plyr_cmd_t& plyr_cmd    = ctl->cmdA[ play_cmd_idx ].u.plyr;
            const char*       piano_label = ctl->active_sf_id == 0 ? "(a)" : (ctl->active_sf_id==1? "(b)" : "(?)");
            const sf_cmd_t&   sf_cmd      = ctl->cmdA[ active_sf_cmd_idx].u.sf;
            proc_info(proc,"%s : ACTIVE: seg_id:%i '%s' %s %s-%i ",cwStringNullGuard(proc->label),ctl->seg_id, cwStringNullGuard(plyr_cmd.seg_label), piano_label, cwStringNullGuard(plyr_cmd.person_label), plyr_cmd.person_seg_num );
            proc_info(proc,"beg:%i end:%i ena:%i", sf_cmd.beg_loc, sf_cmd.end_loc, sf_cmd.enable_fl);
          }
        }
        
        return rc;
      }

      rc_t _on_rt_loc( proc_t* proc, inst_t* p, unsigned loc_id )
      {
        rc_t       rc           = kOkRC;
        const bool exec_play_fl = true;
        const bool play_now_fl  = false;

        // if the incoming location is valid and not the same as the last one
        if( loc_id != kInvalidId && loc_id != p->last_loc_id )
        {

          // if the loc matches a ctl record loc then execute the 'ctl' record.
          for(unsigned i=0; i<p->ctlN; ++i)
            if( p->ctlA[i].loc_id == loc_id )
            {
              rc = _apply_ctl_record(proc,p,i, exec_play_fl, play_now_fl );
              break;
            }

          var_set(proc,kLocOutPId,kAnyChIdx,loc_id);

          p->last_loc_id = loc_id;
        }
        
        return rc;
      }

      rc_t _exec_seg( proc_t* proc, inst_t* p, unsigned seg_id, bool exec_play_fl, bool play_now_fl )
      {
        for(unsigned i=0; i<p->ctlN; ++i)
          if( p->ctlA[i].seg_id == seg_id )
            return _apply_ctl_record(proc,p,i, exec_play_fl, play_now_fl );
        
        return proc_error(proc,kInvalidArgRC,"The segment id '%i' was not found.",seg_id);
      }
      
      rc_t _on_goto_seg( proc_t* proc, inst_t* p )
      {
        rc_t       rc           = kOkRC;
        unsigned   seg_id       = kInvalidId;
        const bool exec_play_fl = false;
        const bool play_now_fl  = false;
        
        if((rc = var_get(proc,kGotoSegPId,kAnyChIdx,seg_id)) != kOkRC )
        {
          goto errLabel;
        }

        rc = _exec_seg(proc,p,seg_id,exec_play_fl,play_now_fl);

        var_set(proc,kSimResetPId,kAnyChIdx,true);
        var_set(proc,kSprResetPId,kAnyChIdx,true);
        var_set(proc,kResetPId,kAnyChIdx,true);
        
        proc_info(proc,"%s GOTO: seg:%i",cwStringNullGuard(proc->label),seg_id);

      errLabel:
        return rc;        
      }

      rc_t _on_play_now( proc_t* proc, inst_t* p )
      {
        rc_t       rc           = kOkRC;
        unsigned   seg_id       = kInvalidId;
        const bool exec_play_fl = true;
        const bool play_now_fl  = true;
        
        if((rc = var_get(proc,kPlayNowPId,kAnyChIdx,seg_id)) != kOkRC )
        {
          goto errLabel;
        }

        rc = _exec_seg(proc,p,seg_id,exec_play_fl,play_now_fl);

        proc_info(proc,"%s PLAY NOW: seg:%i",cwStringNullGuard(proc->label),seg_id);
        
      errLabel:
        return rc;        
      }

      // Advance to the next segment where the currently active SF is not active.
      rc_t _on_recover( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        const bool exec_play_fl = false;
        const bool play_now_fl  = false;
        unsigned cur_active_sf_id = kInvalidId;

        proc_info(proc,"RECOVER begin");

        if( p->last_ctl_idx == kInvalidIdx || p->last_ctl_idx >= p->ctlN )
        {
          rc = proc_error(proc,kInvalidStateRC,"The 'last-ctl-idx' is not set and therefore no recovery is possible.");
          goto errLabel;
        }

        cur_active_sf_id = p->ctlA[ p->last_ctl_idx ].active_sf_id;
        
        // Execute the next segment where the currently active SF is not active.        
        for(unsigned i=p->last_ctl_idx+1; i<p->ctlN; ++i)
          if( p->ctlA[i].active_sf_id != cur_active_sf_id )
          {
            if((rc = _exec_seg(proc,p,p->ctlA[i].seg_id,exec_play_fl,play_now_fl)) != kOkRC )
              goto errLabel;
            break;
          }

        // At this point we have advanced to the next segment.

        // Disable the SF that was originaly active, but presumably not tracking.
        // It will be reactivated when the other SF finishes the next tracked segment.
        switch( cur_active_sf_id )
        {
          case kSfAId:
            var_set(proc,kEnableSfAPId,kAnyChIdx,false);
            break;
            
          case kSfBId:
            var_set(proc,kEnableSfBPId,kAnyChIdx,false);
            break;
            
          default:
            rc = proc_error(proc,kInvalidStateRC,"An invalid SF id (%i) was encountered during recovery.",cur_active_sf_id);
            goto errLabel;
        }
        
        proc_info(proc,"%s RECOVER: seg:%i",cwStringNullGuard(proc->label),p->ctlA[ p->last_ctl_idx].seg_id );

      errLabel:
        return rc;
      }

      rc_t _on_reset( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        p->last_ctl_idx = kInvalidIdx;
        p->last_loc_id  = kInvalidId;

        // reset the SF's
        _on_goto_seg(proc,p);

        return rc;
      }
      
      
      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;

        if( proc->ctx->isInRuntimeFl)
        {
          switch( var->vid )
          {              
            case kGotoSegPId:
              _on_goto_seg(proc,p);
              break;
              
            case kPlayNowPId:
              _on_play_now(proc,p);
              break;
              
            case kRecoverPId:
              _on_recover(proc,p);
              break;

            case kResetPId:
              _on_reset(proc,p);
          }
        }
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;

        const rbuf_t* i_rbuf;

        if( var_get(proc,kSfLocPId,kAnyChIdx,i_rbuf) != kOkRC )
          goto errLabel;

        
        for(unsigned i=0; i<i_rbuf->recdN; ++i)
        {
          unsigned loc_id = kInvalidId;
          
          // get the loc field
          if((rc = recd_get( i_rbuf->type, i_rbuf->recdA + i, p->sf_loc_fld_idx, loc_id)) != kOkRC )
          {
            goto errLabel;
          }

          
          _on_rt_loc(proc,p,loc_id);
          
        }
        
      errLabel:
        
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .notify  = std_notify<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    // gutim_pgm_ctl

    
    //------------------------------------------------------------------------------------------------------------------
    //
    // demo_202602_ctl
    //
    namespace demo_202602_ctl
    {
      enum {
        kCfgFnamePId,
        kSfLocPId,

        k01MpPlayerPId,
        k02MpPlayerPId,
        k03MpPlayerPId,
        k04MpPlayerPId,
        k05MpPlayerPId,
        k06MpPlayerPId,
        k07MpPlayerPId,
        k08MpPlayerPId,
        k09MpPlayerPId,
        k10MpPlayerPId,
        k11MpPlayerPId,
        k12MpPlayerPId,
        k13MpPlayerPId,
        k14MpPlayerPId,
        k15MpPlayerPId,
        k16MpPlayerPId,
        k17MpPlayerPId,
        k18MpPlayerPId,

        kBegSegIdxPId,
        kEndSegIdxPId,
        
        kResetPId,
        kStartPId,
        
        kSfBegLocPId,
        kSfEndLocPId,
        kSfResetFlPId,

        kMpStartPlayIdPId,
        kMpPlayIdPId,
        kMpResetFlPId,
        kMpClearFlPId,
        
      };

      enum seg_type_t {
        kInvalidSegTypeId,
        kGutimSegTypeId,
        kScriabinSegTypeId,
        kSpirioSegTypeId
      };

      enum {
        kVidToSegMapN=18
      };

      struct mp_player_t {
        char*    title;
        unsigned mp_player_id;
      };

      struct seg_t
      {
        unsigned   seg_idx;      // the segment index (and the index of this record in segA[])
        seg_type_t type_id;      // segment type id
        char*      title;        // UI title of this segment        
        unsigned   beg_loc;      // begin score location for this segment
        unsigned   end_loc;      // end score location for this segment
        
        unsigned     mp_playerN;
        mp_player_t* mp_playerA;

        list_t*     ui_list;
        
        unsigned   cur_mp_player_id; // current multi-player player_id       
      };
      
      typedef struct
      {
        unsigned segN; 
        seg_t*   segA;                          // segA[segN] holds the multi-player player id for each segment
        
        unsigned vidToSegMapA[ kVidToSegMapN ]; // map k??MpPlayerPId to an associated segment index
        
        list_t* beg_seg_list;                  // the UI list used by kBeg/EndSegIdxPId
        list_t* end_seg_list;                  //
        
        unsigned cur_seg_idx;                   // index into segA[] of the currently playing segment
        unsigned end_seg_idx;

        unsigned loc_field_idx;
      } inst_t;


      rc_t _parse_seg_mp_player_id_list( proc_t* proc, seg_t* seg, const object_t* menuD, const char* seg_title )
      {
        rc_t            rc   = kOkRC;
        const object_t* pair = nullptr;

        // verify that the syntax of the player label/id list
        if( !menuD->is_dict() )
        {
          rc = proc_error(proc,kSyntaxErrorRC,"The player list is not a dictionary.");
          goto errLabel;
        }

        // the list should never be empty
        if( menuD->child_count() == 0 )
        {
          rc = proc_error(proc,kSyntaxErrorRC,"The player list must have at least player.");
          goto errLabel;
        }

        // allocate the object to hold the list
        seg->mp_playerN = menuD->child_count();
        seg->mp_playerA = mem::allocZ<mp_player_t>(seg->mp_playerN);

        // If this segment has multiple players then create a UI list to display the possible players
        if( seg->mp_playerN > 1 )
        {
          if((rc = list_create(seg->ui_list, seg->mp_playerN )) != kOkRC )
          {
            proc_error(proc,rc,"UI list create failed.");
            goto errLabel;
          }
        }

        // for each player assigned to this segment
        for(unsigned i=0; (pair = menuD->next_child_ele(pair)) != nullptr; ++i)
        {
          const char* title;

          // validate the label/id syntax
          if( !pair->is_pair() || pair->pair_label()==nullptr || pair->pair_value()==nullptr)
          {
            rc = proc_error(proc,kSyntaxErrorRC,"The player list item at index '%i' syntax is invalid.",i);
            goto errLabel;
          }

          // parse the player id
          if((rc = pair->pair_value()->value( seg->mp_playerA[i].mp_player_id )) != kOkRC )
          {
            rc = proc_error(proc,kSyntaxErrorRC,"The player id for item '%s' at index '%i' is could not be parsed.",pair->pair_label(),i);
            goto errLabel;
          }

          // add the label/player id to the UI list
          if( seg->ui_list != nullptr )
            if((rc = list_append(seg->ui_list, pair->pair_label(), seg->mp_playerA[i].mp_player_id )) != kOkRC )
            {
              rc = proc_error(proc,rc,"UI list append failed.");
              goto errLabel;
            }

          // store the title
          seg->mp_playerA[i].title = mem::duplStr(pair->pair_label());

          //printf("%s : %i\n",seg->mp_playerA[i].title,seg->mp_playerA[i].mp_player_id);
        }

      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Parsing failed on the player list for segment '%s'.",cwStringNullGuard(seg_title));
        
        return rc;
      }
      
      rc_t _parse_cfg( proc_t* proc, inst_t* p, const char* fn )
      {
        rc_t                rc          = kOkRC;
        object_t*           cfg         = nullptr;
        const object_t*     segL        = nullptr;
        const id_label_pair_tpl<seg_type_t> typeArray[] = { {kGutimSegTypeId,"gutim"}, {kScriabinSegTypeId,"scriabin"}, {kSpirioSegTypeId,"spirio"}, {kInvalidSegTypeId,""} };
        unsigned            vid_map_idx = 0;
        variable_t*         var         = nullptr;

        // parse the cfg file into an object format
        if((rc = objectFromFile( fn, cfg )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Cfg. file parse failed.");
          goto errLabel;
        }

        // get the segment list
        if((rc = cfg->getv("segL", segL )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Cfg. missing 'seg_idL'.");
          goto errLabel;
        }
        
        p->segN = segL->child_count();
        p->segA = mem::allocZ<seg_t>(p->segN);

        // create the starting segment UI list
        if((rc = list_create(p->beg_seg_list, p->segN )) != kOkRC )
          goto errLabel;        

        // create the ending segment UI list
        if((rc = list_create(p->end_seg_list, p->segN )) != kOkRC )
          goto errLabel;
        
        // for each segment
        for(unsigned seg_idx=0; seg_idx<p->segN; ++seg_idx)
        {
          const object_t* menuD          = nullptr;
          const char*     seg_type_label = nullptr;
          const char*     seg_title      = nullptr;
          seg_t*          seg            = p->segA + seg_idx;

          // verfiy that the segment cfg is valid
          if( segL->child_ele(seg_idx) == nullptr )
          {
            rc = proc_error(proc,kOpFailRC,"Cfg. parse failed on segment id at index %i.",seg_idx);
            goto errLabel;
          }

          // parse the segment cfg top level
          if((rc = segL->child_ele(seg_idx)->getv("type_id", seg_type_label,
                                                  "title",   seg_title,
                                                  "seg_idx", seg->seg_idx,
                                                  "beg_loc", seg->beg_loc,
                                                  "end_loc", seg->end_loc,
                                                  "menuD",   menuD )) != kOkRC )
          {
            rc = proc_error(proc,rc,"Cfg. parse failed on segment info at index %i.",seg_idx);
            goto errLabel;            
          }


          // validate the segment label
          if(textLength(seg_title) == 0 )
          {
            rc = proc_error(proc,kSyntaxErrorRC,"The segment label at index %i is blank.",seg_idx);
            goto errLabel;
          }
          
          // there must be exactly one segment cfg for each cfg. record and they must be ordered sequentially
          if( seg->seg_idx != seg_idx )
          {
            rc = proc_error(proc,kInvalidStateRC,"The segment index %i and the stored record index %i do not match.",seg->seg_idx,seg_idx);
            goto errLabel;
          }

          // validate the score location values
          if( seg->beg_loc >= seg->end_loc )
          {
            rc = proc_error(proc,kInvalidArgRC,"The begin (%i) and end (%i) score locations are not valid on segment '%s'.",seg->beg_loc,seg->end_loc,cwStringNullGuard(seg_title));
            goto errLabel;
          }

          // get the type (gutim,scriabin,spirio) of this segment
          if((seg->type_id = label_to_id(typeArray,seg_type_label,kInvalidSegTypeId)) == kInvalidSegTypeId )
          {
            rc = proc_error(proc,kSyntaxErrorRC,"The segment type '%s' is not valid on segment '%s'.",cwStringNullGuard(seg_type_label),cwStringNullGuard(seg_title));
            goto errLabel;
          }

          // get the mp player id's for this segment
          if((rc = _parse_seg_mp_player_id_list( proc, seg, menuD, seg_title )) != kOkRC )
          {
            goto errLabel;
          }

          // validate the mp player array for this segment
          if( seg->mp_playerA == nullptr || seg->mp_playerN == 0 )
          {
            rc = proc_error(proc,kSyntaxErrorRC,"The segment '%s' does not name any players.",cwStringNullGuard(seg_title));
            goto errLabel;
          }

          // if this segment  has only one player it must be a 'scriabin' or 'spirio' segment
          if( seg->mp_playerN == 1 )
          {
            if( seg->type_id != kScriabinSegTypeId && seg->type_id != kSpirioSegTypeId )
            {
              rc = proc_error(proc,kSyntaxErrorRC,"Invalid segment '%s': Only 'spirio' and 'scriabin' segments should have exactly one player.",cwStringNullGuard(seg_title));
              goto errLabel;
            }
          }
          else
          {
            unsigned vid = k01MpPlayerPId + vid_map_idx;
            
            // if this segment has multiple players it must be a 'gutim' segment
            if( seg->type_id != kGutimSegTypeId )
            {
              rc = proc_error(proc,kSyntaxErrorRC,"Invalid segment '%s': Only 'gutim' segments can have multiple players.",cwStringNullGuard(seg_title));
              goto errLabel;
            }

            // validate the vid map idx
            if( vid_map_idx >= kVidToSegMapN )
            {
              rc = proc_error(proc,kInvalidStateRC,"The count of segments with multiple possible players cannot exceed '%i'.",kVidToSegMapN);
              goto errLabel;
            }

            // find the var player seg variable
            if((rc = var_find(proc, vid, kAnyChIdx, var )) != kOkRC )
            {
              rc = proc_error(proc,rc,"The segment player variable could not be found on segment '%s'.",cwStringNullGuard(seg_title));
              goto errLabel;
            }

            if((rc = var_set(proc, vid, kAnyChIdx, seg->mp_playerA[0].mp_player_id)) != kOkRC )
            {
              rc = proc_error(proc,rc,"The player selection menu could not be set on segment '%s'.",cwStringNullGuard(seg_title));
              goto errLabel;
              
            }

            // tell the UI about the list
            var->value_list = seg->ui_list;
            

            // map the input menu variable to this segment
            p->vidToSegMapA[ vid_map_idx++ ] = seg_idx;

            
          }

          // add this segment to the UI segment list
          if((rc = list_append( p->beg_seg_list, seg_title, seg_idx )) != kOkRC )
          {
            goto errLabel;
          }

          if((rc = list_append( p->end_seg_list, seg_title, seg_idx )) != kOkRC )
          {
            goto errLabel;
          }
          
          seg->title             = mem::duplStr(seg_title);
          seg->cur_mp_player_id  = seg->mp_playerA[0].mp_player_id;
          //printf("CUR: %s seg_idx:%i player_id:%i\n",seg->title,seg_idx,seg->cur_mp_player_id);
          
        }

        // find the 'beg_seg_idx' variable
        if((rc = var_find(proc, kBegSegIdxPId, kAnyChIdx, var )) != kOkRC )
        {
          rc = proc_error(proc,rc,"The 'beg_seg_idx' variable could not be found.");
          goto errLabel;
        }

        // tell the UI about the list
        var->value_list = p->beg_seg_list;


        // find the 'end_seg_idx' variable
        if((rc = var_find(proc, kEndSegIdxPId, kAnyChIdx, var )) != kOkRC )
        {
          rc = proc_error(proc,rc,"The 'end_seg_idx' variable could not be found.");
          goto errLabel;
        }

        var->value_list = p->end_seg_list;

        if((rc = var_set(var,p->segN-1)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Failed to set the end segment index to the last segment.");
          goto errLabel;
        }


        
      errLabel:
        if(rc != kOkRC )
          rc = proc_error(proc,rc,"Configuration from '%s' failed.",cwStringNullGuard(fn));
        
        return rc;
      }

      rc_t _pre_play_setup( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        
        // get the starting segment index and store it in p->cur_seg_idx
        if((rc = var_get(proc,kBegSegIdxPId,p->cur_seg_idx)) != kOkRC || p->cur_seg_idx >= p->segN )
        {
          rc = proc_error(proc,rc,"Error getting the starting segment index.");
          goto errLabel;
        }

        // validate the starting segment index
        if( p->cur_seg_idx >= p->segN )
        {
          rc = proc_error(proc,kInvalidArgRC,"The seg index %i is out of range of %i.",p->cur_seg_idx,p->segN);
          goto errLabel;
        }

        // get the ending segment index and store it in p->cur_seg_idx
        if((rc = var_get(proc,kEndSegIdxPId,p->end_seg_idx)) != kOkRC || p->cur_seg_idx >= p->segN )
        {
          rc = proc_error(proc,rc,"Error getting the starting segment index.");
          goto errLabel;
        }

        // validate the ending segment index
        if( p->end_seg_idx >= p->segN )
        {
          rc = proc_error(proc,kInvalidArgRC,"The seg index %i is out of range of %i.",p->cur_seg_idx,p->segN);
          goto errLabel;
        }

        if( p->end_seg_idx < p->cur_seg_idx )
        {
          rc = proc_error(proc,kInvalidStateRC,"The 'end-segment' %i is before the begin segment '%i'. Playback is not possible.",p->end_seg_idx,p->cur_seg_idx);
          goto errLabel;
        }
        
        // set the SF begin location
        if((rc = var_set(proc,kSfBegLocPId,p->segA[p->cur_seg_idx].beg_loc)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Error setting 'beg-loc'.");
          goto errLabel;
        }

        // set the SF end location
        if((rc = var_set(proc,kSfEndLocPId,p->segA[p->end_seg_idx].end_loc)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Error setting 'end-loc'.");
          goto errLabel;
        }

        // reset the score follower
        if((rc = var_set(proc,kSfResetFlPId,true)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Error setting 'end-loc'.");
          goto errLabel;
        }

      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Pre-play setup failed.");

        return rc;
      }

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t          rc              = kOkRC;        
        const char*   cfg_fname       = nullptr;
        const rbuf_t* rbuf            = nullptr;

        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kCfgFnamePId, "cfg_fname",kBaseSfxId, cfg_fname,
                                      kSfLocPId,    "sf_loc",   kBaseSfxId, rbuf)) != kOkRC )
        {
           goto errLabel;
        }

        if((rc = var_register(proc,kAnyChIdx,
                             
                              k01MpPlayerPId, "gutim_01_plyr_id", kBaseSfxId,
                              k02MpPlayerPId, "gutim_02_plyr_id", kBaseSfxId,
                              k03MpPlayerPId, "gutim_03_plyr_id", kBaseSfxId,
                              k04MpPlayerPId, "gutim_04_plyr_id", kBaseSfxId,
                              k05MpPlayerPId, "gutim_05_plyr_id", kBaseSfxId,
                              k06MpPlayerPId, "gutim_06_plyr_id", kBaseSfxId,
                              k07MpPlayerPId, "gutim_07_plyr_id", kBaseSfxId,
                              k08MpPlayerPId, "gutim_08_plyr_id", kBaseSfxId,
                              k09MpPlayerPId, "gutim_09_plyr_id", kBaseSfxId,
                              k10MpPlayerPId, "gutim_10_plyr_id", kBaseSfxId,
                              k11MpPlayerPId, "gutim_11_plyr_id", kBaseSfxId,
                              k12MpPlayerPId, "gutim_12_plyr_id", kBaseSfxId,
                              k13MpPlayerPId, "gutim_13_plyr_id", kBaseSfxId,
                              k14MpPlayerPId, "gutim_14_plyr_id", kBaseSfxId,
                              k15MpPlayerPId, "gutim_15_plyr_id", kBaseSfxId,
                              k16MpPlayerPId, "gutim_16_plyr_id", kBaseSfxId,
                              k17MpPlayerPId, "gutim_17_plyr_id", kBaseSfxId,
                              k18MpPlayerPId, "gutim_18_plyr_id", kBaseSfxId,

                              kBegSegIdxPId,  "beg_seg_idx",      kBaseSfxId,
                              kEndSegIdxPId,  "end_seg_idx",      kBaseSfxId,
                              
                              kResetPId,      "reset",            kBaseSfxId,
                              kStartPId,      "start",            kBaseSfxId,
                              
                              kSfBegLocPId,  "sf_beg_loc",   kBaseSfxId,
                              kSfEndLocPId,  "sf_end_loc",   kBaseSfxId,
                              kSfResetFlPId, "sf_reset_fl",  kBaseSfxId,

                              kMpStartPlayIdPId, "mp_start_play_id", kBaseSfxId, 
                              kMpPlayIdPId,      "mp_play_id",       kBaseSfxId,
                              kMpResetFlPId,     "mp_reset_fl",      kBaseSfxId,
                              kMpClearFlPId,     "mp_clear_fl",      kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = _parse_cfg( proc, p, cfg_fname )) != kOkRC )
        {
          goto errLabel;
        }
        

        if( p->segN > 0 && p->segA[0].mp_playerN > 0 )
        {
          // set the starting segment id to the first player on the first segment.
          if((rc = var_set( proc, kMpStartPlayIdPId, kAnyChIdx, p->segA[0].mp_playerA[0].mp_player_id )) != kOkRC )
          {
            rc = proc_error(proc,rc,"Error setting the initial starting segment player id'.");
            goto errLabel;
          }
        }

        if(rbuf==nullptr || (p->loc_field_idx  = recd_type_field_index( rbuf->type, "loc")) == kInvalidIdx )
        {
          rc = proc_error(proc,rc,"Unable to locate the 'loc' field index on 'sf_loc'.");
          goto errLabel;
        }

        if((rc = _pre_play_setup( proc, p )) != kOkRC )
          goto errLabel;

      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        for(unsigned i=0; i<p->segN; ++i)
        {
          for(unsigned j=0; j<p->segA[i].mp_playerN; ++j)
          {
            mem::release(p->segA[i].mp_playerA[j].title);
          }
          
          mem::release(p->segA[i].mp_playerA);
          mem::release(p->segA[i].title);
          list_destroy(p->segA[i].ui_list);
        }

        mem::release(p->segA);
        list_destroy(p->beg_seg_list);
        list_destroy(p->end_seg_list);
        
        return rc;
      }

      rc_t _on_mp_player_menu_select( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t     rc       = kOkRC;
        unsigned in_idx   = var->vid - k01MpPlayerPId;
        unsigned seg_idx  = kInvalidIdx;
        unsigned player_id = kInvalidId;
        
        if( in_idx >= kVidToSegMapN )
        {
          rc = proc_error(proc,kInvalidArgRC,"The input vid %i is out of range:%i.",in_idx,kVidToSegMapN);
          goto errLabel;
        }

        // map the variable vid to it's associated segment record
        seg_idx = p->vidToSegMapA[ in_idx ];

        if( seg_idx == kInvalidIdx || seg_idx >= p->segN )
        {
          rc = proc_error(proc,kInvalidArgRC,"The seg. index %i is out of range:%i.",seg_idx,p->segN);
          goto errLabel;
        }

        
        // get the index of the selected menu element
        if((rc = var_get(var,player_id)) != kOkRC )
        {
          goto errLabel;
        }
        
        // update the mp_player_id to use with this segment
        p->segA[ seg_idx ].cur_mp_player_id = player_id; //p->segA[ seg_idx ].mp_playerA[ player_id ].mp_player_id;
        
        printf("PLYR_MENU_SEL:%s %i %i\n",p->segA[ seg_idx ].title, player_id, p->segA[ seg_idx ].cur_mp_player_id );
        
      errLabel:
        if(rc != kOkRC )
          proc_error(proc,rc,"MP player select failed.");
        
        return rc;
      }

      rc_t _on_set_start_player_id( proc_t* proc, inst_t* p, const variable_t* beg_seg_idx_var )
      {
        rc_t     rc      = kOkRC;
        unsigned seg_idx = kInvalidIdx;

        // get the new starting segment index (note that the menu selection index is the same as the selected segment index)
        if((rc = var_get(beg_seg_idx_var,seg_idx)) != kOkRC )
          goto errLabel;

        // validate the segment index
        if( seg_idx >= p->segN )
        {
          rc = proc_error(proc,kInvalidArgRC,"An invalid segment index '%i' was encountered from the 'beg_seg_idx' variable.");
          goto errLabel;
        }

        // set the starting player id
        if((rc = var_set(proc, kMpStartPlayIdPId, p->segA[ seg_idx ].cur_mp_player_id)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Starting player id update failed.");
          goto errLabel;
        }

        printf("START-PLYR: seg_idx:%i player_id:%i\n",seg_idx,p->segA[ seg_idx ].cur_mp_player_id);
        
      errLabel:
        return rc;
      }

      rc_t _on_set_end_seg_idx(proc_t* proc,inst_t* p,variable_t* end_seg_var)
      {
        rc_t rc = kOkRC;
        
        unsigned beg_seg_idx = kInvalidIdx;
        unsigned end_seg_idx = kInvalidIdx;

        if((rc = var_get(end_seg_var,end_seg_idx)) != kOkRC )
          goto errLabel;

        if((rc = var_get(proc,kEndSegIdxPId,kAnyChIdx,beg_seg_idx)) != kOkRC )
          goto errLabel;

        if( end_seg_idx < beg_seg_idx )
          cwLogWarning("The 'end segment' is before the begin segment. This will prevent playback.");

      errLabel:
        return rc;
      }
      
      rc_t _on_reset( proc_t* proc )
      {
        rc_t rc = kOkRC;
        
        if((rc = var_set(proc,kSfResetFlPId,true)) != kOkRC )
        {
          rc = proc_error(proc,rc,"SF reset output failed.");
          goto errLabel;
        }
        
        if((rc = var_set(proc,kMpResetFlPId,true)) != kOkRC )
        {
          rc = proc_error(proc,rc,"MP player reset output failed.");
          goto errLabel;
        }

      errLabel:
        return rc;
      }

      rc_t _on_start( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        if((rc = _pre_play_setup(proc,p)) != kOkRC )
        {
          goto errLabel;
        }
        
        // tell the MP player to start playing the current player on the current segment.
        if((rc = var_set(proc,kMpPlayIdPId,p->segA[p->cur_seg_idx].cur_mp_player_id)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Error setting the MP player 'play' id.");
          goto errLabel;
        }

        proc_info(proc,"Start - seg:%i player:%i",p->cur_seg_idx,p->segA[p->cur_seg_idx].cur_mp_player_id);

      errLabel:
        return rc;
      }

      rc_t _on_sf_loc( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t          rc   = kOkRC;
        const rbuf_t* rbuf = nullptr;

        // get the incoming score-follwed MIDI record
        if((rc = var_get(var,rbuf)) != kOkRC )
        {
          goto errLabel;
        }

        // for each incoming record
        for(unsigned i=0; i<rbuf->recdN; ++i)
        {
          unsigned loc_id = kInvalidId;

          // get the loc field
          if((rc = recd_get( rbuf->type, rbuf->recdA + i, p->loc_field_idx, loc_id)) != kOkRC )
          {
            goto errLabel;
          }

          if( loc_id != kInvalidId )
          {

            // if the location is past the end of the current segment ...
            while( p->cur_seg_idx < p->segN && loc_id >= p->segA[p->cur_seg_idx].end_loc )
            {
              // ... advance the current segment
              p->cur_seg_idx += 1;
              
              // if there is a next segment ....
              if( p->cur_seg_idx < p->segN )
              {
                proc_info(proc,"Starting next segment:%i player:%i",p->cur_seg_idx,p->segA[p->cur_seg_idx].cur_mp_player_id);
                
                // ... then start it
                if((rc = var_set(proc, kMpPlayIdPId, p->segA[p->cur_seg_idx].cur_mp_player_id )) != kOkRC )
                  goto errLabel;
              }
            }
          }
        }
        
      errLabel:
        return rc;
      }

      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;

        switch( var->vid )
        {
          case kSfLocPId:
            _on_sf_loc(proc,p,var);
            break;
            
          case k01MpPlayerPId:
          case k02MpPlayerPId:
          case k03MpPlayerPId:
          case k04MpPlayerPId:
          case k05MpPlayerPId:
          case k06MpPlayerPId:
          case k07MpPlayerPId:
          case k08MpPlayerPId:
          case k09MpPlayerPId:
          case k10MpPlayerPId:
          case k11MpPlayerPId:
          case k12MpPlayerPId:
          case k13MpPlayerPId:
          case k14MpPlayerPId:
          case k15MpPlayerPId:
          case k16MpPlayerPId:
          case k17MpPlayerPId:
          case k18MpPlayerPId:
            _on_mp_player_menu_select(proc,p,var);
            break;

          case kBegSegIdxPId:
            _on_set_start_player_id( proc, p, var );
            break;

          case kEndSegIdxPId:
            _on_set_end_seg_idx(proc,p,var);
            break;
            
          case kResetPId:
            _on_reset(proc);
            break;

          case kStartPId:
            _on_start(proc,p);
            break;
            
        }
        
        
      errLabel:
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;
        
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .notify  = std_notify<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    }    // demo_202602_ctl

    
  } // flow
} //cw
