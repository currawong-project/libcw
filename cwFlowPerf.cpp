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
          rc = cwLogError(kInvalidArgRC,"The score filename is blank.");
          goto errLabel;
        }

        if((fname = proc_expand_filename( proc, score_fname )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The score filename (%s) is invalid.",score_fname);
          goto errLabel;
        }

        cwLogInfo("Opening:%s",fname);
        
        if((rc= perf_score::create( perfScoreH, fname )) != kOkRC )
        {
          rc = cwLogError(rc,"Score create failed on '%s'.",fname);
          goto errLabel;          
        }

        if((p->msgAllocN = perf_score::event_count(perfScoreH)) == 0 )
        {
          rc = cwLogWarning("The score '%s' is empty.",fname);
          goto errLabel;
        }

        if((score_evt = perf_score::base_event(perfScoreH)) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The score '%s' could not be accessed.",fname);
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
                rc = cwLogError(kInvalidStateRC,"The score cannot be loaded because is not in order by location.");
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
                cwLogError(kInvalidStateRC,"Two damper pedal %s msg's without an intervening %s msg. meas:%i", down_fl ? "down" : "up", down_fl ? "up" : "down", m->meas );
              
            }

            if(  midi::isSostenutoPedal( mm->status, mm->d0 ) )
            {
              bool down_fl = pedalStateFlags & kDampPedalDownFl;
              pedalStateFlags = cwEnaFlag(pedalStateFlags, kSostPedalDownFl, midi::isPedalDown( mm->d1 ) );
              if( (pedalStateFlags & kDampPedalDownFl) == down_fl )
                cwLogError(kInvalidStateRC,"Two sostenuto pedal %s msg's without an intervening %s msg. meas:%i", down_fl ? "down" : "up", down_fl ? "up" : "down", m->meas );

            }            
            m->flags = pedalStateFlags;
            
            p->msgN += 1;
          }

        }
        
        
      errLabel:
        if( rc != kOkRC )
          rc = cwLogError(rc,"Score load failed on '%s'.",cwStringNullGuard(fname));
        
        perf_score::destroy(perfScoreH);

        mem::release(fname);

        return rc;
      }


      rc_t _alloc_recd_array( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned chIdx, const recd_type_t* base, recd_array_t*& recd_array_ref  )
      {
        rc_t        rc  = kOkRC;
        variable_t* var = nullptr;
        
        // find the record variable
        if((rc = var_find( proc, var_label, sfx_id, chIdx, var )) != kOkRC )
        {
          rc = cwLogError(rc,"The record variable '%s:%i' could was not found.",cwStringNullGuard(var_label),sfx_id);
          goto errLabel;
        }

        // verify that the variable has a record format
        if( !var_has_recd_format(var) )
        {
          rc = cwLogError(kInvalidArgRC,"The variable does not have a valid record format.");
          goto errLabel;
        }
        else
        {
          recd_fmt_t* recd_fmt = var->varDesc->fmt.recd_fmt;

          // create the recd_array
          if((rc = recd_array_create( recd_array_ref, recd_fmt->recd_type, base, recd_fmt->alloc_cnt )) != kOkRC )
          {
            goto errLabel;
          }
        }
        
      errLabel:
        if( rc != kOkRC )
          rc = cwLogError(rc,"Record array create failed on the variable '%s:%i ch:%i.",cwStringNullGuard(var_label),sfx_id,chIdx);
        
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
          rc = cwLogError(kInvalidArgRC,"Invalid begin %s %i.",label,value);

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

        // allocate the output recd array
        if((rc = _alloc_recd_array( proc, "out", kBaseSfxId, kAnyChIdx, nullptr, p->recd_array  )) != kOkRC )
        {
          goto errLabel;
        }
        
        // create one output record buffer
        rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, p->recd_array->type, nullptr, 0  );

        p->midi_fld_idx = recd_type_field_index( p->recd_array->type, "midi");
        p->loc_fld_idx  = recd_type_field_index( p->recd_array->type, "loc");
        p->meas_fld_idx = recd_type_field_index( p->recd_array->type, "meas");

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
        cwLogInfo("Stopping ...");
        return kOkRC;
      }

      rc_t _set_output_record( inst_t* p, rbuf_t* rbuf, const msg_t* m )
      {
        rc_t rc = kOkRC;
        
        recd_t* r = p->recd_array->recdA + rbuf->recdN;
        
        // if the output record array is full
        if( rbuf->recdN >= p->recd_array->allocRecdN )
        {
          rc = cwLogError(kBufTooSmallRC,"The internal record buffer overflowed. (buf recd count:%i).",p->recd_array->allocRecdN);
          goto errLabel;
        }
        
        recd_set( rbuf->type, nullptr, r, p->midi_fld_idx, m->midi );
        recd_set( rbuf->type, nullptr, r, p->loc_fld_idx,  m->loc  );
        recd_set( rbuf->type, nullptr, r, p->meas_fld_idx, m->meas );
        rbuf->recdN += 1;

      errLabel:
        return rc;
      }

      rc_t _do_stop_now( proc_t* proc, inst_t* p, rbuf_t* rbuf )
      {
        rc_t rc = kOkRC;

        // copy the 'all-note-off','all-ctl-off' msg into output record array
        _set_output_record(p,rbuf,p->midiMsgA + kAllNotesOffMsgIdx);
        _set_output_record(p,rbuf,p->midiMsgA + kResetAllCtlsMsgIdx);
            
        p->state = kIdleStateId;

        // set the 'done' output flag
        var_set(proc, kDoneFlPId, kAnyChIdx, true );

        cwLogInfo("Stopped.");
        
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
        
        var_get( proc, kBMeasPId,  kAnyChIdx, bloc );

        printf("Starting at loc:%i\n",bloc);

        // Rewind the current position to the begin location
        for(i=0; i<p->msgN; ++i)
          if( p->msgA[i].meas >= bloc )
          {
            p->sample_idx = p->msgA[i].sample_idx;
            p->msg_idx    = i;
            p->cur_meas   = p->msgA[i].meas;

            // if the damper pedal is down at the start location
            if( p->msgA[i].flags & kDampPedalDownFl )
              _set_output_record(p,rbuf,p->midiMsgA + kDampPedalDownMsgIdx);

            // if the sostenuto pedal was put down at the start location
            if( p->msgA[i].flags & kSostPedalDownFl )            
              _set_output_record(p,rbuf,p->midiMsgA + kSostPedalDownMsgIdx);
            
            cwLogInfo("New current: msg_idx:%i meas:%i loc:%i %i",p->msg_idx, p->msgA[i].meas, p->msgA[i].loc, bloc );
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
          rc = cwLogError(kInvalidStateRC,"The score player '%s' does not have a validoutput buffer.",proc->label);
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
            _set_output_record(p, rbuf, m );

            if( note_on_fl )
              p->note_cnt += 1;
            
            if( midi::isNoteOff(m->midi->status, m->midi->d1 ) && p->note_cnt > 0)
              p->note_cnt -= 1;
          }
              

          p->msg_idx += 1;

          // track the current measure
          if( m->meas > p->cur_meas )
          {
            cwLogInfo("meas:%i",m->meas);
            p->cur_meas = m->meas;
          }
        } // end-while

        // if the end of the stopping state has been reached or if there are no more msg's in the score
        if( (p->state==kStoppingStateId && (p->note_cnt == 0 || p->sample_idx> p->stopping_sample_idx)) || p->msg_idx >= p->msgN )
        {
          cwLogInfo("End-of-stopping: note_cnt:%i %s %s.",p->note_cnt,p->sample_idx> p->stopping_sample_idx ? "timed-out":"", p->msg_idx>=p->msgN ? "score-done":"");
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
          rc = cwLogError(kInvalidArgRC,"The velocity table filename is blank.");
          goto errLabel;
        }

        if((fname = proc_expand_filename( proc, vel_tbl_fname )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"The velocity table filename (%s) is invalid.",vel_tbl_fname);
          goto errLabel;
        }
        
        if((rc = objectFromFile(fname,cfg)) != kOkRC )
        {
          rc = cwLogError(rc,"Velocity table file parse failed.");
          goto errLabel;
        }

        if((rc = cfg->getv("tables",tblL)) != kOkRC )
        {
          rc = cwLogError(rc,"Velocity table file has no 'tables' field.");
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
            rc = cwLogError(rc,"Velocity table at index %i failed.",i);
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
            rc = cwLogError(rc,"The velocity table named '%s' appears to be blank.",cwStringNullGuard(label));
            continue;
          }

          vt->tblA = mem::allocZ<unsigned>(vt->tblN);

          for(unsigned j=0; j<vt->tblN; ++j)
          {
            const object_t* intCfg;
            
            if((intCfg = velListCfg->child_ele(j)) == nullptr )
            {
              rc = cwLogError(rc,"Access to the integer value at index %i failed on vel. table '%s'.",j,cwStringNullGuard(label));
              goto errLabel;
            }
            
            if((rc = intCfg->value(vt->tblA[j])) != kOkRC )
            {              
              rc = cwLogError(rc,"Parse failed on integer value at index %i in vel. table '%s'.",j,cwStringNullGuard(label));
              goto errLabel;
            }            
          }
          
        }
        

      errLabel:
        if( rc != kOkRC )
          rc = cwLogError(rc,"Score velocity table file load failed on '%s'.",cwStringNullGuard(vel_tbl_fname));          

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

        cwLogWarning("The requested velocity table '%s' was not found on the score instance '%s:%i'.",vel_tbl_label,proc->label, proc->label_sfx_id);
        
        return kOkRC;
      }

      rc_t _alloc_recd_array( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned chIdx, const recd_type_t* base, recd_array_t*& recd_array_ref  )
      {
        rc_t        rc  = kOkRC;
        variable_t* var = nullptr;
        
        // find the record variable
        if((rc = var_find( proc, var_label, sfx_id, chIdx, var )) != kOkRC )
        {
          rc = cwLogError(rc,"The record variable '%s:%i' could was not found.",cwStringNullGuard(var_label),sfx_id);
          goto errLabel;
        }

        // verify that the variable has a record format
        if( !var_has_recd_format(var) )
        {
          rc = cwLogError(kInvalidArgRC,"The variable does not have a valid record format.");
          goto errLabel;
        }
        else
        {
          recd_fmt_t* recd_fmt = var->varDesc->fmt.recd_fmt;

          // create the recd_array
          if((rc = recd_array_create( recd_array_ref, recd_fmt->recd_type, base, recd_fmt->alloc_cnt )) != kOkRC )
          {
            goto errLabel;
          }
        }
        
      errLabel:
        if( rc != kOkRC )
          rc = cwLogError(rc,"Record array create failed on the variable '%s:%i ch:%i.",cwStringNullGuard(var_label),sfx_id,chIdx);
        
        return rc;
        
      }
              
      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t    rc   = kOkRC;        

        const char* vel_tbl_fname = nullptr;
        const char* vel_tbl_label = nullptr;
        const rbuf_t* rbuf = nullptr;
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

        // create the output recd_array using the 'in' record type as the base type
        if((rc = _alloc_recd_array( proc, "out", kBaseSfxId, kAnyChIdx, rbuf->type, p->recd_array  )) != kOkRC )
        {
          goto errLabel;
        }

        // get the record field index for the incoming record
        if((p->i_midi_fld_idx = recd_type_field_index( rbuf->type, "midi")) == kInvalidIdx )
        {
          rc = cwLogError(kInvalidArgRC,"The incoming record does not have a 'midi' field.");
          goto errLabel;                          
        }

        // get the record field index for the outgoing record
        if((p->o_midi_fld_idx = recd_type_field_index( p->recd_array->type, "midi")) == kInvalidIdx )
        {
          rc = cwLogError(kInvalidArgRC,"The outgoing record does not have a 'midi' field.");
          goto errLabel;                          
        }
        
        // create one output record buffer
        rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, p->recd_array->type, nullptr, 0  );

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
            rc = cwLogError(kBufTooSmallRC,"The velocity table MIDI processing buffers overflow (%i).",i);
            goto errLabel;
          }

          // Get pointers to the output records
          recd_t*         o_r = p->recd_array->recdA + i;
          midi::ch_msg_t* o_m = p->midiA + i;

          // get a pointer to the incoming MIDI record
          if((rc = recd_get(i_rbuf->type,i_r,p->i_midi_fld_idx,i_m)) != kOkRC )
          {
            rc = cwLogError(rc,"Record 'midi' field read failed.");
            goto errLabel;
          }

          // copy the incoming MIDI record to the output array
          *o_m = *i_m;

          // if this is a note on
          if( midi::isNoteOn(i_m->status,i_m->d1) )
          {

            // if the 'score_vel' was not given
            if( p->i_score_vel_fld_idx == kInvalidIdx )
            {
              // and the velocity is valid
              if( i_m->d1 >= p->activeVelTbl->tblN )
              {
              rc = cwLogError(kInvalidArgRC,"The pre-mapped velocity value %i is outside of the range (%i) of the velocity table '%s'.",i_m->d1,p->activeVelTbl->tblN,cwStringNullGuard(p->activeVelTbl->label));
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
                rc = cwLogError(kOpFailRC,"'score_velocity access failed in velocity table.");
                goto errLabel;
              }

              // if the score_vel is valid (it won't be if this note was not tracked in the score)
              if( score_vel != (unsigned)-1 )
              {
                // verify that the 'score_vel' is inside the range of the table
                if(score_vel >= p->activeVelTbl->tblN )
                {
                  rc = cwLogError(kInvalidArgRC,"The pre-mapped score velocity value %i is outside of the range (%i) of the velocity table '%s'.",score_vel,p->activeVelTbl->tblN,cwStringNullGuard(p->activeVelTbl->label));
                  goto errLabel;                  
                }

                // apply the score_vel to the map
                o_m->d1 = p->activeVelTbl->tblA[ score_vel ];
              }
              
            }

            //printf("%i %i %s\n",i_m->d1,o_m->d1,p->activeVelTbl->label);
          }

          // update the MIDI pointer in the output record 
          recd_set(o_rbuf->type,i_r,o_r,p->o_midi_fld_idx, o_m );
        }

        //printf("RECDN:%i\n",i_rbuf->recdN);
        
        o_rbuf->recdA = p->recd_array->recdA;
        o_rbuf->recdN = i_rbuf->recdN;        
        
      errLabel:
        if( rc != kOkRC )
          rc = cwLogError(rc,"Vel table exec failed.");
                          
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
          rc = cwLogError(kOpFailRC,"Preset filename expansion failed.");
          goto errLabel;
        }

        // create the cwPresetSel object
        if(cfg==nullptr || (rc = preset_sel::create(p->psH,cfg)) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"The preset select object could not be initialized.");
          goto errLabel;
        }

        // read in the loc->preset map file
        if((rc = preset_sel::read(p->psH,exp_fname)) != kOkRC )
        {
          rc = cwLogError(rc,"The preset_sel data file '%s' could not be read.",cwStringNullGuard(exp_fname));
          goto errLabel;
        }

        // The location is coming from a 'record', get the location field.
        if((p->loc_fld_idx  = recd_type_field_index( rbuf->type, "loc")) == kInvalidIdx )
        {
          rc = cwLogError(kInvalidArgRC,"The 'in' record does not have a 'loc' field.");
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
            rc = cwLogError(rc,"The 'loc' field read failed.");
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
              rc = cwLogError(kInvalidArgRC,"The current frag does not have valid preset associated with it.");
              goto errLabel;
            }

            // validate the preset index
            if( preset_idx >= p->presetN )
            {
              rc = cwLogError(kAssertFailRC,"The selected preset index %i is out of range.",preset_idx);
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
            rc = cwLogError(rc,"The '%s' variable could not be found.",var_labelA[i]);
            goto errLabel;
          }

          var->value_list = p->manual_sel_list;
        }
        
      errLabel:
        if( rc != kOkRC )
          rc = cwLogError(rc,"The 'gutim_ps' manual selection list create failed.");
        return rc;
      }
      
      template< typename T >
      rc_t _read_class_preset_value( proc_t* proc, preset_t* preset, var_cfg_t* var_cfg,  unsigned ch_idx, T& val_ref )
      {
        rc_t rc;
        
        if((rc = class_preset_value( proc->ctx, var_cfg->cls_label, preset->cls_label, var_cfg->cls_var_label, ch_idx, val_ref )) != kOkRC )
        {
          rc = cwLogError(rc,"The preset value could not be accessed for the preset '%s' from '%s:%s' ch:%i.",preset->cls_label,var_cfg->cls_label,var_cfg->cls_var_label,ch_idx);
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
            rc = cwLogError(rc,"The class preset variable list could not be accessed for the preset '%s' from '%s:%s'.",preset->cls_label,var_cfg->cls_label,var_cfg->cls_var_label);
            goto errLabel;          
        }

        if( preset_has_var_fl )
        {
          if((rc = class_preset_value_channel_count( proc->ctx, var_cfg->cls_label, preset->cls_label, var_cfg->cls_var_label, chN )) != kOkRC )
          {
            rc = cwLogError(rc,"The class preset channel count could not be accessed for the preset '%s' from '%s:%s'.",preset->cls_label,var_cfg->cls_label,var_cfg->cls_var_label);
            goto errLabel;
          }

          if( chN > kMaxChN  )
          {          
            rc = cwLogError(rc,"Thethe preset '%s' from '%s:%s' has more channels (%i) than can be processed (%i).",preset->cls_label,var_cfg->cls_label,var_cfg->cls_var_label,chN,kMaxChN);
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
              rc = cwLogError(kInvalidDataTypeRC,"An invalid variable value data type (%i) was encountered.",var_cfg->tid);
          }
        }

        if(rc != kOkRC )
        {
          rc = cwLogError(rc,"The preset value for the variable '%s' for preset '%s' from '%s:%s' could not be accessed.", var_cfg->var_label, preset->cls_label, var_cfg->cls_label, var_cfg->cls_var_label );
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
          rc = cwLogError(rc,"Preset value initialization failed.");
        return rc;        
      }

      rc_t _apply_preset_no_interp(proc_t* proc, inst_t* p, unsigned voice_idx, unsigned preset_idx)
      {
        rc_t rc = kOkRC;

        if( preset_idx == kInvalidIdx || preset_idx >= p->presetN )
        {
          rc = cwLogError(kInvalidArgRC,"The primary preset is invalid.");
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
                rc = cwLogError(kInvalidArgRC,"Unknown preset value type:%i on %s.",v->tid,cwStringNullGuard(var_cfg->var_label));
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
          rc = cwLogError(kInvalidArgRC,"The primary preset is invalid.");
          goto errLabel;
        }
        
        if( sec_preset_idx == kInvalidIdx || sec_preset_idx >= p->presetN )
        {
          rc = cwLogError(kInvalidArgRC,"The secondary preset is invalid.");
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
                rc = cwLogError(kInvalidArgRC,"Unknown preset value type:%i on %s.",var_cfg->tid,cwStringNullGuard(var_cfg->var_label));
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
          rc = cwLogError(kInvalidStateRC,"No current preset has been selected.");
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
          cwLogError(rc,"Preset application failed.");
        return rc;
      }

      rc_t _update_cur_preset_idx( proc_t* proc, inst_t* p, const preset_sel::frag_t* f )
      {
        if( f == nullptr )
          return cwLogError(kInvalidArgRC,"Cannot update current selected preset if no location value has been set.");
        
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

        cwLogInfo("Preset:%s%s%s",_preset_index_to_label(p,p->cur_pri_preset_idx), p->interp_fl ? "->":" ",_preset_index_to_label(p,p->cur_sec_preset_idx));
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
          rc = cwLogError(kInvalidArgRC,"The 'midi_in' must be connected to a 'mult' source with at least one 'mult' instance.");
          goto errLabel;
        }
        
        if((exp_fname = proc_expand_filename(proc,fname)) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"Preset filename expansion failed.");
          goto errLabel;
        }

        // create the cwPresetSel object
        if(cfg==nullptr || (rc = preset_sel::create(p->psH,cfg)) != kOkRC )
        {
          rc = cwLogError(kOpFailRC,"The preset select object could not be initialized.");
          goto errLabel;
        }

        p->dry_preset_idx = preset_sel::dry_preset_index(p->psH);

        // read in the loc->preset map file
        if((rc = preset_sel::read(p->psH,exp_fname)) != kOkRC )
        {
          rc = cwLogError(rc,"The preset_sel data file '%s' could not be read.",cwStringNullGuard(exp_fname));
          goto errLabel;
        }

        //preset_sel::report(p->psH);
        //preset_sel::report_presets(p->psH);
        
        // The location is coming from a 'record', get the location field.
        if((p->loc_fld_idx  = recd_type_field_index( rbuf->type, "loc")) == kInvalidIdx )
        {
          cwLogWarning("The incoming record to the 'gutim_ps' object does not have a 'loc' field. Score tracking is disabled.");
          //rc = cwLogError(kInvalidArgRC,"The 'in' record does not have a 'loc' field.");
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
            rc = cwLogError(kInvalidArgRC,"The 'midi_in' registration failed.");
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
            rc = cwLogError(rc,"The 'loc' field read failed.");
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

            cwLogInfo("ps LOC:%i ",loc);
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
              rc = cwLogError(rc,"Manual primary selected preset index update failed.");
            break;

          case kSecManualSelPId:         
            if((rc = _update_manual_preset_index(p,var,p->cur_manual_sec_preset_idx)) != kOkRC )
              rc = cwLogError(rc,"Manual secondary selected preset index update failed.");
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
          rc = cwLogError(rc,"The input record type on '%s:%i' does not have a 'midi' field.",cwStringNullGuard(proc->label),proc->label_sfx_id);
          goto errLabel;          
        }

        // get the input record 'loc' field index
        if((p->loc_field_idx = recd_type_field_index( in_rbuf->type, "loc")) == kInvalidIdx )
        {
          rc = cwLogError(rc,"The input record type on '%s:%i' does not have a 'loc' field.",cwStringNullGuard(proc->label),proc->label_sfx_id);
          goto errLabel;          
        }

        // parse the dynamics reference array
        if((rc = dyn_ref_tbl::create(p->dynRefH,dyn_tbl_fname)) != kOkRC )
        {
          rc = cwLogError(rc,"The reference dynamics array parse failed.");
          goto errLabel;
        }
        
        // parse the score
        if((rc = create( p->scParseH, score_fname, proc->ctx->sample_rate, p->dynRefH, printParseWarningsFl )) != kOkRC )
        {
          rc = cwLogError(rc,"Score parse failed.");
          goto errLabel;
        }
        
        // create the SF score
        if((rc = create( p->scoreH, p->scParseH, printParseWarningsFl)) != kOkRC )
        {
          rc = cwLogError(rc,"SF Score create failed.");
          goto errLabel;
        }

        args.enableFl = true;
        args.scoreH   = p->scoreH;

        // create the score follower
        if((rc = create( p->sfH, args )) != kOkRC )
        {
          rc = cwLogError(rc,"Score follower create failed.");
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
            rc = cwLogError(rc,"The 'midi' field read failed.");
            goto errLabel;
          }
                    
          if((rc = exec( p->sfH, sec, sample_idx, m->uid, m->status, m->d0,m->d1, match_fl )) != kOkRC )
          {
            rc = cwLogError(rc,"Score follower exec failed.");
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
        kPrintFlPId,
        kOutPId,
      };
      
      typedef struct
      {
        cw::perf_score::handle_t       scoreH;
        cw::score_follow_2::handle_t   sfH;
        unsigned                       i_midi_field_idx;
        unsigned                       o_midi_field_idx;
        unsigned                       loc_field_idx;
        unsigned                       meas_field_idx;
        unsigned                       vel_field_idx;
        recd_array_t*                  recd_array;  // output record array
        unsigned                       cur_loc_id;

      } inst_t;


      rc_t _alloc_recd_array( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned chIdx, const recd_type_t* base, recd_array_t*& recd_array_ref  )
      {
        rc_t        rc  = kOkRC;
        variable_t* var = nullptr;
        
        // find the record variable
        if((rc = var_find( proc, var_label, sfx_id, chIdx, var )) != kOkRC )
        {
          rc = cwLogError(rc,"The record variable '%s:%i' could was not found.",cwStringNullGuard(var_label),sfx_id);
          goto errLabel;
        }

        // verify that the variable has a record format
        if( !var_has_recd_format(var) )
        {
          rc = cwLogError(kInvalidArgRC,"The variable does not have a valid record format.");
          goto errLabel;
        }
        else
        {
          recd_fmt_t* recd_fmt = var->varDesc->fmt.recd_fmt;

          // create the recd_array
          if((rc = recd_array_create( recd_array_ref, recd_fmt->recd_type, base, recd_fmt->alloc_cnt )) != kOkRC )
          {
            goto errLabel;
          }
        }
        
      errLabel:
        if( rc != kOkRC )
          rc = cwLogError(rc,"Record array create failed on the variable '%s:%i ch:%i.",cwStringNullGuard(var_label),sfx_id,chIdx);
        
        return rc;
        
      }
      
      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t                   rc                   = kOkRC;        
        rbuf_t*                in_rbuf              = nullptr;
        const char*            c_score_fname        = nullptr;
        char*                  score_fname          = nullptr;
        unsigned               beg_loc_id = kInvalidId;
        unsigned               end_loc_id = kInvalidId;
        bool                   reset_trig_fl = false;
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
                                      kPrintFlPId,    "print_fl",      kBaseSfxId, sf_args.rpt_fl )) != kOkRC )
        {
          goto errLabel;
        }
        if((score_fname = proc_expand_filename( proc, c_score_fname )) == nullptr )
        {
          rc = cwLogError(kOpFailRC,"Unable to expand the score filename '%s'.",cwStringNullGuard(c_score_fname));
          goto errLabel;
        }

        // create the SF score
        if((rc = create( p->scoreH, score_fname)) != kOkRC )
        {
          rc = cwLogError(rc,"SF Score create failed.");
          goto errLabel;
        }

        // create the score follower
        if((rc = create( p->sfH, sf_args, p->scoreH )) != kOkRC )
        {
          rc = cwLogError(rc,"Score follower create failed.");
          goto errLabel;          
        }

        if((rc = reset( p->sfH, beg_loc_id, end_loc_id )) != kOkRC )
        {
          rc = cwLogError(rc,"Score follower reset failed.");
          goto errLabel;
        }

        // create the output recd_array using the 'in' record type as the base type
        if((rc = _alloc_recd_array( proc, "out", kBaseSfxId, kAnyChIdx, in_rbuf->type, p->recd_array  )) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_register_and_set(proc,kAnyChIdx,
                                      kMaxLocPId,"loc_cnt", kBaseSfxId, max_loc_id(p->sfH) )) != kOkRC )
        {
          goto errLabel;
        }
        
        // create one output record buffer
        if((rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, p->recd_array->type, nullptr, 0  )) != kOkRC )
        {
          goto errLabel;
        }
        
        p->i_midi_field_idx = recd_type_field_index( in_rbuf->type, "midi");
        p->o_midi_field_idx = recd_type_field_index( p->recd_array->type, "midi");
        p->loc_field_idx = recd_type_field_index( p->recd_array->type, "loc");
        p->meas_field_idx = recd_type_field_index(p->recd_array->type, "meas");
        p->vel_field_idx = recd_type_field_index( p->recd_array->type, "score_vel");

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
          rc = cwLogError(rc,"beg_loc read failed.");
          goto errLabel;
        }
        
        if((rc = var_get(proc,kEndLocPId,kAnyChIdx,end_loc_id)) != kOkRC )
        {
          rc = cwLogError(rc,"end_loc read failed.");
          goto errLabel;
        }

        if((rc = reset(p->sfH,beg_loc_id,end_loc_id)) != kOkRC )
        {
          rc = cwLogError(rc,"Score follower reset failed..");
          goto errLabel;          
        }

        p->cur_loc_id = kInvalidId;

        cwLogInfo("SF reset:%i %i",beg_loc_id,end_loc_id);

      errLabel:

        if( rc != kOkRC )
          rc = cwLogError(rc,"SF reset failed on %i %i",beg_loc_id,end_loc_id);
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
        }
        return rc;
      }


      rc_t _set_output_record( inst_t* p, rbuf_t* rbuf, const recd_t* base, unsigned loc_id, unsigned meas_numb, unsigned vel )
      {
        rc_t rc = kOkRC;
        
        recd_t* r = p->recd_array->recdA + rbuf->recdN;
        
        // if the output record array is full
        if( rbuf->recdN >= p->recd_array->allocRecdN )
        {
          rc = cwLogError(kBufTooSmallRC,"The internal record buffer overflowed. (buf recd count:%i).",p->recd_array->allocRecdN);
          goto errLabel;
        }
        
        recd_set( rbuf->type, base, r, p->loc_field_idx,  loc_id  );
        recd_set( rbuf->type, base, r, p->meas_field_idx, meas_numb );
        recd_set( rbuf->type, base, r, p->vel_field_idx,  vel );
        rbuf->recdN += 1;

      errLabel:
        return rc;
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
        
        // for each incoming record
        for(unsigned i=0; i<i_rbuf->recdN; ++i)
        {
          midi::ch_msg_t* m         = nullptr;
          unsigned        loc_id    = kInvalidId;
          unsigned        score_vel = -1;
          unsigned        meas_numb = -1;

          if((rc = recd_get( i_rbuf->type, i_rbuf->recdA+i, p->i_midi_field_idx, m)) != kOkRC )
          {
            rc = cwLogError(rc,"The 'midi' field read failed.");
            goto errLabel;
          }

          if( midi::isNoteOn( m->status, m->d1 ) )
          {
            
            if((rc = on_new_note( p->sfH, m->uid, sec, m->d0, m->d1, loc_id, meas_numb, score_vel )) != kOkRC )
            {
              rc = cwLogError(rc,"Score follower note processing failed.");
              goto errLabel;              
            }

            if( loc_id != kInvalidId )
            {
              if( loc_id != p->cur_loc_id )
              {
                p->cur_loc_id = loc_id;
              
                cwLogInfo("sf LOC:%i",loc_id);
              }
            }
          }

          _set_output_record( p, o_rbuf, i_rbuf->recdA+i, loc_id, meas_numb, score_vel );
          
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

    
  } // flow
} //cw
