#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwVectOps.h"
#include "cwFile.h"
#include "cwFileSys.h"
#include "cwNumericConvert.h"
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

#include "cwPianoScore.h"
#include "cwScoreFollow2.h"
#include "cwPresetSel.h"
#include "cwMidiDetectors.h"
#include "cwFlowPerf.h"

namespace cw
{

  namespace flow
  {
    //------------------------------------------------------------------------------------------------------------------
    //
    // gutim_2_sf_ctl
    //
    namespace gutim_2_sf_ctl
    {
      enum {
        kCfgFnamePId,
        kSfLocPId,
        kGotoLocPId,
        kBegLocSfPId,
        kEndLocSfPId,
        kResetSfPId
      };

      typedef struct recd_str
      {
        unsigned beg_loc;
        unsigned end_loc;
        unsigned piano_id;
        unsigned player_id;        
      } recd_t;
      
      typedef struct
      {
        recd_t* recdA;
        unsigned recdN;
        unsigned cur_recd_idx;
        unsigned cur_loc_id;
        unsigned loc_fld_idx;
        
      } inst_t;

      /*
          {
            "beg_loc": 0,
            "end_loc": 251,
            "player_id": 4,
            "player_label": "AK",
            "color": "purple",
            "piano_id": 0
          },

       */

      rc_t _parse_cfg( proc_t* proc, inst_t* p, const object_t* cfg )
      {
        rc_t rc  = kOkRC;
        p->recdN = cfg->child_count();
        p->recdA = mem::allocZ<recd_t>(p->recdN);
        
        for(unsigned i=0; i<p->recdN; ++i)
        {
          const object_t* r_cfg = cfg->child_ele(i);
          recd_t* r = p->recdA + i;
          if((rc = r_cfg->getv("beg_loc",   r->beg_loc,
                               "end_loc",   r->end_loc,
                               "piano_id",  r->piano_id,
                               "player_id", r->player_id )) != kOkRC )
          {
            rc = proc_error(proc,rc,"Error parsing cfg record at index:%i",i);
            goto errLabel;
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

      bool _is_loc_in_recd( unsigned loc, const recd_t* r )
      {
        return r->beg_loc <= loc && loc <= r->end_loc;
      }

      rc_t _setup_sf(proc_t* proc, inst_t* p, unsigned recd_idx, unsigned next_loc=kInvalidId )
      {
        rc_t rc = kOkRC;
        if( recd_idx >= p->recdN )
        {
          rc= proc_error(proc,kInvalidArgRC,"Requested range record invalid %i >= %i.",recd_idx,p->recdN);
          goto errLabel;
        }
        else
        {
          const recd_t* r = p->recdA + recd_idx;

          if( next_loc == kInvalidId )
            next_loc = r->beg_loc;
        
          var_set(proc,kBegLocSfPId,kAnyChIdx,next_loc );
          var_set(proc,kEndLocSfPId,kAnyChIdx,r->end_loc );
          var_set(proc,kResetSfPId, kAnyChIdx,true );

          p->cur_recd_idx = recd_idx;

          proc_info(proc,"The 'gutim_2_sf_ctl' has set to range: %i - %i.",r->beg_loc,r->end_loc);

        }
      errLabel:
        return rc;
      }

      rc_t _goto_loc(proc_t* proc, inst_t* p, unsigned loc )
      {
        rc_t rc = kOkRC;
        unsigned i=0;
        
        for(; i<p->recdN; ++i)
          if( _is_loc_in_recd( loc, p->recdA + i ) )
          {
            _setup_sf(proc,p,i);
            break;
          }

        if( i >= p->recdN )
        {
          rc = proc_error(proc,kInvalidArgRC,"The 'goto' location '%i' is not valid.",loc);
          p->cur_recd_idx = kInvalidIdx;
          p->cur_loc_id = kInvalidIdx;
        }
        
        return rc;
      }

      rc_t _on_sf_loc(proc_t* proc, inst_t* p, unsigned loc_id )
      {
        rc_t rc = kOkRC;

        if( loc_id == p->cur_loc_id )
          return rc;
        
        if( p->cur_recd_idx == kInvalidIdx )
        {          
          proc_warn(proc,"The 'gutim_2_sf_ctl' does not have a valid tracking range."); 
        }
        else
        {
          bool ok_fl = false;

          // if the location is in the current range
          if( _is_loc_in_recd( loc_id, p->recdA +  p->cur_recd_idx ) )
          {
            ok_fl = true;
          }

          // if we are at, or past, the end of the current range
          if( loc_id >= p->recdA[ p->cur_recd_idx ].end_loc )
          {
            // if a next range exists
            if( p->cur_recd_idx+1 < p->recdN )
            {             
              _setup_sf(proc,p,p->cur_recd_idx + 1, loc_id+1);
              
              ok_fl = true;
            }
          }

          if( !ok_fl )
          {
            proc_info(proc,"SF loc %i out of range (%i %i)\n",loc_id,p->recdA[p->cur_recd_idx].beg_loc,p->recdA[p->cur_recd_idx].end_loc);
          }
          
        }

        
        return rc;
      }
      
      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t          rc        = kOkRC;        
        const char*   cfg_fname = nullptr;
        const rbuf_t* rbuf      = nullptr;
        unsigned      goto_loc = kInvalidId;

        p->cur_recd_idx = kInvalidIdx;
        p->cur_loc_id = kInvalidIdx;
        
        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kSfLocPId,   "sf_loc",   kBaseSfxId, rbuf,
                                      kGotoLocPId, "goto_loc", kBaseSfxId, goto_loc,
                                      kCfgFnamePId,"cfg_fname",kBaseSfxId, cfg_fname)) != kOkRC )
        {
           goto errLabel;
        }

        if((rc = var_register(proc,kAnyChIdx,
                              kBegLocSfPId, "sf_beg_loc",   kBaseSfxId,
                              kEndLocSfPId, "sf_end_loc",   kBaseSfxId,
                              kResetSfPId,  "sf_reset_fl",  kBaseSfxId )) != kOkRC )
        {
          goto errLabel;
        }

        
        if((rc = _parse_cfg_file( proc, p, cfg_fname )) != kOkRC )
        {
          goto errLabel;
        }

        if( goto_loc != kInvalidIdx )
          _goto_loc(proc,p,goto_loc);
          
        if((p->loc_fld_idx  = recd_type_field_index( rbuf->type, "loc")) == kInvalidIdx )
        {
          proc_error(proc,kInvalidArgRC,"The  input record does not have a 'loc' field.");
          goto errLabel;
        }
          
      errLabel:

        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        mem::release(p->recdA);

        return rc;
      }

      
      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;

        if( proc->ctx->isInRuntimeFl )
        {
          switch( var->vid )
          {
            case kGotoLocPId:
              {
                unsigned loc;
                if( var_get(var,loc) == kOkRC )
                  _goto_loc(proc,p,loc);
              }
              break;
              
          }
        }
        
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;
        
        const rbuf_t* rbuf = nullptr;
        
        if((rc = var_get(proc,kSfLocPId,kAnyChIdx,rbuf)) != kOkRC )
        {
          goto errLabel;
        }

        for(unsigned i=0; i<rbuf->recdN; ++i)
        {
          unsigned loc_id;
          
          if((rc = recd_get(rbuf->type, rbuf->recdA + i, p->loc_fld_idx, loc_id)) != kOkRC )
          {
            rc = proc_error(proc,rc,"Loc field read failed.");
            goto errLabel;
          }

          _on_sf_loc(proc,p,loc_id);
          
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
      
    }    // gutim_2_sf_ctl

    //------------------------------------------------------------------------------------------------------------------
    //
    // timeline_player
    //
    namespace timeline_player
    {
      enum {
        kCfgFNamePId,
        kGoMeasPId,
        kGoPortPId,
        kGoLocPId,
        kGoSectionPId,
        kStartPId,
        kStopPId,
        kResetPId,
        kMeasPId,
        kOutPId
      };

      enum {
        kKeyN = midi::kMidiChCnt*midi::kMidiNoteCnt,
        kCtlN = midi::kMidiChCnt*midi::kMidiCtlCnt,
        kMaxAllowedPortId = 128, // maximum allowable port id
      };

      typedef struct {
        unsigned    port_id;
        unsigned    player_id;
        char*       section_label;
        double      start_sec;
        unsigned    beg_loc;
        unsigned    end_loc;        
      } toc_t;

      typedef struct {
        double          sec;
        unsigned        meas_numb;
        unsigned        loc;
        unsigned        smp_idx;
        unsigned        port_id;
        midi::ch_msg_t  midi_ch_msg;
      } msg_t;

      typedef struct {
        unsigned number;
        double   start_sec;
        unsigned msg_idx;
        unsigned msg_cnt;
      } meas_t;

      typedef struct port_str {
        unsigned port_id;

        unsigned keyM[ kKeyN ];  // keyM[ kMidiChCnt*kMidiNoteCnt ] of last velocity for each note
        unsigned ctlM[ kCtlN ];  // ctlM[ kMIdiChCnt*kMidiCtlCnt  ] of last control value for each contrl
        
      } port_t;
      
      typedef struct
      {
        toc_t*   tocA;
        unsigned tocN;

        meas_t*  measA;
        unsigned measN;
        
        msg_t*   msgA;
        unsigned msgN;

        port_t*  portA;
        unsigned portN;

        unsigned* portIdMapA; // portIdMapA[ portIdMapN ] maps port_id to a port record in portA[]
        unsigned  portIdMapN;

        midi::ch_msg_t* midi_ch_bufA;
        unsigned        midi_ch_bufN;
        
        recd_array_t* recd_array;

        list_t*  port_list;


        unsigned midi_fld_idx;
        unsigned meas_fld_idx;
        unsigned port_fld_idx;

        bool     enable_fl;      // set if the player is started
        unsigned start_msg_idx;  // the first msg to play (defaults to 0, and set by seek commands)
        unsigned next_msg_idx;   // next msg to play 
        unsigned cur_smp_idx;    // cur time in samples (always >= msg[start_msg_idx] if the player is playing)
                                 // The next msg is emitted when msgA[next_msg_idx].smp_idx >= cur_smp_idx
        
      } inst_t;

      
      // tocL  = [ {port_id,player_id,section_id,start_sec,beg_loc,end_loc} ]
      // measL = [ {meas_numb, start_sec, msg_idx, msg_cnt  }
      // msgL  = [{sec,ch,status,d0,d1,sci_pitch,evt_id,player_id,port_id,section_label}]

      rc_t _parse_toc_list( proc_t* proc, inst_t* p, const object_t* tocL )
      {
        rc_t rc = kOkRC;
        p->tocN = tocL->child_count();
        p->tocA = mem::allocZ<toc_t>( p->tocN );

        for(unsigned i=0; i<p->tocN; ++i)
        {
          
          const object_t* toc_cfg = tocL->child_ele(i);
          toc_t* toc = p->tocA + i;
          const char* section_label;
          if((rc = toc_cfg->getv("port_id",   toc->port_id,
                                 "player_id", toc->player_id,
                                 "section_id",section_label,
                                 "start_sec", toc->start_sec,
                                 "beg_loc",   toc->beg_loc,
                                 "end_loc",   toc->end_loc)) != kOkRC )
          {
            rc = proc_error(proc,rc,"Error parsing TOC record at index:%i.",i);
            goto errLabel;
          }

          toc->section_label = mem::duplStr(section_label);

          // printf("%6.2f : port_id:%i plyr:%i %s bl:%i el:%i\n",toc->start_sec, toc->port_id,toc->player_id,section_label,toc->beg_loc,toc->end_loc);
        }

      errLabel:
        return rc;
      }

      rc_t _parse_meas_list( proc_t* proc, inst_t* p, const object_t* measL )
      {
        rc_t rc = kOkRC;
        p->measN = measL->child_count();
        p->measA = mem::allocZ<meas_t>(p->measN);

        for(unsigned i=0; i<p->measN; ++i)
        {
          const object_t* meas_cfg = measL->child_ele(i);
          meas_t*         meas     = p->measA + i;
          
          if((rc = meas_cfg->getv("number",    meas->number,
                                  "start_sec", meas->start_sec,
                                  "msg_idx",   meas->msg_idx,                                  
                                  "msg_cnt",   meas->msg_cnt)) != kOkRC )
          {
            rc = proc_error(proc,rc,"Error parsing the measure header record at index:%i.",i);
            goto errLabel;
          }

         
        }
      errLabel:
        return rc;
      }


      rc_t _parse_msg_list( proc_t* proc, inst_t* p, const object_t* msgL )
      {
        rc_t rc = kOkRC;
        p->msgN = msgL->child_count();
        p->msgA = mem::allocZ<msg_t>(p->msgN);

        for(unsigned i=0; i<p->msgN; ++i)
        {
          const object_t* msg_cfg = msgL->child_ele(i);
          msg_t*          msg     = p->msgA + i;
          
          if((rc = msg_cfg->getv("sec",       msg->sec,
                                 "meas_numb", msg->meas_numb,
                                 "loc",       msg->loc,
                                 "ch",        msg->midi_ch_msg.ch,
                                 "status",    msg->midi_ch_msg.status,
                                 "d0",        msg->midi_ch_msg.d0,
                                 "d1",        msg->midi_ch_msg.d1,
                                 "port_id",   msg->port_id)) != kOkRC )
          {
            rc = proc_error(proc,rc,"Error parsing msg at index %i",i);
            goto errLabel;
          }

          msg->smp_idx = (unsigned)(msg->sec * proc->ctx->sample_rate);
        }
        
      errLabel:
        return rc;
      }

      
      rc_t _parse_cfg( proc_t* proc, inst_t* p, const object_t* cfg, const char* cfg_fname )
      {
        rc_t rc = kOkRC;
        const object_t* tocL = nullptr;
        const object_t* measL = nullptr;
        const object_t* msgL  = nullptr;

        if((rc = cfg->getv("tocL", tocL, "measL", measL, "msgL", msgL )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Error parsing cfg header.");
          goto errLabel;          
        }

        if((rc = _parse_toc_list(proc, p, tocL )) != kOkRC )
        {
          goto errLabel;
        }
        
        if((rc = _parse_meas_list( proc, p, measL )) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = _parse_msg_list( proc, p, msgL )) != kOkRC )
        {
          goto errLabel;
        }
        
      errLabel:
        if(rc != kOkRC )
          rc = proc_error(proc,rc,"Error parsing the timeline player file '%s'.",cwStringNullGuard(cfg_fname));
        
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

        if((rc = _parse_cfg( proc, p, cfg, fname)) != kOkRC )
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

      const port_t* _id_to_port( inst_t* p, unsigned port_id )
      {
        for(unsigned i=0; i<p->portN; ++i)
          if( p->portA[i].port_id == port_id )
            return p->portA + i;
        return nullptr;
      }

      rc_t _create_port_array( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        
        if( p->msgN == 0 )
          return rc;

        unsigned max_port_id = kInvalidId;

        // grow portA[] until all unique port_id's have a record in p->portA[]
        for(unsigned i=0; i<p->msgN; ++i)
        {
          if( _id_to_port(p,p->msgA[i].port_id) == nullptr )
          {
            p->portN += 1;
            p->portA = mem::resizeZ<port_t>(p->portA,p->portN);
            p->portA[p->portN-1].port_id = p->msgA[i].port_id;

            if( p->portN == 1 )
              max_port_id = p->msgA[i].port_id;
            
            max_port_id = std::max(p->msgA[i].port_id,max_port_id);
            
          }
        }

        // if any port records exist
        if( p->portN > 0 )
        {
          // validate the port id - the max allowable port id is arbitrary and designed to prevent creating a massive p->portMapA[]
          if( max_port_id > kMaxAllowedPortId )
          {
            rc = proc_error(proc,kInvalidArgRC,"The max. port id (%i) is greater than the max. allowed port id (%i). Port array create failed.",max_port_id,kMaxAllowedPortId);
            goto errLabel;
          }

          // create the portIdMapA[]
          p->portIdMapN = max_port_id + 1;
          p->portIdMapA = mem::allocZ<unsigned>(p->portIdMapN);
        }

        // fill in the portIdMapA[]
        for(unsigned i=0; i<p->portN; ++i)
        {
          assert( p->portA[i].port_id < p->portIdMapN );
          p->portIdMapA[ p->portA[i].port_id ] = i;
        }
          
      errLabel:
        return rc;
      }

      rc_t _goto_msg( proc_t* proc, inst_t* p, unsigned msg_idx )
      {
        rc_t rc = kOkRC;

        if( msg_idx > p->msgN )
        {
          rc = proc_error(proc,kInvalidArgRC,"Cannot seek to invalid message index: %i  (message count=%i).",msg_idx,p->msgN);
          goto errLabel;
        }

        p->start_msg_idx = msg_idx;
        p->next_msg_idx  = msg_idx;
        p->cur_smp_idx   = p->msgA[msg_idx].smp_idx;

        var_set(proc,kMeasPId,kAnyChIdx,p->msgA[msg_idx].meas_numb);

      errLabel:
        return rc;
      }

      rc_t _create_port_list( proc_t* proc, inst_t* p )
      {
        rc_t        rc       = kOkRC;
        variable_t* var      = nullptr;
        const char* labelA[] = { "A","B","C" };
        unsigned    labelN   = std::size(labelA);
        
        if((rc = list_create(p->port_list, labelN ) ) != kOkRC )
        {
          rc = proc_error(proc,rc,"The port list create failed.");
          goto errLabel;
        }

        for(unsigned i=0; i<labelN; ++i)
        {
          if((rc = list_append(p->port_list,labelA[i],i)) != kOkRC )
          {
            rc = proc_error(proc,rc,"The port list append failed on index:%i.",i);
            goto errLabel;            
          }
        }
        
        if((rc = var_find(proc, "go_port", kBaseSfxId, kAnyChIdx, var )) != kOkRC )
        {
          rc = proc_error(proc,rc,"The 'port_list' variable could not be found.");
          goto errLabel;
        }

        var->value_list = p->port_list;

      errLabel:
        return rc;
      }

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t    rc   = kOkRC;
        const char* cfg_fname = nullptr;

        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kCfgFNamePId,"cfg_fname",kBaseSfxId, cfg_fname)) != kOkRC )
        {
           goto errLabel;
        }

        if((rc = var_register(proc,kAnyChIdx,
                              kGoMeasPId,    "go_meas",    kBaseSfxId,
                              kGoPortPId,    "go_port",    kBaseSfxId,
                              kGoLocPId,     "go_loc",     kBaseSfxId,
                              kGoSectionPId, "go_section", kBaseSfxId,
                              kStartPId,     "start",      kBaseSfxId,
                              kStopPId,      "stop",       kBaseSfxId,
                              kResetPId,     "reset",      kBaseSfxId,
                              kMeasPId,      "meas",       kBaseSfxId)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = var_alloc_register_and_set(proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, p->recd_array )) != kOkRC )
        {
          goto errLabel;
        }


        if((rc = _parse_cfg_file(proc, p, cfg_fname )) != kOkRC )
        {
          goto errLabel;
        }

        p->midi_fld_idx = recd_type_field_index( p->recd_array->type, "midi");
        p->meas_fld_idx = recd_type_field_index( p->recd_array->type, "meas");
        p->port_fld_idx= recd_type_field_index( p->recd_array->type, "port_id");

        p->midi_ch_bufN = p->recd_array->allocRecdN;
        p->midi_ch_bufA = mem::allocZ<midi::ch_msg_t>(p->midi_ch_bufN);

        if((rc = _create_port_array(proc, p )) != kOkRC )
          goto errLabel;

        if((rc = _create_port_list(proc, p )) != kOkRC )
          goto errLabel;
        
        _goto_msg(proc, p, 0 );

      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"timeline player create failed.");
        
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        for(unsigned i=0; i<p->tocN; ++i)
        {
          mem::release(p->tocA[i].section_label);
        }
        
        mem::release(p->msgA);
        mem::release(p->tocA);
        mem::release(p->measA);
        mem::release(p->midi_ch_bufA);
        mem::release(p->portA);
        mem::release(p->portIdMapA);
        recd_array_destroy(p->recd_array);
        list_destroy(p->port_list);

        return rc;
      }

      rc_t _goto_meas( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        unsigned meas_numb;
        if((rc = var_get(var,meas_numb)) == kOkRC )
        {
          proc_info(proc,"Searching for meas:%i",meas_numb);
          
          unsigned i = 0;
          for(i=0; i<p->measN;  ++i)
            if( p->measA[i].number == meas_numb )
            {
              rc = _goto_msg(proc,p,i);
              break;
            }

          if( i >= p->measN)
            rc = proc_error(proc,kInvalidArgRC,"The measure number:%i was not found.",meas_numb);
          
        }
        
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Seek to measure failed.");
        
        return rc;
      }

      bool _goto_loc( proc_t* proc, inst_t* p, unsigned port_id, unsigned loc )
      {
        bool     ok_fl = true;
        unsigned i;
        
        proc_info(proc,"Searching for loc:%i on port:%i",loc,port_id);

        // locate the msg matching the port and loc
        for(i=0; i<p->msgN; ++i)
          if( p->msgA[i].port_id == port_id && p->msgA[i].loc == loc )
          {
            ok_fl = _goto_msg(proc,p,i) == kOkRC;            
            break;
          }

        // if the msg was not found
        if( i >= p->msgN )
        {
          proc_info(proc,"The location %i on  port:%i was not found.",loc,port_id);
          ok_fl = false;
        }
        
        return ok_fl;
      }

      rc_t _goto_loc(  proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t     rc      = kOkRC;
        unsigned port_id = kInvalidId;
        unsigned loc     = kInvalidId;
        bool     ok_fl   = false;

        // get the current port 
        if((rc = var_get(proc,kGoPortPId,kAnyChIdx,port_id)) != kOkRC || port_id == kInvalidId )
        {
          rc = proc_error(proc,rc,"The current port id access failed.");
          goto errLabel;
        }
        
        // get the loc value to search for
        if((rc = var_get(var,loc)) != kOkRC || loc == kInvalidId )
        {          
          rc = proc_error(proc,rc,"The current loc id access failed.");
          goto errLabel;
        }

        ok_fl = _goto_loc(proc,p,port_id,loc);

      errLabel:
        if( rc != kOkRC || !ok_fl )
        {
          proc_error(proc,rc,"Loc seek failed.");
        }
        
        if( ok_fl )
          proc_info(proc,"Loc seek succeeded");
        
        return rc;
      }

      rc_t _goto_section(  proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t        rc         = kOkRC;
        unsigned    port_id    = kInvalidId;
        const char* section_id = nullptr;
        unsigned    toc_idx    = kInvalidIdx;
        bool        ok_fl      = false;
        
        // get the current port 
        if((rc = var_get(proc,kGoPortPId,kAnyChIdx,port_id)) != kOkRC || port_id == kInvalidId )
        {
          rc = proc_error(proc,rc,"The current port id access failed.");
          goto errLabel;
        }
        
        // get the toc section_id to search for
        if((rc = var_get(var,section_id)) != kOkRC || section_id == nullptr )
        {          
          rc = proc_error(proc,rc,"The current section id access failed.");
          goto errLabel;
        }

        proc_info(proc,"Searching for section:%s on port:%i",section_id,port_id);

        // locate the toc matching the port and section
        for(toc_idx=0; toc_idx<p->tocN; ++toc_idx)
          if( p->tocA[toc_idx].port_id == port_id && textIsEqual(p->tocA[toc_idx].section_label,section_id) )
          {
            ok_fl = _goto_loc(proc,p,port_id,p->tocA[toc_idx].beg_loc);
            break;
          }

        // if the toc was not found
        if( toc_idx >= p->tocN )
        {
          proc_info(proc,"The location section:%s on port:%i was not found.",section_id,port_id);
        }
        
      errLabel:
        if( rc != kOkRC || !ok_fl )
        {
          proc_error(proc,rc,"Section seek failed.");
        }

        if( ok_fl)
          proc_info(proc,"Section seek succeeded.");
        
        return rc;
      }

      void _set_key_state( unsigned* mtx, unsigned rowN, const midi::ch_msg_t* m, unsigned d1 )
      {
        unsigned idx =  m->ch * rowN + m->d0;
        assert(idx < rowN*midi::kMidiChCnt );
        mtx[ idx ] = d1;
      }
      
      rc_t  _update_key_state( proc_t* proc, inst_t* p, unsigned port_id, midi::ch_msg_t* m )
      {
        rc_t rc = kOkRC;
        if( port_id >= p->portIdMapN )
        {
          proc_error(proc,kInvalidArgRC,"The port_id %i is out of range of the port map (cnt=%i).",port_id,p->portIdMapN);
        }
        
        unsigned port_idx = p->portIdMapA[ port_id ];

        switch( midi::removeCh(m->status) )
        {
          case midi::kNoteOnMdId:
            _set_key_state(p->portA[ port_idx ].keyM,midi::kMidiNoteCnt,m,m->d1);
            break;
            
          case midi::kNoteOffMdId:
            _set_key_state(p->portA[ port_idx ].keyM,midi::kMidiNoteCnt,m,0);
            break;
            
          case midi::kCtlMdId:
            _set_key_state(p->portA[ port_idx ].ctlM,midi::kMidiCtlCnt,m,m->d1);            
            break;
        }

        return rc;
      }

      rc_t _set_output_record( proc_t* proc, inst_t* p, rbuf_t* rbuf, unsigned meas_numb, unsigned port_id, midi::ch_msg_t* m )
      {
        rc_t rc = kOkRC;
        
        recd_t* r = p->recd_array->recdA + rbuf->recdN;
        
        // if the output record array is full
        if( rbuf->recdN >= p->recd_array->allocRecdN )
        {
          rc = proc_error(proc,kBufTooSmallRC,"The internal record buffer overflowed. (buf recd count:%i).",p->recd_array->allocRecdN);
          goto errLabel;
        }

        _update_key_state( proc, p, port_id, m );
        
        recd_set( rbuf->type, nullptr, r, p->midi_fld_idx, m );
        recd_set( rbuf->type, nullptr, r, p->meas_fld_idx, meas_numb );
        recd_set( rbuf->type, nullptr, r, p->port_fld_idx, port_id);
        
        rbuf->recdN += 1;

      errLabel:
        return rc;
      }
      
      rc_t _setup_midi_ch_msg(proc_t* proc, inst_t* p, unsigned& buf_idx_ref, uint8_t status, uint8_t d0, uint8_t d1, midi::ch_msg_t*& m_ref )
      {
        rc_t rc = kOkRC;
        
        if( buf_idx_ref > p->midi_ch_bufN )
          return proc_error(proc,kBufTooSmallRC,"The all-note-off message buffer ran out of space. All notes and controllers may note have been reset.");

        m_ref = p->midi_ch_bufA + buf_idx_ref;
        buf_idx_ref += 1;
        
        memset(m_ref,0,sizeof(midi::ch_msg_t));
        m_ref->ch = 0;
        m_ref->status = status;
        m_ref->d0 = d0;
        m_ref->d1 = d1;

        return rc;
      }

      rc_t _send_midi_clear( proc_t * proc, inst_t* p, rbuf_t* rbuf, port_t* port, unsigned& buf_idx_ref, unsigned rowN, uint8_t status, unsigned mtxN )
      {
        rc_t rc = kOkRC;
        const unsigned  meas_numb = 0;
        midi::ch_msg_t* m         = nullptr;
        for(unsigned ch_idx=0; ch_idx<midi::kMidiChCnt; ++ch_idx)
        {
          unsigned ch_base_idx = ch_idx*rowN;
          
          for(unsigned note_idx=0; note_idx<rowN && buf_idx_ref < p->midi_ch_bufN; ++note_idx)
          {
            unsigned idx = ch_base_idx + note_idx;
            assert( idx < mtxN );              
            
            if( port->keyM[idx] > 0 )
            {
              if((rc = _setup_midi_ch_msg(proc, p, buf_idx_ref, status, note_idx, 0, m )) != kOkRC )
                goto errLabel;
              
              _set_output_record( proc, p, rbuf, meas_numb, port->port_id, m );

              port->keyM[idx] = 0;
            }
          }
        
        }
      errLabel:
        return rc;
      }

  
      
      rc_t _send_all_notes_off( proc_t* proc, inst_t* p, rbuf_t* rbuf )
      {
        rc_t            rc        = kOkRC;
        unsigned        buf_idx   = 0;
        unsigned        meas_numb = 0;
        midi::ch_msg_t* m         = nullptr;

        // for each MIDI output port
        for(unsigned i=0; i<p->portN && buf_idx < p->midi_ch_bufN; ++i)
        {
          // we only send reset-all-controllers for ch 0 - hopefully this is enough
          if((rc = _setup_midi_ch_msg(proc, p, buf_idx, midi::kCtlMdId, midi::kResetAllCtlsMdId, 0, m )) != kOkRC )
            goto errLabel;
          
          _set_output_record( proc, p, rbuf, meas_numb, p->portA[i].port_id, m );

          // we only send all-notes-off for ch 0 - hopefully this is enough
          if((rc = _setup_midi_ch_msg(proc, p, buf_idx, midi::kCtlMdId, midi::kAllNotesOffMdId, 0, m )) != kOkRC )
            goto errLabel;
          
          _set_output_record( proc, p, rbuf, meas_numb, p->portA[i].port_id, m );

          // send note-off on to all active notes
          _send_midi_clear(proc, p, rbuf, p->portA + i, buf_idx, midi::kMidiNoteCnt, midi::kNoteOffMdId, kKeyN );

          // send ctl value 0 to all active controllers
          _send_midi_clear(proc, p, rbuf, p->portA + i, buf_idx, midi::kMidiCtlCnt,  midi::kCtlMdId,     kCtlN );
          
        }

      errLabel:
        if(rc != kOkRC )
           rc = proc_error(proc,rc,"All-notes-off failed.");
        return rc;
      }

      rc_t _get_out_rbuf( proc_t* proc, inst_t* p, rbuf_t*& rbuf_ref )
      {
        rc_t rc = kOkRC;
        
        rbuf_ref = nullptr;

        // read the variable to get the output buffer 
        if((rc = var_get(proc,kOutPId,kAnyChIdx,rbuf_ref)) != kOkRC )
        {
          rc = proc_error(proc,kInvalidStateRC,"The multi-player '%s' does not have a validoutput buffer.",proc->label);
          goto errLabel;
        }

        // set the record buffer to be empty
        rbuf_ref->recdA = p->recd_array->recdA;
        rbuf_ref->recdN = 0;
      errLabel:
        return rc;
        
      }

      rc_t _on_stop( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        rbuf_t* rbuf = nullptr;

        if( p->enable_fl )
        {
        
          p->enable_fl = false;

          if((rc = _get_out_rbuf(proc, p, rbuf )) != kOkRC )
          {
            goto errLabel;
          }

          if((rc = _send_all_notes_off(proc, p, rbuf )) != kOkRC )
          {
            goto errLabel;
          }
        }

      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Stop failed.");
        return rc;
      }
      
      rc_t _on_reset( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        
        if((rc = _on_stop(proc,p)) != kOkRC )
          goto errLabel;

        
        if((rc = _goto_msg(proc,p,p->start_msg_idx)) != kOkRC )
          goto errLabel;

      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Reset failed.");
        
        return rc;
      }

      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        switch( var->vid )
        {
          case kGoMeasPId:
            _goto_meas(proc,p,var);
            break;

          case kGoPortPId:
            {
              unsigned port_id;
              var_get(var,port_id);
            }
            break;

          case kGoLocPId:
            _goto_loc(proc,p,var);
            break;

          case kGoSectionPId:
            _goto_section(proc,p,var);
            break;

          case kStartPId:
            p->enable_fl = true;
            break;

          case kStopPId:
            _on_stop(proc,p);
            break;

          case kResetPId:
            _on_reset(proc,p);
            break;

        }
        return rc;
      }


      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;
        if( p->enable_fl )
        {
          unsigned cur_meas_numb = 1;
          unsigned new_meas_numb = cur_meas_numb;
          
          var_get(proc,kMeasPId,kAnyChIdx,cur_meas_numb);
          
          // get the output record buf
          rbuf_t* rbuf    = nullptr;
          if((rc = _get_out_rbuf( proc, p, rbuf )) != kOkRC )
          {
            goto errLabel;
          }

          // while there are expired msgs 
          while( p->next_msg_idx < p->msgN && p->msgA[ p->next_msg_idx].smp_idx <= p->cur_smp_idx )
          {
            msg_t* msg = p->msgA + p->next_msg_idx;

            // add the msg to the output record buffer
            if((rc = _set_output_record(proc,p, rbuf, msg->meas_numb, msg->port_id, &msg->midi_ch_msg )) != kOkRC )
            {
              proc_error(proc,rc,"Output failed.");
              goto errLabel;
            }

            // if the current measure advanced 
            if( msg->meas_numb > new_meas_numb )
              new_meas_numb = msg->meas_numb;            

            p->next_msg_idx += 1;
          }

          // update the current measure display
          if( new_meas_numb > cur_meas_numb )
            var_set(proc,kMeasPId,kAnyChIdx,new_meas_numb);

          // if we have encountered the endof the message list ...
          if( p->next_msg_idx >= p->msgN )
          {
            proc_info(proc,"Last timeline message sent.");
          }

          // advance time
          p->cur_smp_idx += proc->ctx->framesPerCycle;

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
      
    }    // timeline_player

    
    
  }
}
