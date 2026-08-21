#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwObject.h"
#include "cwTime.h"
#include "cwVectOps.h"
#include "cwDspTypes.h"

#include "cwMidiDecls.h"
#include "cwMidi.h"
#include "cwKeyStateMonitor.h"

namespace cw
{
  namespace key_state_monitor
  {
    typedef enum {
      kPendingStateId,    // cur loc. is before prime range and waiting for a loc in the prime range
      kPrimedStateId,     // a loc in the prime range was located - waiting for a trigger condition
      kKeyPedalUpStateId, // the note-on/note-off attribute has been satified - waiting for the key-up / pedal-up attribute to be satisfied
      kTrigDelayStateId,  // all the trigger attributes have been satisfied - waiting for the trigger delay to time-out
      kTriggeredStateId,  // the source was successfully triggered
      kSupercededStateId, // the target of this source was triggered by a different source
      kFailStateId,       // the location has passed the prime range and the the source did not trigger
    } state_id_t;

    typedef struct {
      unsigned    state_id;
      const char* label;
    } state_label_t;

    state_label_t state_labelA[] = {
      { kPendingStateId, "pending" },
      { kPrimedStateId, "prime" },
      { kKeyPedalUpStateId,"kp-up" },
      { kTrigDelayStateId, "trig-dly" },
      { kTriggeredStateId, "trigd" },
      {kSupercededStateId, "superc" },
      {kFailStateId,"fail" }      
    };
    
    enum {
      kNoteOnAttrFl      = 0x01,   // Wait for any note-on inside the prime-range
      kNoteOffAttrFl     = 0x02,   // Wait for a note-off associated with a note-on in the prime range.
      kDampUpAttrFl      = 0x04,   // Wait for the damper pedal to be raised for at least min_up_dur_ms
      kSostUpAttrFl      = 0x08,   // Wait for the sost. pedal to be raised for at least min_up_dur_ms
      kKeysUpAttrFl      = 0x10,   // Wait for all keys to be released for at least min_up_dur_ms
      kAllPedalsUpAttrFl = kDampUpAttrFl | kSostUpAttrFl    // Wait for all pedals to be released for at least min_up_dur_ms
    };
    
    typedef struct {
      const char* label;
      unsigned    value;
    } flag_ref_t;
    
    flag_ref_t flag_refA[] = {
        { "note-on",       kNoteOnAttrFl },   
        { "note-off",      kNoteOffAttrFl },
        { "damp-up",       kDampUpAttrFl },
        { "sost-up",       kSostUpAttrFl },
        { "all-keys-up",   kKeysUpAttrFl},
        { "all-pedals-up", kAllPedalsUpAttrFl}
      };

    struct source_str;
    typedef struct {
      char*     label;                // user supplied target label
      unsigned  id;                   // user supplied target id
      struct source_str* source_list; // linked list of sources that may trigger this target
    } target_t;

    typedef struct {
      uint8_t key_known_fl;
      uint8_t ctl_known_fl;
      uint8_t key_gate_fl;
      uint8_t ctl_value;
    } midi_state_t;

    struct port_str;
    
    typedef struct source_str {

      char*            label;  // source label
      target_t*        target; // source target
      struct port_str* port;   // owning port

      struct source_str* link; // target linked list
      
      unsigned beg_loc;        // prime begin loc
      unsigned end_loc;        // prime end loc
      unsigned attr_flags;     // trigger flags to detect 
      
      unsigned min_up_dur_ms;  // min. time the key/pedals must remain up before triggering
      unsigned min_up_dur_smp; //
                                
      unsigned trig_delay_ms;  // time delay ater achieving the trigger state before emitting the trigger
      unsigned trig_delay_smp; //

      state_id_t state_id;       // current matching state (see kXXXStateId)
      unsigned trig_flags;     // attribute flags detected so far 

      // key_known_fl = set if this pitch exists inside the prime range
      // key_gate_fl  = set when this note-on is detected 
      midi_state_t  note_on_state[ midi::kMidiNoteCnt ];
      
      unsigned trig_delay_start_smp; // start trigger delay sample frame index
     
    } source_t;

    typedef struct port_str {
      unsigned  port_id;
      
      source_t* sourceA;
      unsigned  sourceN;

      midi_state_t midi_state[ midi::kMidiNoteCnt ];

      int      key_down_cnt;
      unsigned keys_up_smp_idx;  // sample index when all-keys up begain or kInvalidIdx if some keys are down
      unsigned damp_up_smp_idx;  // sample index when damper came up or kInvalidIdx if it is down
      unsigned sost_up_smp_idx;  // sample index when sost last came up
      
    } port_t;
    
    typedef struct ksm_str
    {
      port_t*       portA;
      unsigned      portN;
      
      target_t*     targetA;
      unsigned      targetN;

      trigger_id_t* triggerA;
      unsigned      triggerN;
      unsigned      cur_trig_cnt;

      dsp::srate_t srate;
      unsigned     pedal_off_max_d1;  // The pedal is off (raised) when d1 < pedal_off_max_d1
      unsigned     pedal_on_min_d1;  //  The pedal is on (down) when d1 > peda_on_mid_d1

      
    } ksm_t;

    ksm_t* _handleToPtr( handle_t h )
    { return handleToPtr<handle_t,ksm_t>(h); }

    rc_t _destroy( ksm_t* p )
    {
      if( p != nullptr )
      {
        for(unsigned i=0; i<p->targetN; ++i)
        {
          mem::release(p->targetA[i].label);
        }

        for(unsigned i=0; i<p->portN; ++i)
        {          
          for(unsigned j=0; j<p->portA[i].sourceN; ++j)
          {
            mem::release(p->portA[i].sourceA[j].label);
          }
          
          mem::release(p->portA[i].sourceA);
        }
        
        mem::release(p->portA);
        mem::release(p->targetA);
        mem::release(p->triggerA);
      }

      return kOkRC;
    }

    const char* _state_id_to_label( state_id_t state_id )
    {
      unsigned n = std::size(state_labelA);

      for(unsigned i=0; i<n; ++i)
        if( state_labelA[i].state_id == state_id )
          return state_labelA[i].label;
      return nullptr;
    }

    unsigned _port_id_to_port_idx(ksm_t* p, unsigned port_id)
    {
      for(unsigned i=0; i<p->portN; ++i)
        if( p->portA[i].port_id == port_id )
          return i;
      return kInvalidIdx;
    }

    port_t* _port_id_to_port_ptr( ksm_t* p, unsigned port_id)
    {
      unsigned port_idx;
      if((port_idx = _port_id_to_port_idx(p,port_id)) != kInvalidIdx)
        return p->portA + port_idx;
      return nullptr;
    }

    rc_t _parse_target_array( ksm_t* p, const object_t* targetL_cfg )
    {
      rc_t     rc      = kOkRC;
      unsigned src_cnt = 0;
      
      p->targetN  = targetL_cfg->child_count();
      p->targetA  = mem::allocZ<target_t>(p->targetN);

      // for each target
      for(unsigned i=0; i<p->targetN; ++i)
      {
        const char*     target_label = nullptr;
        unsigned        target_id    = kInvalidId;
        const object_t* srcL_cfg     = nullptr;        
        const object_t* tgt_cfg      = targetL_cfg->child_ele(i);

        // parse the target header
        if((rc = tgt_cfg->getv("target_label",target_label,
                               "id",target_id,
                               "sourceL",srcL_cfg)) != kOkRC )
        {
          rc = cwLogError(rc,"Target record at index %i parse failed.",i);
          goto errLabel;
        }

        // validate the target label
        if( target_label == nullptr || textLength(target_label) == 0 )
        {
          rc = cwLogError(rc,"The target record at index %i does not have a valid label.");
          goto errLabel;
        }

        p->triggerN += srcL_cfg->child_count();

        // for each target source
        for(unsigned si=0; si<srcL_cfg->child_count(); ++si)
        {
          const object_t* src_cfg = srcL_cfg->child_ele(si);
          unsigned        port_id = kInvalidId;
          port_t*         port    = nullptr;

          // parse the port_id
          if((rc = src_cfg->getv("port_id",port_id)) != kOkRC )
          {
            rc = cwLogError(rc,"The source record at index:%si on the target '%s' could not be parsed.",si,cwStringNullGuard(target_label));
            goto errLabel;
          }

          // validate the port id
          if((port = _port_id_to_port_ptr(p,port_id)) == nullptr )
          {
            rc = cwLogError(kEleNotFoundRC,"An invalid port id '%s' was encountered on the target '%s' on the source at index %i.",port_id,cwStringNullGuard(target_label),si);
            goto errLabel;
          }

          // count the number of sources attached to this port
          port->sourceN += 1;
        }
        

        p->targetA[i].label = mem::duplStr(target_label);
        p->targetA[i].id    = target_id;
      }

    errLabel:
      return rc;
    }

    rc_t _parse_port_id_list( ksm_t* p, const object_t* portL_cfg )
    {
      rc_t rc = kOkRC;
      p->portN = portL_cfg->child_count();
      p->portA = mem::allocZ<port_t>(p->portN);

      for(unsigned i=0; i<p->portN; ++i)
      {
        const object_t* portId_cfg = portL_cfg->child_ele(i);        
        if((rc = portId_cfg->value( p->portA[i].port_id)) != kOkRC )
        {
          rc = cwLogError(rc,"Error parsing key-state-monitor the 'port_id' at index %i.",i);
          goto errLabel;
        }
      }

    errLabel:
      return rc;
    }

/*
          "port_id": 0,
        "beg_primer_loc": 1171,
        "end_primer_loc": 1174,
        "attrL": [
          "all-keys-up",
          "all-pedals-up"
        ],
        "pitchL": [
          23,
          37,
          41,
          50,
          31,
          21,
          23,
          37,
          41,
          50
        ],
        "up_min_dur_ms": 200,
        "trig_delay_ms": 0
      },

 */

    rc_t _parse_attr_label_to_flag( const char* label, unsigned& val_ref )
    {
      unsigned n = std::size( flag_refA );
      for(unsigned i=0; i<n; ++i)
        if( textIsEqual(label,flag_refA[i].label) )
        {
          val_ref = flag_refA[i].value;
          return kOkRC;
        }
      
      return cwLogError(kEleNotFoundRC,"The source attribute flag '%s' is not valid.",cwStringNullGuard(label));
    }
    
    rc_t _parse_attr_flags( ksm_t* p, source_t* src, const object_t* attrL_cfg )
    {
      rc_t     rc    = kOkRC;
      unsigned attrN = attrL_cfg->child_count();

      // for each source attribute
      for(unsigned i=0; i<attrN; ++i)
      {
        const char* attr_label = nullptr;

        // read the attribute label
        if((rc = attrL_cfg->child_ele(i)->value(attr_label)) != kOkRC )
        {
          rc = cwLogError(rc,"Error parsing the attribute flag at attribute index %i",i);
          goto errLabel;
        }

        unsigned flags = 0;
        
        // convert the attribute label to a flag value
        if((rc = _parse_attr_label_to_flag( attr_label, flags )) != kOkRC )
        {
          rc = cwLogError(rc,"The attribute '%s' at attribute index %i is not valid.",cwStringNullGuard(attr_label),i);
          goto errLabel;
        }

        // trace the flags for this source
        src->attr_flags += flags;

      }
           
    errLabel:
      return rc;
      
    }

    rc_t _parse_pitch_list( ksm_t* p, source_t* src, const object_t* pitchL_cfg)
    {
      rc_t rc = kOkRC;
      
      for(unsigned i=0; i<pitchL_cfg->child_count(); ++i)
      {
        unsigned midi_pitch = -1;
        
        if((rc = pitchL_cfg->child_ele(i)->value(midi_pitch)) != kOkRC )
        {
          rc = cwLogError(rc,"Pitch parsed on pitch index %i.",i);
          goto errLabel;
        }

        if( midi_pitch >= midi::kMidiNoteCnt )
        {
          rc = cwLogError(rc,"MIDI pitch values %i is out of the range (0-127).",midi_pitch);
          goto errLabel;
        }

        // track  the pitches of interest on this source
        src->note_on_state[ midi_pitch ].key_known_fl = 1;
        
      }
    errLabel:
      return rc;
    }
        
    rc_t _parse_source(ksm_t* p, source_t* src, const object_t* src_cfg )
    {
      rc_t rc = kOkRC;
      const object_t* attrL_cfg  = nullptr;
      const object_t* pitchL_cfg = nullptr;
      const char*     label      = nullptr;
      
      if((rc = src_cfg->getv("beg_primer_loc",src->beg_loc,
                             "end_primer_loc",src->end_loc,
                             "label",label,
                             "attrL",attrL_cfg,
                             "pitchL",pitchL_cfg,
                             "up_min_dur_ms",src->min_up_dur_ms,
                             "trig_delay_ms",src->trig_delay_ms)) != kOkRC )
      {
        goto errLabel;
      }

      src->min_up_dur_smp = (unsigned)((src->min_up_dur_ms * p->srate)/1000);
      src->trig_delay_smp = (unsigned)((src->trig_delay_ms * p->srate)/1000);
      
      src->label = mem::duplStr(label);

      if((rc = _parse_attr_flags( p, src, attrL_cfg )) != kOkRC )
      {
        goto errLabel;
      }

      if((rc = _parse_pitch_list( p, src, pitchL_cfg)) != kOkRC )
      {
        goto errLabel;
      }
    errLabel:
      return rc;
    }
    
    rc_t _parse_all_sources(ksm_t* p, const object_t* targetL_cfg )
    {
      rc_t     rc      = kOkRC;
      unsigned src_idx = 0;

      // create the source arrays for each port
      for(unsigned i=0; i<p->portN; ++i)
        if( p->portA[i].sourceN > 0 )
        {
          p->portA[i].sourceA = mem::allocZ<source_t>(p->portA[i].sourceN);
        }

      
      // allocate an array to hold the current count of sources instantiated on each port
      unsigned cur_port_src_cntA[ p->portN ];
      vop::zero(cur_port_src_cntA,p->portN);

      // for each target
      for(unsigned tgt_idx=0; tgt_idx<p->targetN; ++tgt_idx)
      {
        const char*     target_label = nullptr;
        const object_t* srcL_cfg     = nullptr;
        const object_t* tgt_cfg      = targetL_cfg->child_ele(tgt_idx);
        
        if((rc = tgt_cfg->getv("target_label",target_label,
                               "sourceL",srcL_cfg)) != kOkRC )
        {
          rc = cwLogError(rc,"Target record at index %i parse failed.",tgt_idx);
          goto errLabel;
        }

        // for each source on this target
        for(unsigned si=0; si<srcL_cfg->child_count(); ++si)
        {
          unsigned        port_id  = kInvalidId;
          unsigned        port_idx = kInvalidIdx;
          unsigned        src_idx  = 0;
          source_t*       src      = nullptr;
          const object_t* src_cfg  = srcL_cfg->child_ele(si);

          // get the port id for this source
          if((rc = src_cfg->getv("port_id",port_id)) != kOkRC )
          {
            rc = cwLogError(rc,"Error parsing the port id on source at index %i on the target %s.",si,cwStringNullGuard(target_label));
            goto errLabel;
          }

          // get the port index of this port  into p->portA[] (and also cnt_port_src_cntA[])
          if((port_idx = _port_id_to_port_idx(p,port_id)) == kInvalidIdx )
          {
            rc = cwLogError(rc,"An invalid port id %i on source at index %i on the target %s.",port_id,si,cwStringNullGuard(target_label));
            goto errLabel;            
          }

          // get the index of the new source record into p->portA[port_idx].sourceA[]
          src_idx = cur_port_src_cntA[ port_idx ]++;

          // validate the source index
          if( src_idx >= p->portA[ port_idx ].sourceN )
          {
            rc = cwLogError(rc,"The source index %i on the target %s is invalid.",src_idx,cwStringNullGuard(target_label));
            goto errLabel;            
          }

          // get a pointer to the new source record
          src = p->portA[ port_idx ].sourceA + src_idx;

          
          src->target = p->targetA + tgt_idx;   // set the target and port pointer on the new source record
          src->port   = p->portA + port_idx;    //


          // load the new source recd from the cfg
          if((rc = _parse_source(p,src,src_cfg)) != kOkRC )
          {
            rc = cwLogError(rc,"Error create the source at index %i on target '%s'.",si,src->target->label);
            goto errLabel;
          }
        }
        
      }
      
    errLabel:
        return rc;
    }

    rc_t _parse_cfg( ksm_t* p, const object_t* cfg )
    {
      rc_t rc = kOkRC;
      const object_t* portL_cfg   = nullptr;
      const object_t* targetL_cfg = nullptr;

      // parse the header record
      if((rc = cfg->getv("portL",portL_cfg,
                         "targetL",targetL_cfg)) != kOkRC )
      {
        rc = cwLogError(rc,"The key-state-monitor cfg. file header parse failed.");
        goto errLabel;
      }

      // parse the portId list
      if((rc = _parse_port_id_list(p, portL_cfg )) != kOkRC )
      {
        goto errLabel;
      }

      // parse the target array and get the total source array count
      if((rc = _parse_target_array(p, targetL_cfg )) != kOkRC )
      {
        goto errLabel;
      }

      // Parse the trigger detector sources
      if((rc = _parse_all_sources(p, targetL_cfg )) != kOkRC )
      {
        goto errLabel;
      }

      p->triggerA = mem::allocZ<trigger_id_t>(p->triggerN);
      
    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"The key-state-monitor cfg. file parse failed.");
          
      return rc;
    }    

    bool _source_compare( const source_t& s0, const source_t& s1 )
    {
      return s0.beg_loc < s1.beg_loc;
    }

    void _sort_and_link_sources( ksm_t* p )
    {
      // sort the portA[].sourceA[] 
      for(unsigned port_idx = 0; port_idx < p->portN; ++port_idx)
        std::sort(p->portA[port_idx].sourceA,p->portA[port_idx].sourceA + p->portA[port_idx].sourceN, _source_compare );

      // build the target->source_list
      for(target_t* t=p->targetA; t<p->targetA+p->targetN; ++t)
      {
        for(port_t* port=p->portA; port<p->portA+p->portN; ++port)
        {
          for(source_t* s=port->sourceA; s<port->sourceA + port->sourceN; ++s)            
            if( textIsEqual(s->target->label,t->label) )
            {
              assert( s->link == nullptr );
              s->link = s->target->source_list;
              s->target->source_list = s;
            }
        }
      }
    }

    rc_t _parse_cfg_file( ksm_t* p, const char* fname )
    {
      rc_t rc = kOkRC;
      object_t* cfg = nullptr;

      // convert the cfg. file to an object
      if((rc = objectFromFile( fname, cfg )) != kOkRC )
      {
        goto errLabel;
      }

      // parse the cfg. file
      if((rc = _parse_cfg( p, cfg)) != kOkRC )
      {
        goto errLabel;
      }

      _sort_and_link_sources(p);
      
    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Key-state-monitor configuration file parsing failed on '%s'.",cwStringNullGuard(fname));
      
      if( cfg != nullptr )
        cfg->free();
      
      return rc;      
    }

    void _set_state( source_t* src, state_id_t state_id, unsigned smp_idx )
    {
      if( src->state_id != state_id )
      {
        cwLogInfo("%i KSM: %s : %s %s",
                  smp_idx,
                  _state_id_to_label(state_id),
                  src->target->label,
                  src->label);
        src->state_id = state_id;
      }
    }

    rc_t _on_port_loc(ksm_t* p, port_t* port, unsigned loc_id, unsigned smp_idx)
    {
      rc_t rc = kOkRC;
      for(unsigned si=0; si<port->sourceN; ++si)
      {
        source_t* src = port->sourceA + si;
        
        if( src->state_id == kPendingStateId && src->beg_loc <= loc_id and loc_id <= src->end_loc )
        {
          _set_state(src,kPrimedStateId,smp_idx);
        }
      }
      return rc;
    }

    void _update_note_state( ksm_t* p, source_t* src, uint8_t status, uint8_t d0, uint8_t d1 )
    {
      switch( status )
      {
        case midi::kNoteOnMdId:
          src->note_on_state[ d0 ].key_gate_fl = 1;

          // if the note-on rule is in effect
          if( cwIsFlag(src->attr_flags, kNoteOnAttrFl ) )
          {
            src->trig_flags = cwSetFlag(src->trig_flags, kNoteOnAttrFl);
          }              
          break;
              
        case midi::kNoteOffMdId:
          // if the note-off rule is in effect
          if( src->note_on_state[d0].key_known_fl && src->note_on_state[d0].key_gate_fl && cwIsFlag(src->attr_flags, kNoteOffAttrFl ) )
          {            
            src->trig_flags = cwSetFlag(src->trig_flags, kNoteOffAttrFl);
          }
          src->note_on_state[ d0 ].key_gate_fl = 0;
          break;
              
      }
      
    }

    void _update_pedal_key_state( source_t* src, unsigned smp_idx )
    {
      port_t* port = src->port;

      // if the damper up rule is in effect
      if( cwIsFlag(src->attr_flags,kDampUpAttrFl) )
      {
        // and the damper has been up for at least min_up_dur_smp samples
        if( port->damp_up_smp_idx != kInvalidIdx && smp_idx >= port->damp_up_smp_idx && (smp_idx - port->damp_up_smp_idx) >= src->min_up_dur_smp )
        {
          src->trig_flags = cwSetFlag(src->trig_flags,kDampUpAttrFl);
        }
      }

      // if the sost. up rule is in effect
      if( cwIsFlag(src->attr_flags,kSostUpAttrFl) )
      {
        // and the sost. has been up for at least min_up_dur_smp samples
        if( port->sost_up_smp_idx != kInvalidIdx && smp_idx >= port->sost_up_smp_idx && (smp_idx - port->sost_up_smp_idx) >= src->min_up_dur_smp )
        {
          src->trig_flags = cwSetFlag(src->trig_flags,kSostUpAttrFl);
        }
      }

      // if the all-keys-up rule is in effect
      if( cwIsFlag(src->attr_flags,kKeysUpAttrFl) )
      {
        // and all keys have been up for at least min_up_dur_smp samples
        if( port->keys_up_smp_idx != kInvalidIdx && smp_idx >= port->keys_up_smp_idx && (smp_idx - port->keys_up_smp_idx) >= src->min_up_dur_smp )
        {
          src->trig_flags = cwSetFlag(src->trig_flags,kKeysUpAttrFl);
        }
      }

    }
    void _update_port_midi_state( ksm_t* p, unsigned smp_idx, port_t* port, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1 )
    {
      switch( status )
      {
        case midi::kNoteOnMdId:
          
          if( !port->midi_state[ d0 ].key_gate_fl )
            port->key_down_cnt += 1;
          
          port->midi_state[ d0 ].key_known_fl = 1;
          port->midi_state[ d0 ].key_gate_fl  = 1;
          break;
          
        case midi::kNoteOffMdId:
          {
            port->midi_state[ d0 ].key_known_fl = 1;
            port->midi_state[ d0 ].key_gate_fl  = 0;
          
            int key_down_cnt = port->key_down_cnt;
          
            if( port->midi_state[ d0 ].key_gate_fl )
              port->key_down_cnt -= 1;

            if( port->key_down_cnt < 0)
            {
              cwLogWarning("The key down count on port id %i is out of sync.",port->port_id);
              port->key_down_cnt = 0;
            }

            // if all keys are up
            if( key_down_cnt > 0 && port->key_down_cnt == 0)
              port->keys_up_smp_idx = smp_idx;
          } 
          break;
                    
        case midi::kCtlMdId:
          {            
            bool damp_on_0_fl = port->midi_state[ midi::kSustainCtlMdId   ].ctl_value > p->pedal_on_min_d1;
            bool sost_on_0_fl = port->midi_state[ midi::kSostenutoCtlMdId ].ctl_value > p->pedal_on_min_d1;
            
            port->midi_state[ d0 ].ctl_known_fl =  1;
            port->midi_state[ d0 ].ctl_value    = d1;

            
            bool damp_on_1_fl = port->midi_state[ midi::kSustainCtlMdId   ].ctl_value < p->pedal_off_max_d1;
            bool sost_on_1_fl = port->midi_state[ midi::kSostenutoCtlMdId ].ctl_value < p->pedal_off_max_d1;

            if( damp_on_1_fl )
            {
              port->damp_up_smp_idx = kInvalidIdx;
            }
            else
            {
              if( damp_on_0_fl )
              {
                port->damp_up_smp_idx = smp_idx;
              }
            }

            if( sost_on_1_fl )
            {
              port->sost_up_smp_idx = kInvalidIdx;
            }
            else
            {
              if( sost_on_0_fl )
              {
                port->sost_up_smp_idx = smp_idx;
              }
            }
            
          }
          break;
      }
      
    }

    rc_t _on_port_midi_msg(ksm_t* p, unsigned smp_idx, port_t* port, uint8_t ch, uint8_t status,uint8_t d0,uint8_t d1)
    {
      rc_t rc = kOkRC;

      // verify that the MIDI values are valid
      if( ch>=midi::kMidiChCnt || d0>=midi::kMidiNoteCnt || d1>127 )
        return cwLogError(kInvalidArgRC,"An invalid MIDI value was encountered: ch:%i status:%i d0:%i d1:%i",ch,status,d0,d1); 

      // convert note-on vel=0 to note-off
      if( status == midi::kNoteOnMdId && d1==0 )
        status = midi::kNoteOffMdId;

      // track the keyboard and pedal state on this port
      _update_port_midi_state(p, smp_idx, port, ch, status, d0, d1 );

      // update the state of each source assigned to this port
      for(unsigned si=0; si<port->sourceN; ++si)
      {
        source_t* src = port->sourceA + si;

        // if this source is no longer active
        if( src->state_id == kTriggeredStateId || src->state_id == kSupercededStateId || src->state_id == kFailStateId )
        {
          continue;
        }

        // if the source has entered the beg/end loc range
        if( src->state_id == kPrimedStateId )
        {
          _update_note_state(p,src, status, d0, d1 );

          // if the note-on/off state has been acheived
          if(    ((src->trig_flags & kNoteOnAttrFl)  == (src->attr_flags & kNoteOnAttrFl)) && 
                 ((src->trig_flags & kNoteOffAttrFl) == (src->attr_flags & kNoteOffAttrFl)) )
          {
            _set_state(src,kKeyPedalUpStateId,smp_idx);
          }
        }

        // if we are waiting to check the pedal or global key state
        if( src->state_id == kKeyPedalUpStateId )
        {
          _update_pedal_key_state(src,smp_idx);

          // if the key/pedal state has been acheived
          if(    ((src->trig_flags & kDampUpAttrFl) == (src->attr_flags & kDampUpAttrFl)) && 
                 ((src->trig_flags & kSostUpAttrFl) == (src->attr_flags & kSostUpAttrFl)) &&
                 ((src->trig_flags & kKeysUpAttrFl) == (src->attr_flags & kKeysUpAttrFl)) )
          {
            _set_state(src,kTrigDelayStateId,smp_idx);
            src->trig_delay_start_smp = smp_idx;
          }          
        }

        // if we are delaying after all the rules have been satisfied
        if( src->state_id == kTrigDelayStateId )
        {
          // if the delay has expired
          if( smp_idx >= src->trig_delay_start_smp && (smp_idx - src->trig_delay_start_smp) > src->trig_delay_smp )
          {
            // mark this source as having triggered the target
            _set_state(src,kTriggeredStateId,smp_idx);
            
            // mark all other sources assigned to this target as 'superceded'
            for(source_t* s = src->target->source_list; s!=nullptr; s=s->link)
              if( s->state_id != kTriggeredStateId )
                _set_state(s,kSupercededStateId,smp_idx);

            // insert the trigger in the output buffer for pickup by the client
            if( p->cur_trig_cnt >= p->triggerN )
              rc = cwLogError(kInvalidStateRC,"The trigger count %i exceeded the trigger buffer size %i.",p->cur_trig_cnt,p->triggerN);
            else
            {
              p->triggerA[ p->cur_trig_cnt ].id    = src->target->id;
              p->triggerA[ p->cur_trig_cnt ].label = src->target->label;
            
              p->cur_trig_cnt += 1;
            }            
          }
        }
      }
    errLabel:
      return rc;
    }
    
    
  }
}

cw::rc_t cw::key_state_monitor::create( handle_t& hRef, const char* cfg_fname, dsp::srate_t srate, uint8_t pedal_off_max_d1, uint8_t pedal_on_min_d1 )
{
  rc_t rc = kOkRC;
  ksm_t* p = nullptr;
  
  if( hRef.isValid() )
    if((rc = destroy(hRef)) != kOkRC )
      goto errLabel;

  p = mem::allocZ<ksm_t>();

  p->srate = srate;
  p->pedal_off_max_d1 = pedal_off_max_d1;
  p->pedal_on_min_d1  = pedal_on_min_d1;
  
  if((rc = _parse_cfg_file(p,cfg_fname)) != kOkRC )
    goto errLabel;

  hRef.set(p);
  
errLabel:
  if( rc != kOkRC )
    cwLogError(rc,"key_state_monitor create failed.");
  
  return rc;
}

cw::rc_t cw::key_state_monitor::destroy( handle_t& hRef )
{
  rc_t rc = kOkRC;
  if( !hRef.isValid() )
  {
    return rc;
  }

  ksm_t* p = _handleToPtr(hRef);

  if((rc = _destroy(p)) != kOkRC )
    return rc;

  mem::release(p);
  hRef.clear();
  return rc;  
}


cw::rc_t cw::key_state_monitor::reset( handle_t h, unsigned loc )
{
  rc_t   rc = kOkRC;
  ksm_t* p  = _handleToPtr(h);

  for(unsigned port_idx=0; port_idx<p->portN; ++port_idx)
  {
    port_t* port = p->portA + port_idx;

    for(unsigned i=0; i<midi::kMidiNoteCnt; ++i)
    {
      port->midi_state[i].key_known_fl = 0;
      port->midi_state[i].ctl_known_fl = 0;
      port->midi_state[i].key_gate_fl  = 0;
      port->midi_state[i].ctl_value    = 0;
    }
    
    port->key_down_cnt    = 0;
    port->keys_up_smp_idx = kInvalidIdx;
    port->damp_up_smp_idx = kInvalidIdx;
    port->sost_up_smp_idx = kInvalidIdx;
    
    for(unsigned src_idx=0; src_idx<port->sourceN; ++src_idx)
    {
      source_t* src = port->sourceA + src_idx;
      src->state_id   = kPendingStateId;
      src->trig_flags = 0;

      for(unsigned i=0; i<midi::kMidiNoteCnt; ++i)
        src->note_on_state[i].key_gate_fl = 0;
    }
    
  }
  
  return rc;  
}

cw::rc_t cw::key_state_monitor::on_msg( handle_t h, unsigned smp_idx, unsigned port_id, uint8_t ch, uint8_t status, uint8_t d0, uint8_t d1, unsigned loc_id, unsigned& trig_cnt_ref )
{
  rc_t rc = kOkRC;

  ksm_t* p = _handleToPtr(h);

  port_t* port;

  trig_cnt_ref    = 0;
  p->cur_trig_cnt = 0;

  // TODO: replace this call withh a map instead of a function that must search
  
  if((port = _port_id_to_port_ptr(p,port_id)) == nullptr )
  {
    rc = cwLogError(kEleNotFoundRC,"The port_id '%i' is not valid.",port_id);
    goto errLabel;
  }

  //printf("KSM: port:%i loc:%i ch:%i status:%i d0:%i d1:%i\n",port_id,loc_id,ch,status,d0,d1);
  
  if( loc_id != kInvalidId )
  {
    rc = _on_port_loc(p,port,loc_id,smp_idx);
  }

  if( status != midi::kInvalidStatusMdId )
  {
    rc = _on_port_midi_msg(p,smp_idx,port,ch,status,d0,d1);
  }

  trig_cnt_ref = p->cur_trig_cnt;
  
errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"on_msg() failed.");
  
  return rc;
}


const cw::key_state_monitor::trigger_id_t* cw::key_state_monitor::trigger_array( handle_t h, unsigned& trig_cnt_ref )
{
  ksm_t* p = _handleToPtr(h);

  if( p->cur_trig_cnt > 0  )
  {
    trig_cnt_ref = p->cur_trig_cnt;
    return p->triggerA;
  }

  trig_cnt_ref = 0;
  return nullptr;
}
