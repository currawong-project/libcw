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
        kVelTblFnamePId,
        kVelTblLabelPId,
        kDoneFlPId,
        kOutPId,
        kLocPId
      };

      typedef struct
      {        
        unsigned         sample_idx;
        unsigned         loc;
        unsigned         meas;
        unsigned         d1;   // inital d1 value before velocity mapping was applied
        midi::ch_msg_t*  midi; // index of associated msg in chMsgA
      } msg_t;

      typedef struct vel_tbl_str
      {
        unsigned* tblA;
        unsigned  tblN;
        char*     label;
        struct vel_tbl_str* link;
      } vel_tbl_t;

      typedef struct
      {

        unsigned        msgAllocN;
        unsigned        msgN;
        msg_t*          msgA;    // msgA[ msgN ]
        midi::ch_msg_t* chMsgA;  // chMsgA[ msgN ]

        recd_array_t* recd_array;
        unsigned      midi_fld_idx;
        unsigned      loc_fld_idx;
        unsigned      meas_fld_idx;

        vel_tbl_t* velTblL;      // List of vel. tables.
        vel_tbl_t* activeVelTbl; // Currently active vel. table or null if no vel. tbl is active.
        
        
        unsigned sample_idx;
        unsigned msg_idx;
      } inst_t;

      rc_t _load_score( proc_t* proc, inst_t* p, const char* score_fname )
      {
        rc_t rc = kOkRC;

        perf_score::handle_t       perfScoreH;
        const perf_score::event_t* score_evt   = nullptr;
        char*                      fname       = nullptr;

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

        p->msgA   = mem::allocZ<msg_t>(p->msgAllocN);
        p->chMsgA = mem::allocZ<midi::ch_msg_t>(p->msgAllocN);

        for(; p->msgN<p->msgAllocN && score_evt !=nullptr; score_evt=score_evt->link)
        {
          if( score_evt->status != 0 )
          {
            msg_t*          m  = p->msgA   + p->msgN;
            midi::ch_msg_t* mm = p->chMsgA + p->msgN;
            
            m->sample_idx = (unsigned)(proc->ctx->sample_rate * score_evt->sec);
            m->loc        = score_evt->loc;
            m->meas       = score_evt->meas;           
            m->midi       = mm;
            
            time::fracSecondsToSpec( mm->timeStamp, score_evt->sec );

            mm->devIdx = kInvalidIdx;
            mm->portIdx= kInvalidIdx;
            mm->uid    = score_evt->uid;
            mm->ch     = score_evt->status & 0x0f;
            mm->status = score_evt->status & 0xf0;
            mm->d0     = score_evt->d0;
            mm->d1     = score_evt->d1;
            m->d1      = score_evt->d1;
            
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
          
          const char* label = nullptr;

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

      rc_t _apply_vel_table( inst_t* p )
      {
        rc_t rc = kOkRC;

        if( p->activeVelTbl == nullptr )
        {
          cwLogWarning("A velocity table has not been selected.");
          goto errLabel;
        }
        
        for(unsigned i=0; i<p->msgN; ++i)
        {
          midi::ch_msg_t* m = p->msgA[i].midi;
          
          if( midi::isNoteOn(m->status,m->d1) )
          {
            if( p->msgA[i].d1 >= p->activeVelTbl->tblN )
            {
              rc = cwLogError(kInvalidArgRC,"The pre-mapped velocity value %i is outside of the range (%i) of the velocity table '%s'.",p->msgA[i].d1,p->activeVelTbl->tblN,cwStringNullGuard(p->activeVelTbl->label));
              goto errLabel;
            }
            
            m->d1 = p->activeVelTbl->tblA[ p->msgA[i].d1 ];
          }
        }
        
      errLabel:
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

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t    rc   = kOkRC;        
        const char* score_fname = nullptr;
        const char* vel_tbl_fname = nullptr;
        const char* vel_tbl_label = nullptr;
        
        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kScoreFNamePId,    "fname",         kBaseSfxId, score_fname,
                                      kVelTblFnamePId,   "vel_tbl_fname", kBaseSfxId, vel_tbl_fname,
                                      kVelTblLabelPId,   "vel_tbl_label", kBaseSfxId, vel_tbl_label)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_register(proc,kAnyChIdx,
                              kLocPId,"loc", kBaseSfxId,
                              kDoneFlPId,"done_fl", kBaseSfxId )) != kOkRC )
        {
          goto errLabel;
        }

        // load the score
        if((rc = _load_score( proc, p, score_fname )) != kOkRC )
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

        // apply the selected velocity table
        if( p->activeVelTbl != nullptr )
        {
          if((rc = _apply_vel_table( p )) != kOkRC  )
          {
            goto errLabel;
          }
        }

        // allocate the output recd array
        if((rc = _alloc_recd_array( proc, "out", kBaseSfxId, kAnyChIdx, nullptr, p->recd_array  )) != kOkRC )
        {
          goto errLabel;
        }
        
        // create one output MIDI buffer
        rc = var_register_and_set( proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, p->recd_array->type, nullptr, 0  );

        p->midi_fld_idx = recd_type_field_index( p->recd_array->type, "midi");
        p->loc_fld_idx  = recd_type_field_index( p->recd_array->type, "loc");
        p->meas_fld_idx = recd_type_field_index( p->recd_array->type, "meas");


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

        recd_array_destroy(p->recd_array);
        mem::release(p->msgA);
        mem::release(p->chMsgA);
        return rc;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t    rc      = kOkRC;
        rbuf_t* rbuf    = nullptr;
        bool    done_fl = false;

        p->sample_idx += proc->ctx->framesPerCycle;

        // get the output variable
        if((rc = var_get(proc,kOutPId,kAnyChIdx,rbuf)) != kOkRC )
          rc = cwLogError(kInvalidStateRC,"The MIDI file instance '%s' does not have a valid MIDI output buffer.",proc->label);
        else
        {
          rbuf->recdA = nullptr;
          rbuf->recdN = 0;
                    
          while( p->msg_idx < p->msgN && p->sample_idx >= p->msgA[p->msg_idx].sample_idx  )
          {
            recd_t* r = p->recd_array->recdA + rbuf->recdN;
            msg_t*  m = p->msgA + p->msg_idx;

            if( rbuf->recdA == nullptr )
              rbuf->recdA = r;

            if( rbuf->recdN >= p->recd_array->allocRecdN )
            {
              rc = cwLogError(kBufTooSmallRC,"The internal record buffer overflowed. (buf recd count:%i).",p->recd_array->allocRecdN);
              goto errLabel;
            }
            
            recd_set( rbuf->type, nullptr, r, p->midi_fld_idx, m->midi );
            recd_set( rbuf->type, nullptr, r, p->loc_fld_idx,  m->loc  );
            recd_set( rbuf->type, nullptr, r, p->meas_fld_idx, m->meas );            
              
            rbuf->recdN += 1;

            p->msg_idx += 1;

            done_fl = p->msg_idx == p->msgN;

          }

          
          if( done_fl )            
            var_set(proc, kDoneFlPId, kAnyChIdx, true );
          
        }
      errLabel:
        return rc;
      }

      rc_t _report( proc_t* proc, inst_t* p )
      { return kOkRC; }

      class_members_t members = {
        .create  = std_create<inst_t>,
        .destroy = std_destroy<inst_t>,
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    } // score_player


    //------------------------------------------------------------------------------------------------------------------
    //
    // preset_select
    //
    namespace preset_select
    {
      enum {
        kInPId,
        kInitCfgPId,
        kPresetProcPId,
        kXfCntPId,
        kFNamePId,
        kLocPId,
        kOutIdxPId,
        kXfArgsPId, // kXfArgs:xfArgs+xf_cnnt
      };
      
      typedef struct
      {
        const char*          preset_proc_label; // proc containing preset label->value mapping
        unsigned             xf_cnt;        // count of transform processors 
        preset_sel::handle_t psH;           // location->preset map
        unsigned             loc_fld_idx;   // 
        unsigned             loc;           // last received location
        unsigned             out_idx;       // current transform processor index (0:xf_cnt)
        recd_array_t*        xf_recd_array; // xf_recd_array[ xf_cnt ] one record per 'out' var
        unsigned             presetN; 
        recd_array_t*        preset_recd_array; // preset_recd_array[ presetN ] one record per network preset
      } inst_t;

      
      rc_t _network_preset_value_to_recd_field(const char* proc_inst_label, const preset_value_t* ps_val_list, const recd_field_t* f, const recd_type_t* recd_type, recd_t* recd  )
      {
        rc_t rc = kOkRC;
        const preset_value_t* psv = nullptr;
        
        // search through the preset value list ...
        for(psv=ps_val_list; psv!=nullptr; psv=psv->link)
        {
          //printf("%s %s : %s %s\n",proc_inst_label,f->label,psv->proc->label,psv->var->label);
          
          // looking for the preset value whose proc instance label matches proc_inst_label and
          // whose var->label matches the recd field label
          if( textIsEqual(psv->proc->label,proc_inst_label) && textIsEqual(psv->var->label,f->label) )
          {
            // set the record field value to the preset value
            if((rc = value_from_value( psv->value, recd->valA[f->u.index] )) != kOkRC )
            {
              rc = cwLogError(kOpFailRC,"The preset value field '%s.%s value assignment failed.",cwStringNullGuard(proc_inst_label),cwStringNullGuard(f->label));
              goto errLabel;
            }
            
            break; // the search is complete
          }
        } 

        // It's possible that a record field value is not included in a preset spec - therefore it is not
        // an error to fail by not finding the preset value for a field.
        // In this case the record value is set to the default value set in the
        // record cfg in the 'preset_select' class desc.
        
        //if( ps_val_list!=nullptr && psv == nullptr )
        //  rc = cwLogError(kEleNotFoundRC,"The preset field '%s.%s' was not found.",cwStringNullGuard(proc_inst_label),cwStringNullGuard(f->label));
        
      errLabel:
        return rc;
      }
      
      rc_t _network_preset_to_record( network_t* net, const char* preset_label, const recd_type_t* recd_type, recd_t* recd )
      {
        rc_t rc = kOkRC;
        
        const network_preset_t* net_ps;

        // Locate the named preset label in the list of network presets
        if((net_ps = network_preset_from_label( *net, preset_label )) == nullptr )
        {
          rc = cwLogError(kEleNotFoundRC,"The network preset '%s' was not found.",cwStringNullGuard(preset_label));
          goto errLabel;
        }

        // the identified preset cannot be a 'dual' preset specification
        if( net_ps->tid != kPresetVListTId )
        {
          rc = cwLogError(kInvalidArgRC,"The network preset '%s' is not of type 'value_list'.",cwStringNullGuard(preset_label));
          goto errLabel;
        }

        // for each group in the recd
        for(const recd_field_t* f0 = recd_type->fieldL; f0!=nullptr; f0=f0->link)
          if( f0->group_fl )
          {
            // for each non-group field in this group
            for(const recd_field_t* f1=f0->u.group_fieldL; f1!=nullptr; f1=f1->link)
              if( !f1->group_fl )
              {
                // store the value of the preset field into the recd
                if((rc = _network_preset_value_to_recd_field(f0->label, net_ps->u.vlist.value_head, f1, recd_type, recd  )) != kOkRC )
                {                
                  goto errLabel;
                }
              }
          }
        
        
      errLabel:
        return rc;
      }

      rc_t _alloc_preset_recd_array( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        network_t* net = proc->net;
        
        if((p->presetN = preset_count(p->psH)) == 0 )
        {
          rc = cwLogError(kInvalidStateRC,"There are no presets defined.");
          goto errLabel;
        }

        // create p->preset_recd_array
        if((rc = recd_array_create(p->preset_recd_array, p->xf_recd_array->type, nullptr,  p->presetN )) != kOkRC )
        {
          rc = cwLogError(rc,"The preset record array allocation failed.");
          goto errLabel;
        }

        if( p->preset_proc_label != nullptr && textLength(p->preset_proc_label)> 0 )
        {
          proc_t* net_proc = nullptr;
          
          if((rc = proc_find(*proc->net, p->preset_proc_label, kBaseSfxId, net_proc )) != kOkRC || net_proc->internal_net == nullptr )
          {
            rc = cwLogError(rc,"The preset network could not be found.");
            goto errLabel;
          }

          net = net_proc->internal_net;
        }
        
        // for each preset ...
        for(unsigned i=0; i<p->presetN; ++i)
        {
          // ... fill a record in p->preset_recd_array with the values from the preset.
          if((rc = _network_preset_to_record( net, preset_label(p->psH,i), p->preset_recd_array->type, p->preset_recd_array->recdA + i )) != kOkRC )
          {
            rc = cwLogError(rc,"The preset '%s' could not be converted to a 'record'.",cwStringNullGuard(preset_label(p->psH,i)));
            goto errLabel;
          }        
        }

        // Note that p->preset_recd_array[] is in the same order as cwPresetSel.preset_label() and therefore the
        // frag_idx in cwPresetSel.frag_t is the index into p->preset_recd_array[] of the associated record.

      errLabel:
        return rc;
      }

      unsigned _next_avail_xf_channel( inst_t* p )
      {
        p->out_idx += 1;
        if( p->out_idx == p->xf_cnt )
          p->out_idx = 0;
        return p->out_idx;
      }
      

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t        rc    = kOkRC;        
        const char* fname = nullptr;
        rbuf_t*     rbuf;
        const object_t* cfg = nullptr;
        char* exp_fname = nullptr;
        
        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kInitCfgPId,  "cfg",     kBaseSfxId, cfg,
                                      kPresetProcPId,"preset_proc", kBaseSfxId, p->preset_proc_label,
                                      kXfCntPId,    "xf_cnt",  kBaseSfxId, p->xf_cnt,
                                      kInPId,       "in",      kBaseSfxId, rbuf,
                                      kFNamePId,    "fname",   kBaseSfxId, fname,
                                      kLocPId,      "loc",     kBaseSfxId, p->loc,
                                      kOutIdxPId,   "out_idx", kBaseSfxId, p->out_idx)) != kOkRC )
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

        // Alloc an array of 'xf_cnt' records of type 'xf_args'.
        if((rc = var_alloc_record_array( proc, "xf_args", kBaseSfxId, kAnyChIdx, nullptr, p->xf_recd_array, p->xf_cnt )) != kOkRC )
          goto errLabel;
        
        // Create the 'xf_arg' outputs
        for(unsigned i=0; i<p->xf_cnt; ++i)
          rc = var_register_and_set( proc, "xf_args", kBaseSfxId+i, kXfArgsPId+i, kAnyChIdx, p->xf_recd_array->type, nullptr, 0 );
        
      errLabel:
        mem::release(exp_fname);
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        preset_sel::destroy(p->psH);
        recd_array_destroy(p->xf_recd_array);
        recd_array_destroy(p->preset_recd_array);

        return rc;
      }

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;
        rbuf_t* in_rbuf = nullptr;
        unsigned loc = kInvalidIdx;

        if( p->preset_recd_array == nullptr )
        {
          // Fill p->preset_recd_array with values from the network presets
          if((rc = _alloc_preset_recd_array(proc,p)) != kOkRC )
            goto errLabel;
        }
                
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
          rbuf_t*                   xf_rbuf    = nullptr;
          const preset_sel::frag_t* frag       = nullptr;
          unsigned                  preset_idx = kInvalidIdx;
          
          // lookup the fragment associated with the location
          if( preset_sel::track_loc( p->psH, loc, frag ) && frag != nullptr )
          {
            // get the next available xf output channel
            p->out_idx = _next_avail_xf_channel(p);

            // select the preset(s) from the current frag
            if((rc = var_get(proc, kXfArgsPId + p->out_idx, kAnyChIdx, xf_rbuf)) != kOkRC )
              goto errLabel;

            // get the preset index associated with the current frag
            if((preset_idx = fragment_play_preset_index(p->psH, frag )) == kInvalidIdx )
            {
              rc = cwLogError(kInvalidArgRC,"The current frag does not a valid preset associated with it.");
              goto errLabel;
            }
            
            // validate the preset index
            if( preset_idx >= p->presetN )
            {
              rc = cwLogError(kAssertFailRC,"The selected preset index is out of range.");
              goto errLabel;
            }
            
            // set the value of the selected preset
            xf_rbuf->recdA = p->preset_recd_array->recdA + preset_idx;
            xf_rbuf->recdN = 1;

            if((rc = var_set(proc, kOutIdxPId, kAnyChIdx, p->out_idx)) != kOkRC )
              goto errLabel;

            //printf("PS OUT_IDX:%i\n",p->out_idx);
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
        .value   = std_value<inst_t>,
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

      rc_t _value( proc_t* proc, inst_t* p, variable_t* var )
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
        .value   = std_value<inst_t>,
        .exec    = std_exec<inst_t>,
        .report  = std_report<inst_t>
      };
      
    } // score_follower    

    
    
  } // flow
} //cw
