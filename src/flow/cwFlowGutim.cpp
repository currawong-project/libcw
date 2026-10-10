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
#include "cwFlowRecd.h"
#include "cwFlowTypes.h"
#include "cwFlowNet.h"
#include "cwFlowProc.h"

#include "cwFlowGutim.h"
#include "cwKeyStateMonitor.h"
#include "cwGutimMeas.h"
#include "cwAutoRange.h"

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
        kSfDoneFlPId,
        kGotoMeasPId,
        kGotoSectionPId,
        kGotoLocPId,
        kBegLocSfPId,
        kEndLocSfPId,
        kPostGapSecSfPId,
        kResetSfPId
      };

      typedef struct meas_loc_str
      {
        unsigned meas_numb;
        unsigned loc_id;
      } meas_loc_t;

      typedef struct section_loc_str
      {
        char*    section_id;
        unsigned loc_id;
      } section_loc_t;

      typedef struct recd_str
      {
        unsigned beg_loc;
        unsigned end_loc;
        unsigned piano_id;
        double   post_gap_dur_sec;  // last record is set to -1
      } recd_t;
      
      typedef struct
      {
        recd_t*  recdA;
        unsigned recdN;

        meas_loc_t* meas_locA;
        unsigned    meas_locN;

        section_loc_t* sect_locA;
        unsigned       sect_locN;
        
        unsigned cur_recd_idx;
        unsigned loc_fld_idx;

        list_t*  section_list;
        bool exec_done_fl;
        
      } inst_t;

      /*
          {
            "beg_loc": 0,
            "end_loc": 251,
            "player_id": 4,
            "post_gap_dur_sec": 23.32,
            "max_dur_sec: 1.5634,  # max IOI duration 
            "piano_id": 0
          },

       */

      rc_t _parse_meas_loc_map( proc_t* proc, inst_t* p, const object_t* meas_loc_cfg )
      {
        rc_t rc = kOkRC;
        
        p->meas_locN = meas_loc_cfg->child_count();
        p->meas_locA = mem::allocZ<meas_loc_t>( p->meas_locN );


        // for each meas-loc map record
        for(unsigned i=0; i<p->meas_locN; ++i)
        {
          const object_t* pair   = nullptr;
          unsigned        loc_id = kInvalidId;
          
          // validate the pair
          if((pair = meas_loc_cfg->child_ele(i)) == nullptr || !pair->is_pair())
          {
            rc = proc_error(proc,kInvalidArgRC,"The meas/loc record is invalid at index %i.",i);
            goto errLabel;
          }
                    
          // read the loc-id
          if((rc = pair->pair_value()->value(p->meas_locA[i].loc_id)) != kOkRC )
          {
            rc = proc_error(proc,rc,"Error parsing the loc-id at index:%i",i);
            goto errLabel;
          }

          // validate the measure number
          if( pair->pair_label() == nullptr || textLength(pair->pair_label()) == 0 )
          {
            rc = proc_error(proc,kInvalidArgRC,"The measure number at index:%i is invalid.",i);
            goto errLabel;
          }

          // convert the meas. number to an integer
          if((rc = string_to_number(pair->pair_label(),p->meas_locA[i].meas_numb)) != kOkRC )
          {
            rc = proc_error(proc,rc,"Measure number parse failed at index:%i.",i);
            goto errLabel;
          }
            
        }
        
      errLabel:
        return rc;
      }

      rc_t _parse_section_loc_map( proc_t* proc, inst_t* p, const object_t* sect_loc_cfg )
      {
        rc_t rc = kOkRC;

        p->sect_locN = sect_loc_cfg->child_count();
        p->sect_locA = mem::allocZ<section_loc_t>( p->sect_locN );

        // for each section-loc map record
        for(unsigned i=0; i<p->sect_locN; ++i)
        {
          const object_t* pair = nullptr;;
          unsigned    loc_id    = kInvalidId;
           
          // validate the pair
          if((pair = sect_loc_cfg->child_ele(i)) == nullptr || !pair->is_pair())
          {
            rc = proc_error(proc,kInvalidArgRC,"The section/loc record is invalid at index %i.",i);
            goto errLabel;            
          }

          // read the loc-id
          if((rc = pair->pair_value()->value(p->sect_locA[i].loc_id)) != kOkRC )
          {
            rc = proc_error(proc,rc,"Error parsing the loc-id at index:%i",i);
            goto errLabel;
          }

          // validate the section-id
          if( pair->pair_label() == nullptr || textLength(pair->pair_label()) == 0 )
          {
            rc = proc_error(proc,kInvalidArgRC,"The section label at index:%i is invalid.",i);
            goto errLabel;
          }

          // 
          p->sect_locA[i].section_id = mem::duplStr(pair->pair_label());
            
        }
        
      errLabel:
        return rc;
      }

      rc_t _parse_recd_array( proc_t* proc, inst_t* p, const object_t* recd_array_cfg )
      {
        rc_t rc  = kOkRC;
        p->recdN = recd_array_cfg->child_count();
        p->recdA = mem::allocZ<recd_t>(p->recdN);
        
        for(unsigned i=0; i<p->recdN; ++i)
        {
          const object_t* r_cfg = recd_array_cfg->child_ele(i);
          recd_t* r = p->recdA + i;
          if((rc = r_cfg->getv("beg_loc",   r->beg_loc,
                               "end_loc",   r->end_loc,
                               "piano_id",  r->piano_id,
                               "post_gap_dur_sec", r->post_gap_dur_sec )) != kOkRC )
          {
            rc = proc_error(proc,rc,"Error parsing cfg record at index:%i",i);
            goto errLabel;
          }
        }
        
      errLabel:
        return rc;
      }
      
      rc_t _create_section_list( proc_t* proc, inst_t* p, const object_t* sect_loc_map_cfg )
      {
        rc_t rc = kOkRC;
        variable_t* var = nullptr;

        if((rc = list_create(p->section_list, sect_loc_map_cfg)) != kOkRC )
        {
          rc = proc_error(proc,rc,"List create failed.");
          goto errLabel;
        }

        if((rc = var_find(proc, "goto_section", kBaseSfxId, kAnyChIdx, var )) != kOkRC )
        {
          rc = proc_error(proc,rc,"The 'section_list' variable could not be found.");
          goto errLabel;
        }

        var->value_list = p->section_list;

      errLabel:
        return rc;
      }

      rc_t _parse_cfg( proc_t* proc, inst_t* p, const object_t* cfg )
      {
        rc_t rc = kOkRC;
        const object_t* recd_array_cfg   = nullptr;
        const object_t* meas_loc_map_cfg = nullptr;
        const object_t* sect_loc_map_cfg = nullptr;
        
        if((rc = cfg->getv("sf_ctlL",recd_array_cfg,
                           "meas_loc_map", meas_loc_map_cfg,
                           "section_loc_map", sect_loc_map_cfg )) != kOkRC )
        {
          rc = proc_error(proc,rc,"The 'hdr' record parse failed.");
          goto errLabel;
        }

        if((rc = _parse_recd_array(proc,p,recd_array_cfg)) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = _parse_section_loc_map(proc, p, sect_loc_map_cfg )) != kOkRC )
        {
          goto errLabel;
        }
        
        if((rc = _parse_meas_loc_map(proc, p, meas_loc_map_cfg )) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = _create_section_list( proc, p, sect_loc_map_cfg)) != kOkRC )
        {
          goto errLabel;
        }

      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"The cfg. parse failed.");
        
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
          if( recd_idx == p->recdN )
            proc_info(proc,"End-of-score encountered. Done!");
          else
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
          var_set(proc,kPostGapSecSfPId,kAnyChIdx,r->post_gap_dur_sec);
          var_set(proc,kResetSfPId, kAnyChIdx,true );

          p->cur_recd_idx = recd_idx;
          p->exec_done_fl = false;
          
          //proc_info(proc,"New range: %i - %i.",next_loc,r->end_loc);

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
            _setup_sf(proc,p,i,loc);
            break;
          }

        if( i >= p->recdN )
        {
          rc = proc_error(proc,kInvalidArgRC,"The 'goto' location '%i' is not valid.",loc);
          p->cur_recd_idx = kInvalidIdx;
        }
        
        return rc;
      }

      rc_t _goto_meas( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t     rc        = kOkRC;
        unsigned meas_numb = 0;
        unsigned loc_id    = kInvalidId;
        unsigned i         = kInvalidIdx;
        
        if((rc = var_get(var,meas_numb)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Error accessing the 'goto_meas' variable.");
          goto errLabel;
        }

        for(i=0; i<p->meas_locN; ++i)
          if( p->meas_locA[i].meas_numb == meas_numb )
          {
            if((rc = _goto_loc(proc,p,p->meas_locA[i].loc_id)) != kOkRC )
            {
              goto errLabel;
            }
            
            break;
          }

        if( i >= p->meas_locN )
          proc_warn(proc,"The measure '%i' could not be found.",meas_numb);
        
      errLabel:
        if( rc != kOkRC )
          proc_error(proc,rc,"Error seeking to measure number:%i",meas_numb);
        
        return rc;
      }
      
      rc_t _goto_section( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t        rc         = kOkRC;
        unsigned    list_idx   = kInvalidIdx;
        const char* section_id = nullptr;
        unsigned    loc_id     = kInvalidId;
        unsigned    i          = kInvalidIdx;
        
        if((rc = var_get(var,list_idx)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Error accessing the 'goto_section' list index.");
          goto errLabel;
        }

        if((rc = list_ele_value( p->section_list, list_idx, loc_id )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Error accessing the 'goto_section' loc. value at list index %i.",list_idx);
          goto errLabel;
        }

        if((rc = _goto_loc(proc,p,loc_id)) != kOkRC )
        {
          goto errLabel;
        }

      errLabel:
        if( rc != kOkRC )
        {
          rc = proc_error(proc,rc,"Error seeking to section:%s",cwStringNullGuard(section_id));
        }

        return rc;
        
      }

      
      rc_t _on_sf_loc(proc_t* proc, inst_t* p, unsigned loc_id )
      {
        rc_t rc = kOkRC;

        if( loc_id == kInvalidId )
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
            // if no next range exists
            if( p->cur_recd_idx+1 >= p->recdN )
            {
              proc_info(proc,"Preparing to encounter end-of-score.");
            }
            else
            {
              //proc_info(proc,"loc:%i cur-end-loc:%i At the end of the SF segment. Waiting for transition.",loc_id,p->recdA[ p->cur_recd_idx ].end_loc);
              
              ok_fl = true;
            }
          }

          if( !ok_fl )
          {
            proc_info(proc,"SF loc %i out of range (%i %i)",loc_id,p->recdA[p->cur_recd_idx].beg_loc,p->recdA[p->cur_recd_idx].end_loc);
          }
          
        }

        
        return rc;
      }

      rc_t _on_sf_done(proc_t* proc, inst_t* p)
      {
        rc_t rc = kOkRC;
        bool done_fl=false;
        if((rc = var_get(proc,kSfDoneFlPId,kAnyChIdx,done_fl)) != kOkRC )
        {
          rc = proc_error(proc,rc,"'done_fl' access failed.");
          goto errLabel;
        }

        if( done_fl )
        {
          double post_gap_sec = -1;
          var_get(proc,kPostGapSecSfPId,kAnyChIdx,post_gap_sec);
          proc_info(proc,"Post SF gap wait: %5.1f secs",post_gap_sec);
          _setup_sf(proc,p,p->cur_recd_idx + 1);
        }

      errLabel:
        return rc;
      }


      
      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t          rc        = kOkRC;        
        const char*   cfg_fname = nullptr;
        const rbuf_t* rbuf      = nullptr;
        unsigned      goto_loc = kInvalidId;
        unsigned      goto_meas = 0;
        unsigned      goto_sect = 0;
        bool          sf_done_fl = false;

        p->cur_recd_idx = kInvalidIdx;
        
        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kSfLocPId,   "sf_loc",   kBaseSfxId, rbuf,
                                      kSfDoneFlPId,       "sf_done_fl",   kBaseSfxId, sf_done_fl,
                                      kGotoMeasPId,    "goto_meas",    kBaseSfxId, goto_meas,
                                      kGotoSectionPId, "goto_section", kBaseSfxId, goto_sect,
                                      kGotoLocPId,     "goto_loc",     kBaseSfxId, goto_loc,
                                      kCfgFnamePId,    "cfg_fname",    kBaseSfxId, cfg_fname)) != kOkRC )
        {
           goto errLabel;
        }

        if((rc = var_register(proc,kAnyChIdx,
                              kBegLocSfPId, "sf_beg_loc",   kBaseSfxId,
                              kEndLocSfPId, "sf_end_loc",   kBaseSfxId,
                              kPostGapSecSfPId, "sf_post_gap_sec", kBaseSfxId,
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
          
        if((p->loc_fld_idx  = recd_array_field_index( rbuf->recd_array, "loc")) == kInvalidIdx )
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

        list_destroy(p->section_list);
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
            case kSfDoneFlPId:
              p->exec_done_fl = true;
              break;
              
            case kGotoMeasPId:
              _goto_meas(proc,p,var);
              break;

            case kGotoSectionPId:
              _goto_section(proc,p,var);
              break;
              
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

        for(unsigned i=0; i<rbuf->recd_array->recdN; ++i)
        {
          unsigned loc_id;
          
          if((rc = recd_get(rbuf->recd_array->recdA + i, p->loc_fld_idx, loc_id)) != kOkRC )
          {
            rc = proc_error(proc,rc,"Loc field read failed.");
            goto errLabel;
          }

          _on_sf_loc(proc,p,loc_id);
          
        }

        // check for 'done' from the SF after all the incoming loc's are processed otherwise
        // the segment boundaries will will be updated and the incoming loc's may be out of range.
        if( p->exec_done_fl )
        {
          _on_sf_done(proc,p);
          p->exec_done_fl = false;
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

        bool exec_reset_fl;
        bool exec_stop_fl;


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
                                  "msg_idx",   meas->msg_idx)) != kOkRC )
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

        cwLogInfo("Parsing TOC: %s",cfg_fname);
        if((rc = _parse_toc_list(proc, p, tocL )) != kOkRC )
        {
          goto errLabel;
        }
        
        cwLogInfo("Parsing measure list: %s",cfg_fname);
        if((rc = _parse_meas_list( proc, p, measL )) != kOkRC )
        {
          goto errLabel;
        }

        cwLogInfo("Parsing msg list:%s", cfg_fname);
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
        const msg_t* m = nullptr;
        if( msg_idx > p->msgN )
        {
          rc = proc_error(proc,kInvalidArgRC,"Cannot seek to invalid message index: %i  (message count=%i).",msg_idx,p->msgN);
          goto errLabel;
        }

        p->start_msg_idx = msg_idx;
        p->next_msg_idx  = msg_idx;
        p->cur_smp_idx   = p->msgA[msg_idx].smp_idx;

        var_set(proc,kMeasPId,kAnyChIdx,p->msgA[msg_idx].meas_numb);

        m = p->msgA + msg_idx;
        proc_info(proc,"next TLP msg: meas:%i sec:%6.2f smp_idx:%i loc:%i",m->meas_numb,m->sec,m->smp_idx,m->loc);

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

        if((rc = var_alloc_register_and_set(proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, 0, p->recd_array )) != kOkRC )
        {
          goto errLabel;
        }


        if((rc = _parse_cfg_file(proc, p, cfg_fname )) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = recd_array_field_index( p->recd_array,
                                         "midi", p->midi_fld_idx,
                                         "tlp_meas", p->meas_fld_idx,
                                         "port_id", p->port_fld_idx )) != kOkRC )
        {
          goto errLabel;
        }
        
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
              rc = _goto_msg(proc,p,p->measA[i].msg_idx);
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

      rc_t _set_output_record( proc_t* proc, inst_t* p, unsigned meas_numb, unsigned port_id, midi::ch_msg_t* m )
      {
        rc_t rc = kOkRC;
        
        assert( m != nullptr );
        //printf("TLP port:%i :  ch:%i status:%i d0:%i d1:%i\n",m->devIdx,m->ch,m->status,m->d0,m->d1);

        _update_key_state( proc, p, port_id, m );

        if((rc = recd_append( p->recd_array, nullptr, p->midi_fld_idx, m, p->meas_fld_idx, meas_numb, p->port_fld_idx, port_id )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Record output failed.");
          goto errLabel;
        }

      

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

      rc_t _send_midi_clear( proc_t * proc, inst_t* p, port_t* port, unsigned& buf_idx_ref, unsigned rowN, uint8_t status, unsigned mtxN )
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
              
              _set_output_record( proc, p, meas_numb, port->port_id, m );

              port->keyM[idx] = 0;
            }
          }
        
        }
      errLabel:
        return rc;
      }

  
      
      rc_t _send_all_notes_off( proc_t* proc, inst_t* p )
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
          
          _set_output_record( proc, p, meas_numb, p->portA[i].port_id, m );

          // we only send all-notes-off for ch 0 - hopefully this is enough
          if((rc = _setup_midi_ch_msg(proc, p, buf_idx, midi::kCtlMdId, midi::kAllNotesOffMdId, 0, m )) != kOkRC )
            goto errLabel;
          
          _set_output_record( proc, p, meas_numb, p->portA[i].port_id, m );

          // send note-off on to all active notes
          _send_midi_clear(proc, p, p->portA + i, buf_idx, midi::kMidiNoteCnt, midi::kNoteOffMdId, kKeyN );

          // send ctl value 0 to all active controllers
          _send_midi_clear(proc, p, p->portA + i, buf_idx, midi::kMidiCtlCnt,  midi::kCtlMdId,     kCtlN );
          
        }

      errLabel:
        if(rc != kOkRC )
           rc = proc_error(proc,rc,"All-notes-off failed.");
        return rc;
      }

      rc_t _on_stop( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        rbuf_t* rbuf = nullptr;

        if( p->enable_fl )
        {
        
          p->enable_fl = false;

          if((rc = _send_all_notes_off(proc, p )) != kOkRC )
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
            p->exec_stop_fl = true;            
            break;

          case kResetPId:
            p->exec_reset_fl = true;
            break;

        }
        return rc;
      }


      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;

        // set the record buffer to be empty
        recd_array_empty(p->recd_array);

        // if reset was requested
        if( p->exec_reset_fl )
        {
          _on_reset(proc,p);
          p->exec_reset_fl = false;
        }

        // if stop was requested
        if( p->exec_stop_fl )
        {
          _on_stop(proc,p);
          p->exec_stop_fl = false;
        }

        if( p->enable_fl )
        {
          unsigned cur_meas_numb = 1;
          unsigned new_meas_numb = cur_meas_numb;
          
          var_get(proc,kMeasPId,kAnyChIdx,cur_meas_numb);
          
          // while there are expired msgs 
          while( p->next_msg_idx < p->msgN && p->msgA[ p->next_msg_idx].smp_idx <= p->cur_smp_idx )
          {
            msg_t* msg = p->msgA + p->next_msg_idx;

            //if( msg->meas_numb == 202 && midi::isNoteOn( msg->midi_ch_msg.status, msg->midi_ch_msg.d1) )
            //  printf("%6.3f %i : %i : %i\n",msg->sec,msg->smp_idx,msg->midi_ch_msg.d0,p->cur_smp_idx-msg->smp_idx);

            // add the msg to the output record buffer
            if((rc = _set_output_record(proc,p, msg->meas_numb, msg->port_id, &msg->midi_ch_msg )) != kOkRC )
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
            _on_stop( proc, p );
          }

          // advance time
          p->cur_smp_idx += proc->ctx->framesPerCycle;

        }
      errLabel:

        //if( o_rbuf != nullptr && o_rbuf->recdN > 0 )
        //  proc_info(proc,"TLP recd count:%i",o_rbuf->recdN);
        
        
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

    //------------------------------------------------------------------------------------------------------------------
    //
    // key_state_monitor
    //
    namespace key_state_monitor
    {
      enum {
        kCfgFnamePId,
        kResetPId,
        kOutPId,
        kInBasePId,
      };
      
      typedef struct
      {
        unsigned                        in_port_cnt;
        recd_array_t*                   recd_array;
        cw::key_state_monitor::handle_t ksmH;

        unsigned midi_fld_idx;
        unsigned loc_fld_idx;
        unsigned trig_id_fld_idx;
        unsigned cur_smp_idx;
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t        rc        = kOkRC;        
        const char* cfg_fname = nullptr;

        if((rc = var_register_and_get(proc,kAnyChIdx, kCfgFnamePId,"cfg_fname",kBaseSfxId,cfg_fname)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Registration failed on 'cfg_name' variable.");
          goto errLabel;
        }

        if((rc = var_register(proc,kAnyChIdx,kResetPId,"reset",kBaseSfxId)) != kOkRC )
        {
          rc = proc_error(proc,rc,"Registration failed on 'reset' variable.");
          goto errLabel;
        }

        if((p->in_port_cnt = var_mult_count(proc,"in")) == kInvalidCnt || p->in_port_cnt == 0 )
        {
          rc = proc_error(proc,kInvalidArgRC,"The 'in' must be connected to a 'mult' source with at least one 'mult' instance.");
          goto errLabel;
        }

        p->midi_fld_idx    = kInvalidIdx;
        p->loc_fld_idx     = kInvalidIdx;
        p->trig_id_fld_idx = kInvalidIdx;

        // for each input port
        for(unsigned i=0; i<p->in_port_cnt; ++i)
        {
          rbuf_t* rbuf = nullptr;
          unsigned idx = kInvalidIdx;

          // register the input port and get pointer to the associated record buf
          if((rc = var_register_and_get(proc,kAnyChIdx,kInBasePId+i,"in",kBaseSfxId+i,rbuf)) != kOkRC )
          {
            rc = proc_error(proc,rc,"Registration failed on 'in' variable at index %i.",i);
            goto errLabel;            
          }

          // get the field index of the 'midi' fiield on in the input reocrd
          idx = recd_array_field_index( rbuf->recd_array, "midi");

          // if the 'midi' field index has not yet been assigned
          if( p->midi_fld_idx == kInvalidIdx )
            p->midi_fld_idx = idx;
          else
          {
            // if the 'midi' field index has been assigned then it must match the earlier values
            if( idx != p->midi_fld_idx )
            {
              rc = proc_error(proc,kInvalidArgRC,"The input record index on all the 'midi' input ports doesn't match.");
              goto errLabel;
            }
          }

          // if the input record does not have a 'midi' field
          if( idx == kInvalidIdx )
          {
            rc = proc_error(proc,kInvalidArgRC,"The input record on port index %i does not have a 'midi' field.",i);
            goto errLabel;
          }
          

          // get the index  of the input record 'loc' index
          idx = recd_array_field_index( rbuf->recd_array, "loc");

          // if the stored 'loc' index field index has not yet been assigned
          if( p->loc_fld_idx == kInvalidIdx )
            p->loc_fld_idx  = idx;
          else
          {
            // the 'loc' field index must be the same for all input records
            if( idx != p->loc_fld_idx)
            {
              rc = proc_error(proc,kInvalidArgRC,"The input record index on all the 'loc' input ports doesn't match.");
              goto errLabel;
            }
          }

          // if the 'loc' field index does not exist on this input record
          if( idx == kInvalidIdx )
          {
            rc = proc_error(proc,kInvalidArgRC,"The input record on port index %i does not have a 'loc' field.",i);
            goto errLabel;
          }
          
        }
        
        // register the output port
        if((rc = var_alloc_register_and_set(proc, "out", kBaseSfxId, kOutPId, kAnyChIdx, nullptr, 0, p->recd_array )) != kOkRC )
        {
          goto errLabel;
        }


        // get the 'trigger_id' field index on the output record
        if((p->trig_id_fld_idx = recd_array_field_index( p->recd_array, "trigger_id")) == kInvalidIdx )
        {
          rc = proc_error(proc,kInvalidArgRC,"The output record does not hava field named 'trigger_id'.");
          goto errLabel;
        }
        

        // create the internal key-state-monitor instance
        if((rc = create(p->ksmH, cfg_fname, proc->ctx->sample_rate )) != kOkRC )
        {
          goto errLabel;
        }


        
      errLabel:
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        destroy(p->ksmH);
        recd_array_destroy(p->recd_array);

        return rc;
      }

      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;

        switch( var->vid )
        {
          case kResetPId:
            reset(p->ksmH);
            break;
        }
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t rc      = kOkRC;

        recd_array_empty(p->recd_array);
        
        for(unsigned pi=0; pi<p->in_port_cnt; ++pi)
        {
          const rbuf_t* i_rbuf = nullptr;
              
          
          if((rc = var_get(proc,kInBasePId+pi,kAnyChIdx,i_rbuf)) != kOkRC )
          {
            goto errLabel;
          }
          

          for(unsigned i=0; i<i_rbuf->recd_array->recdN; ++i)
          {
            unsigned                                   loc_id     = kInvalidId;
            midi::ch_msg_t*                            m          = nullptr;
            unsigned                                   target_cnt = 0;
            const cw::key_state_monitor::trigger_id_t* trigA      = nullptr;

            // get the MIDI msg and the SF loc id
            if((rc = recd_get(i_rbuf->recd_array->recdA + i, p->midi_fld_idx, m, p->loc_fld_idx, loc_id )) != kOkRC )
            {
              
              rc = proc_error(proc,rc,"Input record read failed.");
              goto errLabel;
            }

            // update the key_state_monitor 
            if((rc =  on_msg( p->ksmH, p->cur_smp_idx, pi, m->ch, m->status, m->d0, m->d1, loc_id, target_cnt )) != kOkRC )
            {
              rc = proc_error(proc,rc,"key-state-monitor MIDI msg. update failed.");
              goto errLabel;
            }

            // if any triggers fired
            if( target_cnt )
            {
              // get the array of triggers that fired
              if((trigA = trigger_array( p->ksmH, target_cnt )) != nullptr )
              {
                // set the id of each trigger in an output record
                for(unsigned j=0; j<target_cnt && j<p->recd_array->allocRecdN; ++j)
                {
                  if((recd_append( p->recd_array, nullptr, p->trig_id_fld_idx, trigA[j].id )) != kOkRC )
                  {
                    rc = proc_error(proc,rc,"Record output failed.");
                    goto errLabel;
                  }
                }
              }
            }
            
          }

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
      
    }    // key_state_monitor
    
    //------------------------------------------------------------------------------------------------------------------
    //
    // gutim_perf_eval
    //
    namespace gutim_perf_eval
    {
      enum {
        kMeasCfgFNamePId,
        kMapCfgFNamePId,
        kVelTblFNamePId,
        kVelTblNamePId,
        kResetPId,
        kSfResetPId,
        kSfBegLocPId,
        kSfEndLocPId,
        kInPId,
        kSdOutPId,
        kCtlOutPId,
        kShmOutPId
      };
      
      typedef struct
      {
        gutim_meas::handle_t gmH;
        unsigned i_perf_note_idx_fld_idx;
        unsigned i_loc_fld_idx;
        unsigned i_sec_fld_idx;
        unsigned i_midi_fld_idx;
        unsigned i_score_vel_fld_idx;

        auto_range::handle_t arH;

        recd_array_t* sd_recd_array;
        recd_array_t* ctl_recd_array;
        recd_array_t* shm_recd_array;


        // auto-range input variable id's
        unsigned avg_loc_dev_sec_ar_id;
        unsigned avg_dyn_ar_id;
        unsigned avg_dyn_dev_ar_id;
        unsigned avg_chord_spread_secs_ar_id;
        unsigned avg_beat_period_dev_sec_ar_id;
        unsigned avg_beat_dur_pct_ar_id;
        unsigned avg_grace_period_dev_sec_ar_id;
        unsigned avg_grace_dur_pct_ar_id;
        unsigned section_dur_dev_pct_ar_id;

        // auto-range output variable spec-dist id's
        unsigned ceiling_sd_id;
        unsigned expo_sd_id;
        unsigned thresh_sd_id;
        unsigned upr_sd_id;
        unsigned lwr_sd_id;
        unsigned mix_sd_id;

        // auto-range output variable ctl id's
        unsigned per_note_fl_ctl_id;
        unsigned pri_prob_fl_ctl_id;
        unsigned pri_uniform_fl_ctl_id;
        unsigned pri_dry_on_play_fl_ctl_id;
        unsigned pri_allow_all_fl_ctl_id;
        unsigned pri_dry_on_sel_fl_ctl_id;

        // auto-range output variable SHM id's
        unsigned peak_fl_shm_id;
        unsigned peak_gain_shm_id;
        unsigned hgain_shm_id;
        unsigned hfeedback_shm_id;
        unsigned stretch_shm_id;
        unsigned expo_shm_id;


        //  sd output variable spec-dist id's
        unsigned ceiling_sd_fi;
        unsigned expo_sd_fi;
        unsigned thresh_sd_fi;
        unsigned upr_sd_fi;
        unsigned lwr_sd_fi;
        unsigned mix_sd_fi;

        // ctl output variable ctl id's
        unsigned per_note_fl_ctl_fi;
        unsigned pri_prob_fl_ctl_fi;
        unsigned pri_uniform_fl_ctl_fi;
        unsigned pri_dry_on_play_fl_ctl_fi;
        unsigned pri_allow_all_fl_ctl_fi;
        unsigned pri_dry_on_sel_fl_ctl_fi;

        // shm output variable SHM id's
        unsigned peak_fl_shm_fi;
        unsigned peak_gain_shm_fi;
        unsigned hgain_shm_fi;
        unsigned hfeedback_shm_fi;
        unsigned stretch_shm_fi;
        unsigned expo_shm_fi;
        
        
      } inst_t;


      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t          rc                 = kOkRC;        
        const char*   meas_cfg_fname     = nullptr;
        const char*   map_cfg_fname      = nullptr;
        const char*   vt_fname           = nullptr;
        const char*   vt_name            = nullptr;
        const rbuf_t* i_rbuf             = nullptr;
        bool          reset_fl           = false;
        bool          sf_reset_fl        = false;
        unsigned      sf_beg_loc         = kInvalidId;
        unsigned      sf_end_loc         = kInvalidId;
        char*         exp_meas_cfg_fname = nullptr;
        char*         exp_map_cfg_fname  = nullptr;
        char*         exp_vt_fname       = nullptr;
        

        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kMeasCfgFNamePId,    "meas_cfg_fname",     kBaseSfxId, meas_cfg_fname,
                                      kMapCfgFNamePId, "map_cfg_fname", kBaseSfxId, map_cfg_fname,
                                      kVelTblFNamePId, "vel_tbl_fname", kBaseSfxId, vt_fname,
                                      kVelTblNamePId,  "vel_tbl_name",  kBaseSfxId, vt_name,
                                      kResetPId,       "reset",         kBaseSfxId, reset_fl,
                                      kSfResetPId,     "sf_reset_fl",   kBaseSfxId, sf_reset_fl,
                                      kSfBegLocPId,    "sf_beg_loc",    kBaseSfxId, sf_beg_loc,
                                      kSfEndLocPId,    "sf_end_loc",    kBaseSfxId, sf_end_loc,
                                      kInPId,          "in",            kBaseSfxId, i_rbuf)) != kOkRC )
        {
          goto errLabel;
        }        
        
        if((rc = recd_array_field_index( i_rbuf->recd_array,
                                        "midi", p->i_midi_fld_idx,
                                         "perf_note_idx", p->i_perf_note_idx_fld_idx,
                                         "loc",       p->i_loc_fld_idx,
                                         "sec",       p->i_sec_fld_idx,
                                         "score_vel", p->i_score_vel_fld_idx )) != kOkRC )
        {
          goto errLabel;
        }

        // create and register the sd_out record array
        if((rc = var_alloc_register_and_set(proc, "sd_out", kBaseSfxId, kSdOutPId, kAnyChIdx, nullptr, 0, p->sd_recd_array )) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = recd_array_field_index( p->sd_recd_array, 
                                         "ceiling",p->ceiling_sd_fi,
                                         "sd_expo",p->expo_sd_fi,
                                         "thresh",p->thresh_sd_fi,
                                         "upr",p->upr_sd_fi,
                                         "lwr",p->lwr_sd_fi,
                                         "mix",p->mix_sd_fi)) != kOkRC )
        {
          goto errLabel;
        }

        // create and register the ctl_out record array
        if((rc = var_alloc_register_and_set(proc, "ctl_out", kBaseSfxId, kCtlOutPId, kAnyChIdx, nullptr, 0, p->ctl_recd_array )) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = recd_array_field_index( p->ctl_recd_array,                                         
                                         "per_note_fl",p->per_note_fl_ctl_fi,
                                         "pri_prob_fl",p->pri_prob_fl_ctl_fi,
                                         "pri_uniform_fl",p->pri_uniform_fl_ctl_fi,
                                         "pri_dry_on_play_fl",p->pri_dry_on_play_fl_ctl_fi,
                                         "pri_allow_all_fl",p->pri_allow_all_fl_ctl_fi,
                                         "pri_dry_on_sel_fl",p->pri_dry_on_sel_fl_ctl_fi)) != kOkRC )
        {
          goto errLabel;
        }

        // create and register the shm_out record array
        if((rc = var_alloc_register_and_set(proc, "shm_out", kBaseSfxId, kShmOutPId, kAnyChIdx, nullptr, 0, p->shm_recd_array )) != kOkRC )
        {
          goto errLabel;
        }

        if((rc = recd_array_field_index( p->shm_recd_array,
                                         "peak_fl",p->peak_fl_shm_fi,
                                         "peak_gain",p->peak_gain_shm_fi,
                                         "hgain",p->hgain_shm_fi,
                                         "hfeedback",p->hfeedback_shm_fi,
                                         "stretch",p->stretch_shm_fi,
                                         "shm_expo",p->expo_shm_fi)) != kOkRC )
        {
          goto errLabel;
        }                                         
        
        if((exp_meas_cfg_fname = proc_expand_filename(proc,meas_cfg_fname)) == nullptr )
        {
          goto errLabel;
        }

        if((exp_vt_fname = proc_expand_filename(proc, vt_fname)) == nullptr )
        {
          goto errLabel;
        }
        
        if((rc = create(p->gmH, exp_meas_cfg_fname, exp_vt_fname, vt_name )) != kOkRC )
        {
          goto errLabel;
        }

        if((exp_map_cfg_fname = proc_expand_filename(proc,map_cfg_fname)) == nullptr )
        {
          goto errLabel;
        }

        if((rc = create( p->arH, exp_map_cfg_fname )) != kOkRC )
        {
          goto errLabel;
        }

        // get the auto-range input variable id's
        if((rc = in_variable_label_to_id( p->arH, "avg_loc_dev_sec", p->avg_loc_dev_sec_ar_id )) != kOkRC )
          goto errLabel;          
        if((rc = in_variable_label_to_id( p->arH, "avg_loc_dev_sec", p->avg_loc_dev_sec_ar_id )) != kOkRC )
          goto errLabel;
        if((rc = in_variable_label_to_id( p->arH, "avg_dyn", p->avg_dyn_ar_id )) != kOkRC )
          goto errLabel;
        if((rc = in_variable_label_to_id( p->arH, "avg_dyn_dev", p->avg_dyn_dev_ar_id )) != kOkRC )
          goto errLabel;
        if((rc = in_variable_label_to_id( p->arH, "avg_chord_spread_secs", p->avg_chord_spread_secs_ar_id )) != kOkRC )
          goto errLabel;
        if((rc = in_variable_label_to_id( p->arH, "avg_beat_period_dev_sec", p->avg_beat_period_dev_sec_ar_id )) != kOkRC )
          goto errLabel;
        if((rc = in_variable_label_to_id( p->arH, "avg_beat_dur_pct", p->avg_beat_dur_pct_ar_id )) != kOkRC )
          goto errLabel;
        if((rc = in_variable_label_to_id( p->arH, "avg_grace_period_dev_sec", p->avg_grace_period_dev_sec_ar_id )) != kOkRC )
          goto errLabel;
        if((rc = in_variable_label_to_id( p->arH, "avg_grace_dur_pct", p->avg_grace_dur_pct_ar_id )) != kOkRC )
          goto errLabel;
        if((rc = in_variable_label_to_id( p->arH, "section_dur_dev_pct", p->section_dur_dev_pct_ar_id )) != kOkRC )
          goto errLabel;

        // get the auto-range output spec-dist variable id's 
        if((rc = out_variable_label_to_id( p->arH, "ceiling", p->ceiling_sd_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "sd_expo", p->expo_sd_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "thresh", p->thresh_sd_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "upr", p->upr_sd_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "lwr", p->lwr_sd_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "mix", p->mix_sd_id )) != kOkRC )
          goto errLabel;

        // get the auto-range output ctl variable id's 
        if((rc = out_variable_label_to_id( p->arH, "per_note_fl", p->per_note_fl_ctl_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "pri_prob_fl", p->pri_prob_fl_ctl_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "pri_uniform_fl", p->pri_uniform_fl_ctl_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "pri_dry_on_play_fl", p->pri_dry_on_play_fl_ctl_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "pri_allow_all_fl", p->pri_allow_all_fl_ctl_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "pri_dry_on_sel_fl", p->pri_dry_on_sel_fl_ctl_id )) != kOkRC )
          goto errLabel;

        // get the auto-range output SHM variable id's 
        if((rc = out_variable_label_to_id( p->arH, "peak_fl", p->peak_fl_shm_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "peak_gain", p->peak_gain_shm_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "hgain", p->hgain_shm_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "hfeedback", p->hfeedback_shm_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "stretch", p->stretch_shm_id )) != kOkRC )
          goto errLabel;
        if((rc = out_variable_label_to_id( p->arH, "shm_expo", p->expo_shm_id )) != kOkRC )
          goto errLabel;


      errLabel:

        mem::release(exp_meas_cfg_fname);
        mem::release(exp_vt_fname);
        mem::release( exp_map_cfg_fname);
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        if( p->gmH.isValid() )
        {
          destroy(p->gmH);
        }

        recd_array_destroy(p->sd_recd_array);
        recd_array_destroy(p->ctl_recd_array);
        recd_array_destroy(p->shm_recd_array);
        
        return rc;
      }

      rc_t _on_sf_reset(proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        unsigned beg_loc_id = kInvalidId;
        unsigned end_loc_id = kInvalidId;

        if((rc = var_get(proc,kSfBegLocPId,kAnyChIdx,beg_loc_id)) != kOkRC )
          goto errLabel;
          
        if((rc = var_get(proc,kSfEndLocPId,kAnyChIdx,end_loc_id)) != kOkRC )
          goto errLabel;
        
        if((rc = set_current_section(p->gmH,beg_loc_id,end_loc_id)) != kOkRC )
          goto errLabel;
          
      errLabel:
        if(rc != kOkRC )
          proc_error(proc,rc,"Set section failed on beg:%i end:%i.", beg_loc_id, end_loc_id);
        
        return rc;
      }

      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;
        switch( var->vid )
        {
          case kResetPId:
            gutim_meas::reset(p->gmH);
            break;

          case kSfResetPId:
            _on_sf_reset(proc,p);
            break;
        }
        
        return rc;
      }

      rc_t _update_auto_range_inputs( proc_t* proc, inst_t* p, double sec, const gutim_meas::results_t* results )
      {
        rc_t rc = kOkRC;
        
        if((rc = update_variable( p->arH, sec,
                                  p->avg_loc_dev_sec_ar_id,results->avg_loc_dev_sec,
                                  p->avg_dyn_ar_id,                  results->avg_dyn,
                                  p->avg_dyn_dev_ar_id,              results->avg_dyn_dev,
                                  p->avg_chord_spread_secs_ar_id,    results->avg_chord_spread_secs,
                                  p->avg_beat_period_dev_sec_ar_id,  results->avg_beat_period_dev_sec,
                                  p->avg_beat_dur_pct_ar_id,         results->avg_beat_dur_pct,
                                  p->avg_grace_period_dev_sec_ar_id, results->avg_grace_period_dev_sec,
                                  p->avg_grace_dur_pct_ar_id,        results->avg_grace_dur_pct,
                                  p->section_dur_dev_pct_ar_id,      results->section_dur_dev_pct )) != kOkRC )
        {
          goto errLabel;
        }
        
      errLabel:
        return rc;
      }

      rc_t _set_output_records( proc_t* proc, inst_t* p, double sec )
      {
        rc_t rc = kOkRC;
        coeff_t ceiling,sd_expo,thresh,upr,lwr,mix;
        bool per_note_fl, pri_prob_fl, pri_uniform_fl, pri_dry_on_play_fl, pri_allow_all_fl, pri_dry_on_sel_fl;
        bool peak_fl;
        coeff_t peak_gain, hgain, hfeedback, stretch, shm_expo;
        
        // get the SD auto-range output values
        if((rc = get_value(p->arH,sec,
                           p->ceiling_sd_id, ceiling,
                           p->expo_sd_id, sd_expo,
                           p->thresh_sd_id, thresh,
                           p->upr_sd_id, upr,
                           p->lwr_sd_id, lwr,
                           p->mix_sd_id, mix)) != kOkRC )
        {
          rc = proc_error(proc,rc,"SD auto-range access failed.");
          goto errLabel;
        }
        
        // set the SD output records
        if((rc = recd_append( p->sd_recd_array, nullptr,
                         p->ceiling_sd_fi, ceiling,
                         p->expo_sd_fi, sd_expo,
                         p->thresh_sd_fi, thresh,
                         p->upr_sd_fi, upr,
                         p->lwr_sd_fi, lwr,
                         p->mix_sd_fi, mix )) != kOkRC )
        {
          rc = proc_error(proc,rc,"SD record output failed.");
          goto errLabel;
        }

        // get the ctl auto-range output values
        if((rc = get_value( p->arH, sec,
                            p->per_note_fl_ctl_id, per_note_fl,
                            p->pri_prob_fl_ctl_id, pri_prob_fl,
                            p->pri_uniform_fl_ctl_id, pri_uniform_fl,
                            p->pri_dry_on_play_fl_ctl_id, pri_dry_on_play_fl,
                            p->pri_allow_all_fl_ctl_id, pri_allow_all_fl,
                            p->pri_dry_on_sel_fl_ctl_id, pri_dry_on_sel_fl)) != kOkRC )
        {
          rc = proc_error(proc,rc,"ctl auto-range access failed.");
          goto errLabel;
        }

        // set the ctl output record
        if((rc = recd_append( p->ctl_recd_array, nullptr,                            
                         p->per_note_fl_ctl_fi,per_note_fl,
                         p->pri_prob_fl_ctl_fi,pri_prob_fl,
                         p->pri_uniform_fl_ctl_fi,pri_uniform_fl,
                         p->pri_dry_on_play_fl_ctl_fi,pri_dry_on_play_fl,
                         p->pri_allow_all_fl_ctl_fi,pri_allow_all_fl,
                         p->pri_dry_on_sel_fl_ctl_fi,pri_dry_on_sel_fl)) != kOkRC )
        {
          rc = proc_error(proc,rc,"ctl record output failed.");
          goto errLabel;
        }

        // get he SHM auto-range output values
        if((rc = get_value( p->arH, sec,
                            p->peak_fl_shm_id,peak_fl,
                            p->peak_gain_shm_id,peak_gain,
                            p->hgain_shm_id,hgain,
                            p->hfeedback_shm_id,hfeedback,
                            p->stretch_shm_id,stretch,
                            p->expo_shm_id,shm_expo)) != kOkRC )
        {
          rc = proc_error(proc,rc,"SHM auto-range access failed.");
          goto errLabel;
        }

        // set the SHM output record
        if((rc = recd_append(p->shm_recd_array, nullptr,
                            p->peak_fl_shm_fi,peak_fl,
                            p->peak_gain_shm_fi,peak_gain,
                            p->hgain_shm_fi,hgain,
                            p->hfeedback_shm_fi,hfeedback,
                            p->stretch_shm_fi,stretch,
                            p->expo_shm_fi,shm_expo)) != kOkRC )
        {
          rc = proc_error(proc,rc,"SHM record output failed.");
          goto errLabel;
        }

        // print the output values
        report(p->arH);
        
      errLabel:
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t            rc     = kOkRC;
        const rbuf_t*   i_rbuf = nullptr;
        midi::ch_msg_t* m      = nullptr;
        unsigned        loc_id = kInvalidId;
        unsigned        perf_note_idx = kInvalidIdx;
        double          sec    = 0.0;
        unsigned        score_vel = kInvalidId;
        
        
        if((rc = var_get(proc,kInPId,kAnyChIdx,i_rbuf)) != kOkRC )
          goto errLabel;

        recd_array_empty(p->sd_recd_array);
        recd_array_empty(p->ctl_recd_array);
        recd_array_empty(p->shm_recd_array);
        
        // for each incoming score-following record
        for(unsigned i=0; i<i_rbuf->recd_array->recdN; ++i)
        {
          if((rc = recd_get(i_rbuf->recd_array->recdA + i,
                            p->i_midi_fld_idx, m,
                            p->i_perf_note_idx_fld_idx, perf_note_idx,
                            p->i_sec_fld_idx, sec,
                            p->i_score_vel_fld_idx, score_vel,
                            p->i_loc_fld_idx, loc_id )) != kOkRC )
          {
            goto errLabel;
          }

          // this is a valid score-followed note-on record
          if( perf_note_idx != kInvalidIdx )
          {
            // pass the score-followed note on to the GUTIM-meas object
            if((rc = on_note( p->gmH, perf_note_idx, loc_id, sec, m->d0, score_vel )) != kOkRC )
            {
              goto errLabel;
            }

            // if a measurement section is complete
            if( is_section_complete(p->gmH) )
            {
              gutim_meas::results_t* results;

              // get the measurement results
              if((rc = get_results( p->gmH, results )) != kOkRC && results != nullptr )
              {
                rc = cwLogError(rc,"measurements access failed.");
                goto errLabel;
              }

              // update the auto-range input variables
              if((rc = _update_auto_range_inputs(proc,p,sec,results)) != kOkRC )
              {
                rc = cwLogError(rc,"update auto-range input failed.");
                goto errLabel;
              }

              // get the auto-range output variables
              if((rc = _set_output_records(proc, p, sec )) != kOkRC )
              {
                rc = cwLogError(rc,"update output failed.");
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
      
    }    // gutim_perf_eval

    //------------------------------------------------------------------------------------------------------------------
    //
    // event_trig_ctl
    //
    namespace event_trig_ctl
    {
      enum {
        kCfgFNamePId,
        kMeasPId,
        kPendingSecPId,
        kMidiInPId,
        kMidiOutPId,
        kBtnBasePId
      };

      enum {
        kNextBtnOffset,
        kPlayBtnOffset,
        kStatusOffset,
        kBtnCnt
      };

      enum {
        kInvalidStatusId,
        kPendingStatusId,
        kNextStatusId,
        kPlayingStatusId,
        kDoneStatusId,
        kWaitingStatusId,        
      };

      typedef struct {
        char*    title;
        unsigned meas;
        
        unsigned* dis_idA; // list of trigger id's which will disable this btn
        unsigned  dis_idN;
        
        unsigned* valueA;  // list of trigger id's this btn will fire
        unsigned  valueN;

        unsigned delta_flag; //
      } btn_t;
      
      typedef struct
      {
        btn_t*        btnA;
        unsigned      btnN;
        unsigned      next_btn_idx;
        unsigned      play_now_btn_idx;
        
        recd_array_t* recd_array;

        midi::ch_msg_t* midiA;
        unsigned        midiN;
        
        unsigned      i_midi_fld_idx;
        unsigned      o_midi_fld_idx;

        unsigned      pending_dur_sec;
        unsigned      pending_btn_idx;
        unsigned      pending_cycle_accum;
        unsigned      pending_cycle_limit;
        
      } inst_t;

      unsigned _next_btn_vid( unsigned btn_idx )
      { return kBtnBasePId + (btn_idx*kBtnCnt) + kNextBtnOffset; }

      unsigned _play_btn_vid( unsigned btn_idx )
      { return kBtnBasePId + (btn_idx*kBtnCnt) + kPlayBtnOffset; }
      
      unsigned _status_vid( unsigned btn_idx )
      { return kBtnBasePId + (btn_idx*kBtnCnt) + kStatusOffset; }
     
      rc_t _parse_cfg(proc_t* proc, inst_t* p, const char* fname )
      {
        rc_t      rc   = kOkRC;
        object_t* cfgL = nullptr;
        char* fn = nullptr;

        if((fn = proc_expand_filename(proc,fname)) == nullptr )
        {
          rc = proc_error(proc,kOpFailRC,"The button_list cfg. file '%s' could not be expanded.",cwStringNullGuard(fname));
          goto errLabel;
        }
        
        // parse the cfg file into an object format
        if((rc = objectFromFile( fn, cfgL )) != kOkRC )
        {
          rc = proc_error(proc,rc,"Cfg. file parse failed.");
          goto errLabel;
        }

        // verify that the file is not empty
        if( (p->btnN = cfgL->child_count()) == 0)
        {
          proc_error(proc,kInvalidArgRC,"The cfg. file (%s) is empty.",cwStringNullGuard(fn));
          goto errLabel;
        }
        
        p->btnA = mem::allocZ<btn_t>(p->btnN);
        
        for(unsigned i=0; i<p->btnN; ++i)
        {
          const object_t* r       = cfgL->child_ele(i);
          const char*     title   = nullptr;
          const object_t* dis_idL = nullptr;
          const object_t* valueL  = nullptr;
          unsigned v;
          
          if((rc = r->getv("title",title,
                           "meas",    p->btnA[i].meas,
                           "dis_idL", dis_idL,
                           "valueL",   valueL)) != kOkRC )
          {
            rc = proc_error(proc,rc,"An error occured while parsing the record at index %i.",i);
            goto errLabel;
          }

          p->btnA[i].title   = mem::duplStr(title);
          p->btnA[i].dis_idN = dis_idL->child_count();
          p->btnA[i].dis_idA = mem::allocZ<unsigned>(p->btnA[i].dis_idN);
          p->btnA[i].valueN  = valueL->child_count();
          p->btnA[i].valueA  = mem::allocZ<unsigned>(p->btnA[i].valueN);

          for(unsigned j=0; j<p->btnA[i].dis_idN; ++j)
          {
            if((rc = dis_idL->child_ele(j)->value(p->btnA[i].dis_idA[j])) != kOkRC )
            {
              proc_error(proc,rc,"Error parsing 'dis_idL' value at btn recd index %i dis_idL index %i.",i,j);
              goto errLabel;
            }

            if( p->btnA[i].dis_idA[j] > midi::kMaxCtlValue )
            {
              rc = proc_error(proc,kInvalidArgRC,"The button array dis-id %i out of range %i.",p->btnA[i].dis_idA[j],midi::kMaxCtlValue);
              goto errLabel;
            }
            
          }
          
          for(unsigned j=0; j<p->btnA[i].valueN; ++j)
          {
            if((rc = valueL->child_ele(j)->value(p->btnA[i].valueA[j])) != kOkRC )
            {
              rc = proc_error(proc,rc,"Error parsing 'valueL' value at btn recd index %i valueL index %i.",i,j);
              goto errLabel;
            }

            if( p->btnA[i].valueA[j] > midi::kMaxCtlValue )
            {
              rc = proc_error(proc,kInvalidArgRC,"The button array value %i out of range %i.",p->btnA[i].valueA[j],midi::kMaxCtlValue);
              goto errLabel;
            }
          }
        }
        
      errLabel:
        if( rc != kOkRC )
          rc = proc_error(proc,rc,"Parsing failed on the button array cfg file '%s'.",cwStringNullGuard(fname));

        mem::release(fn);
        return rc;
      }

      rc_t _reset( proc_t* proc, inst_t* p, unsigned meas_num )
      {
        rc_t rc       = kOkRC;

        p->pending_cycle_accum = 0;
        p->pending_btn_idx     = kInvalidIdx;
        p->next_btn_idx        = kInvalidIdx;
        p->play_now_btn_idx    = kInvalidIdx;
        proc_info(proc,"reset: meas:%i next btn idx:<invalid>",meas_num);
        
        for(unsigned i=0; i<p->btnN; ++i)
        {
          unsigned status_vid = _status_vid(i);
          unsigned status_id  = kInvalidStatusId;
          
          if( p->btnA[i].meas < meas_num )
          {
            status_id = kDoneStatusId;
          }
          else
          {
            if( p->next_btn_idx == kInvalidIdx )
            {
              status_id = kNextStatusId;
              p->next_btn_idx = i;
              proc_info(proc,"reset : next btn idx:%i",p->next_btn_idx);
            }
            else
            {
              status_id = kWaitingStatusId;
            }
          }

          if((rc = var_set(proc,status_vid,status_id)) != kOkRC )
          {
            goto errLabel;
          }
        }
        
      errLabel:
        return rc;
      }

      // Emit a MIDI msg to begin playing btnA[btn_idx].value[]
      rc_t _start_playing(proc_t* proc, inst_t* p, unsigned btn_idx)
      {
        rc_t rc = kOkRC;

        proc_info(proc,"_start_playing btn_idx:%i",btn_idx);
        
        if( btn_idx >= p->btnN )
        {
          rc = proc_error(proc,kInvalidStateRC,"The requested event index %i is out of range. (%i).",btn_idx,p->btnN);
          goto errLabel;
        }

        // for each value in btnA[].valueA[]
        for(unsigned i=0; i<p->btnA[btn_idx].valueN; ++i)
        {
          // verify that the MIDI msg buffer is sane
          if( p->recd_array->recdN >= p->midiN )
          {
            rc = proc_error(proc,kInvalidStateRC,"The MIDI array is empty although the record array is not. This should be impossible.");
            goto errLabel;
          }

          // get the output MIDI record
          midi::ch_msg_t* m      = p->midiA + p->recd_array->recdN;

          // get the event-id to transmit
          unsigned        evt_id = p->btnA[btn_idx].valueA[i];

          // validate the event-id (this was already done when the cfg. file was parsed)
          if( evt_id > midi::kMaxCtlValue )
          {
            rc = proc_error(proc,kInvalidStateRC,"The output event-id %i is out of range: %i.",evt_id,midi::kMaxCtlValue);
            goto errLabel;
          }
                    
          m->status = midi::kCtlMdId;
          m->d0     = 20;
          m->d1     = evt_id;

          proc_info(proc,"Send start msg: %i",evt_id);
          
          if((rc = recd_append(p->recd_array, nullptr, p->o_midi_fld_idx, m )) != kOkRC )
          {
            rc = proc_error(proc,rc,"MIDI record output failed.");
            goto errLabel;
          }
        }
        
      errLabel:
        return rc;
      }

      // Emit a MIDI msg to begin playing btnA[ p->next_btn_idx ].valueA[]
      rc_t _on_play_next( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;
        if( p->next_btn_idx == kInvalidIdx )
        {
          proc_warn(proc,"The next event to trigger has not yet been set.");
          goto errLabel;
        }

        if( p->pending_btn_idx != kInvalidIdx )
        {
          proc_warn(proc,"Play requests are ignored while 'pending' actions exist to prevent re-triggering.");
          goto errLabel;
        }
        
        rc = _start_playing(proc,p,p->next_btn_idx);
        
      errLabel:
        return rc;
      }

      bool _btn_has_value( const btn_t* btn, unsigned value )
      {
        for(unsigned i=0; i<btn->valueN; ++i)
          if( btn->valueA[i] == value )
            return true;
        return false;
      }
      unsigned _value_to_btn_index( inst_t* p, unsigned btn_value )
      {
        for(unsigned i=0; i<p->btnN; ++i)
          if( _btn_has_value(p->btnA+i,btn_value) )
            return i;

        return kInvalidIdx;          
      }

      rc_t _set_status_pending( proc_t* proc, inst_t* p, unsigned pending_btn_idx )
      {
        rc_t rc = kOkRC;
        unsigned target_btn_idx = kInvalidIdx;
        
        // for each possible btn
        for(unsigned i=0; i<p->btnN; ++i)
        {
          unsigned status_vid = _status_vid(i);
          unsigned cur_status = kInvalidStatusId;
          
          // get the current status of the btn
          if((rc = var_get(proc,status_vid,kAnyChIdx,cur_status)) != kOkRC )
            goto errLabel;

          // if this is the specified button
          if( i == pending_btn_idx )
          {
            // if the current status is not the same as the new status ... then update the status
            if( cur_status != kPendingStatusId ) 
              var_set(proc,status_vid,kAnyChIdx,kPendingStatusId );

            target_btn_idx = i;
          }
          else
          {
            // only one btn can be next - but this one say's it's also 'next'
            if( cur_status == kPendingStatusId )
            {
              proc_warn(proc,"Multiple btn's encoutered with 'pending' status.");
              unsigned status_id = target_btn_idx == kInvalidIdx ? kDoneStatusId : kWaitingStatusId;
              
              var_set(proc,status_vid, kAnyChIdx, status_id );
            }
          }            
        }

      errLabel:
        return rc;
        
      }

      rc_t _set_status_next( proc_t* proc, inst_t* p, unsigned next_btn_idx )
      {
        rc_t rc = kOkRC;
        unsigned target_btn_idx = kInvalidIdx;
        
        // for each possible btn
        for(unsigned i=0; i<p->btnN; ++i)
        {
          unsigned status_vid = _status_vid(i);
          unsigned cur_status = kInvalidStatusId;
          
          // get the current status of the btn
          if((rc = var_get(proc,status_vid,kAnyChIdx,cur_status)) != kOkRC )
            goto errLabel;

          // if this is the specified button
          if( i == next_btn_idx )
          {
            // if the current status is not the same as the new status ... then update the status
            if( cur_status != kNextStatusId )
            {
              if((rc = var_set(proc,status_vid,kAnyChIdx,kNextStatusId )) != kOkRC )
                goto errLabel;
              
              proc_info(proc,"'next' btn status set.");
            }

            target_btn_idx = i;
          }
          else
          {
            // only one btn can be next - but this one say's it's also 'next'
            if( cur_status == kNextStatusId )
            {
              proc_warn(proc,"Multiple btn's encoutered with 'next' status.");
              unsigned status_id = target_btn_idx == kInvalidIdx ? kDoneStatusId : kWaitingStatusId;
              
              var_set(proc,status_vid, kAnyChIdx, status_id );
            }
          }            
        }

      errLabel:
        if( rc != kOkRC )
          proc_error(proc,rc,"Error setting 'next' status.");
        return rc;
        
      }
      
      
      // Receive notification that the player associated with 'btn_value' has begun playing
      rc_t _on_set_status_playing( proc_t* proc, inst_t* p, unsigned btn_value )
      {
        rc_t rc = kOkRC;
        unsigned target_btn_idx = kInvalidIdx;

        // for each possible btn
        for(unsigned i=0; i<p->btnN; ++i)
        {
          unsigned status_vid = _status_vid(i);
          unsigned cur_status = kInvalidStatusId;
          
          // get the current status of the btn
          if((rc = var_get(proc,status_vid,kAnyChIdx,cur_status)) != kOkRC )
            goto errLabel;

          
          // if this is the specified button
          if( _btn_has_value( p->btnA + i, btn_value) )
          {
            // if the current status is not the same as the new status ... then update the status
            if( cur_status != kPlayingStatusId ) 
              var_set(proc,status_vid,kAnyChIdx,kPlayingStatusId );

            target_btn_idx = i;
            break;
          }
          else
          {
            // notice btn's that are still 'waiting'
            p->btnA[i].delta_flag = cur_status == kWaitingStatusId;
          }
            
        }

        // if the target btn was not found
        if( target_btn_idx == kInvalidIdx )
        {
          rc = proc_error(proc,kInvalidArgRC,"The button associated with play target value '%i' was not found.",btn_value);
          goto errLabel;
        }

        // Set the status of any btn's prior to the target btn that are still 'Waiting' to 'Done'.
        for(unsigned i=0; i<target_btn_idx; ++i)
          if( p->btnA[i].delta_flag )
            var_set(proc,_status_vid(i),kAnyChIdx,kDoneStatusId);


        if( target_btn_idx + 1 >= p->btnN )
        {
          proc_info(proc,"End-of-btn list encountered.");
        }
        else
        {
          if( p->pending_btn_idx != kInvalidIdx && p->pending_btn_idx != target_btn_idx+1 )
            proc_warn(proc,"A pending button existed and is being ignored and overwritten.");
              
          p->pending_btn_idx = target_btn_idx + 1;
          p->pending_cycle_accum = 0;
          
          proc_info(proc,"pending button advanced to:%i",p->pending_btn_idx);

          rc = _set_status_pending(proc,p,p->pending_btn_idx);
          
        }
        
        
      errLabel:
        return rc;
      }

      // Receive notification that the player associated with 'btn_value' has finished playing
      rc_t _on_set_status_done( proc_t* proc, inst_t* p, unsigned btn_value )
      {
        rc_t rc = kOkRC;
        unsigned target_btn_idx = kInvalidIdx;

        // for each possible btn
        for(unsigned i=0; i<p->btnN; ++i)
        {
          unsigned status_vid = _status_vid(i);
          unsigned cur_status = kInvalidStatusId;
          
          // get the current status of the btn
          if((rc = var_get(proc,status_vid,kAnyChIdx,cur_status)) != kOkRC )
            goto errLabel;

          
          // if this is the specified button
          if( _btn_has_value( p->btnA + i, btn_value) )
          {
            // if the current status is not the same as the new status ... then update the status
            if( cur_status != kDoneStatusId ) 
              var_set(proc,status_vid,kAnyChIdx,kDoneStatusId );

            target_btn_idx = i;
            break;
          }
          else
          {
            // notice btn's that are still 'waiting'
            p->btnA[i].delta_flag = cur_status == kWaitingStatusId;
          }
            
        }

        // if the target btn was not found
        if( target_btn_idx == kInvalidIdx )
        {
          rc = proc_error(proc,kInvalidArgRC,"The button associated with 'done' target value '%i' was not found.",btn_value);
          goto errLabel;
        }

        // Set the status of any btn's prior to the target btn that are still 'Waiting' to 'Done'.
        for(unsigned i=0; i<target_btn_idx; ++i)
          if( p->btnA[i].delta_flag )
            var_set(proc,_status_vid(i),kAnyChIdx,kDoneStatusId);

        
      errLabel:
        return rc;
      }
      

      rc_t _on_midi_msg(proc_t* proc, inst_t* p, const midi::ch_msg_t* m)
      {
        rc_t rc = kOkRC;

        proc_info(proc,"on_midi: 0x%x %i %i",m->status,m->d0,m->d1);

        switch( m->status )
        {
          case midi::kPbendMdId:
            {
              unsigned meas = midi::to14Bits( m->d0, m->d1 );
              proc_info(proc,"pbend reset to meas:%i",meas);
              rc = _reset(proc,p,meas);
            }
            break;
            
          case midi::kCtlMdId:
            {
              switch( m->d0 )
              {
                case 20: // a 'play-next' trigger has arrived
                  rc = _on_play_next(proc,p);
                  break;
                  
                case 21: // confirmation that a player has started
                  _on_set_status_playing(proc,p,m->d1);
                  break;
                  
                case 22: // notiification that a player has finished
                  _on_set_status_done(proc,p,m->d1);
                  break;
              }
            }
            break;
            
          default:
            proc_warn(proc,"Unexpected MIDI status:0x%x",m->status);
            break;
        }
          
        
      errLabel:
        return rc;
      }
      

      rc_t _create( proc_t* proc, inst_t* p )
      {
        rc_t          rc        = kOkRC;
        const char*   cfg_fname = nullptr;
        const rbuf_t* i_rbuf    = nullptr;
        unsigned      meas_num  = 0;

        if((rc = var_register_and_get(proc,kAnyChIdx,
                                      kCfgFNamePId,  "cfg_fname",   kBaseSfxId, cfg_fname,
                                      kMeasPId,      "meas",        kBaseSfxId,meas_num,
                                      kPendingSecPId,"pending_sec", kBaseSfxId, p->pending_dur_sec,
                                      kMidiInPId,    "midi_in",     kBaseSfxId,i_rbuf)) != kOkRC )
        {
          goto errLabel;
        }

        // parse the button array file
        if((rc = _parse_cfg(proc,p,cfg_fname)) != kOkRC )
        {
          goto errLabel;
        }

        // for each trigger event
        for(unsigned i=0; i<p->btnN; ++i)
        {
          if((rc = var_register(proc,kAnyChIdx,_next_btn_vid(i),"next_btn",kBaseSfxId + i)) != kOkRC )
          {
            goto errLabel;
          }

          // Set the UI title on the 'next butn
          if((rc = var_set_ui_title(proc,_next_btn_vid(i),kAnyChIdx,mem::duplStr(p->btnA[i].title))) != kOkRC )
          {
            goto errLabel;
          }

          // register the 'play' btn
          if((rc = var_register(proc,kAnyChIdx,_play_btn_vid(i),"play",kBaseSfxId + i)) != kOkRC )
          {
            goto errLabel;
          }
          
          // register the status 
          if((rc = var_register(proc,kAnyChIdx,_status_vid(i),"status",kBaseSfxId + i)) != kOkRC )
          {
            goto errLabel;
          }
          
        }
        
        // create and register the midi_out record array
        if((rc = var_alloc_register_and_set(proc, "midi_out", kBaseSfxId, kMidiOutPId, kAnyChIdx, nullptr, 0, p->recd_array )) != kOkRC )
        {
          goto errLabel;
        }

        p->midiN = p->recd_array->allocRecdN;
        p->midiA = mem::allocZ<midi::ch_msg_t>(p->midiN);
        

        // get the field index for the MIDI input field
        if((rc = recd_array_field_index(i_rbuf->recd_array,"midi",p->i_midi_fld_idx)) != kOkRC )
          goto errLabel;

        // get the field index for the MIDI output field
        if((rc = recd_array_field_index(p->recd_array,"midi",p->o_midi_fld_idx)) != kOkRC )
          goto errLabel;

        p->pending_cycle_limit= (p->pending_dur_sec * proc->ctx->sample_rate) / proc->ctx->framesPerCycle;
        proc_info(proc,"Pending duration %i sec's %i cycles",p->pending_dur_sec,p->pending_cycle_limit);
        
        _reset( proc, p, 1 );

        
      errLabel:        
        return rc;
      }

      rc_t _destroy( proc_t* proc, inst_t* p )
      {
        rc_t rc = kOkRC;

        for(unsigned i=0; i<p->btnN; ++i)
        {
          mem::release(p->btnA[i].title);
          mem::release(p->btnA[i].dis_idA);
          mem::release(p->btnA[i].valueA);
        }
        mem::release(p->btnA);
        recd_array_destroy(p->recd_array);

        return rc;
      }

      rc_t _notify( proc_t* proc, inst_t* p, variable_t* var )
      {
        rc_t rc = kOkRC;

        if( var->vid == kMeasPId )
        {
          unsigned meas_num = 0;
          if((rc = var_get(var,meas_num)) != kOkRC )
            goto errLabel;
          
          _reset(proc,p,meas_num);
        }
        else
        {
          // if this was a grid btn press
          if( kBtnBasePId <= var->vid and var->vid < kBtnBasePId + (p->btnN*3) )
          {
            // determine the type of the btn (next/play/status)
            unsigned btn_type = (var->vid - kBtnBasePId) % kBtnCnt;

            // get the button array index assoc'd with this button
            unsigned btn_idx = (var->vid - kBtnBasePId) / kBtnCnt;

            // vaidate the button index
            if( btn_idx >= p->btnN )
            {
              rc = proc_error(proc,kInvalidStateRC,"An out of range (%i) button id %i was encountered.",p->btnN,btn_idx);
              goto errLabel;
            }

            // act on the button
            switch( btn_type )
            {
              case kNextBtnOffset:
                _reset(proc,p,p->btnA[btn_idx].meas);
                break;
                
              case kPlayBtnOffset:
                // defer playing until p->recd_array has been emptied.
                p->play_now_btn_idx = btn_idx;
                break;
                
              case kStatusOffset:
                // the status indicator is not a button
                break;

              default:
                assert(0);
            }
          }
        }     
        
      errLabel:
        return rc;
      }

      rc_t _exec( proc_t* proc, inst_t* p )
      {
        rc_t          rc     = kOkRC;
        const rbuf_t* i_rbuf = nullptr;
        
        if((rc = var_get(proc,kMidiInPId,kAnyChIdx,i_rbuf)) != kOkRC )
        {
          goto errLabel;
        }

        recd_array_empty(p->recd_array);

        // if a 'play' btn was pressed
        if( p->play_now_btn_idx != kInvalidIdx )
        {
          _start_playing(proc,p,p->play_now_btn_idx);
          p->play_now_btn_idx = kInvalidIdx;
        }

        // check for incoming MIDI messages
        for(unsigned i=0; i<i_rbuf->recd_array->recdN; ++i)
        {
          const midi::ch_msg_t* m;

          // get the incoming MIDI msg
          if((rc = recd_get(i_rbuf->recd_array->recdA + i, p->i_midi_fld_idx, m)) != kOkRC )
          {
            goto errLabel;
          }

          // handle the incoming MIDI msg
          if((rc = _on_midi_msg(proc,p,m)) != kOkRC )
            goto errLabel;
        }

        // check if there is a 'pending' next button
        if( p->pending_btn_idx != kInvalidIdx )
        {
          p->pending_cycle_accum += 1;
          if( p->pending_cycle_accum >= p->pending_cycle_limit )
          {
            proc_info(proc,"Pending expired - setting next.");
            p->next_btn_idx = p->pending_btn_idx; // the pending button becomes the next button
            p->pending_btn_idx = kInvalidIdx;
            p->pending_cycle_accum = 0;
            _set_status_next(proc,p,p->next_btn_idx);
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
      
    }    // event_trig_ctl
    
  }
}
