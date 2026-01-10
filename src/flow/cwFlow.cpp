//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwTest.h"
#include "cwMem.h"
#include "cwText.h"
#include "cwNumericConvert.h"
#include "cwObject.h"
#include "cwTracer.h"

#include "cwAudioFile.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwDspTypes.h" // coeff_t, sample_t, srate_t ...
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwFlowDecl.h"
#include "cwFlow.h"
#include "cwFlowValue.h"
#include "cwFlowTypes.h"
#include "cwFlowNet.h"
#include "cwFlowProc.h"
#include "cwFlowPerf.h"
namespace cw
{
  namespace flow
  {
    typedef struct library_str
    {
      const char*      label;
      class_members_t* members;
    } library_t;
    
    library_t g_library[] = {
      { "user_def_proc",   &user_def_proc::members },
      { "poly",            &poly::members },
      { "midi_in",         &midi_in::members },
      { "midi_out",        &midi_out::members },
      { "audio_in",        &audio_in::members },
      { "audio_out",       &audio_out::members },
      { "audio_file_in",   &audio_file_in::members },
      { "audio_file_out",  &audio_file_out::members },
      { "audio_buf_file_out",  &audio_buf_file_out::members },
      { "audio_gain",      &audio_gain::members },
      { "audio_xfade",     &audio_xfade::members },
      { "audio_split",     &audio_split::members },
      { "audio_duplicate", &audio_duplicate::members },
      { "audio_merge",     &audio_merge::members },
      { "audio_mix",       &audio_mix::members },
      { "audio_silence",   &audio_silence::members },
      { "audio_pass",      &audio_pass::members },
      { "sine_tone",       &sine_tone::members },
      { "pv_analysis",     &pv_analysis::members },
      { "pv_synthesis",    &pv_synthesis::members },
      { "spec_dist",       &spec_dist::members },
      { "compressor",      &compressor::members },
      { "limiter",         &limiter::members },
      { "audio_delay",     &audio_delay::members },
      { "dc_filter",       &dc_filter::members },
      { "balance",         &balance::members },
      { "audio_meter",     &audio_meter::members },
      { "audio_marker",    &audio_marker::members },
      { "xfade_ctl",       &xfade_ctl::members },
      { "poly_voice_ctl",  &poly_voice_ctl::members },
      { "midi_voice",      &midi_voice::members },
      { "piano_voice",     &piano_voice::members },
      { "voice_detector",  &voice_detector::members },
      { "sample_hold",     &sample_hold::members },
      { "number",          &number::members },
      { "label_value_list",&label_value_list::members },
      { "string_list",     &string_list::members },
      { "reg",             &reg::members },
      { "timer",           &timer::members },
      { "counter",         &counter::members },
      { "list",            &list::members },
      { "add",             &add::members },
      { "preset",          &preset::members },
      { "print",           &print::members },
      { "on_start",        &on_start::members },
      { "halt",            &halt::members },
      { "midi_msg",        &midi_msg::members },
      { "make_midi",       &make_midi::members },
      { "midi_select",     &midi_select::members },
      { "midi_split",      &midi_split::members },
      { "midi_file",       &midi_file::members },
      { "recd_list",       &recd_list::members },
      { "recd_route",      &recd_route::members },
      { "recd_merge",      &recd_merge::members },
      { "recd_extract",    &recd_extract::members },
      { "recd_pass",       &recd_pass::members },
      { "midi_merge",      &midi_merge::members },
      { "poly_xform_ctl",  &poly_xform_ctl::members },
      { "gutim_ps_msg_table", &gutim_ps_msg_table::members },
      { "gutim_take_menu", &gutim_take_menu::members },
      { "score_player_ctl",&score_player_ctl::members },
      { "midi_recorder",   &midi_recorder::members },
      { "button_array",    &button_array::members },
      { "score_player",    &score_player::members },
      { "multi_player",    &multi_player::members },
      { "vel_table",       &vel_table::members },
      { "preset_select",   &preset_select::members },
      { "gutim_ps",        &gutim_ps::members },
      { "score_follower",  &score_follower::members },
      { "score_follower_2",&score_follower_2::members },
      { "gutim_ctl",       &gutim_ctl::members },
      { "gutim_sf_ctl",    &gutim_sf_ctl::members },
      { "gutim_spirio_ctl",&gutim_spirio_ctl::members },
      { "gutim_pgm_ctl",   &gutim_pgm_ctl::members },
      { nullptr, nullptr }
    };

    class_members_t* _find_library_record( const char* label )
    {
      for(library_t* l = g_library; l->label != nullptr; ++l)
        if( textCompare(l->label,label) == 0)
          return l->members;

      return nullptr;
    }
      
    flow_t* _handleToPtr(handle_t h)
    { return handleToPtr<handle_t,flow_t>(h); }


    /*
    rc_t _is_var_flag_set( const object_t* var_flags_obj, const char* flag_label, const char* classLabel, const char* varLabel, bool&is_set_flag_ref )
    {
      rc_t rc = kOkRC;
      
      is_set_flag_ref = false;

      if( var_flags_obj != nullptr )
      {
        for(unsigned k=0; k<var_flags_obj->child_count(); ++k)
        {
          const object_t* tag_obj = var_flags_obj->child_ele(k);
          const char* tag = nullptr;
          if( tag_obj != nullptr &&  tag_obj->is_string() && (rc=tag_obj->value(tag))==kOkRC && tag != nullptr )
          {
            if( strcmp(tag,flag_label) == 0 )
              is_set_flag_ref = true;
          }
          else                  
          {
            rc = cwLogError(kSyntaxErrorRC,"An invalid or non-string value was found in a flow class '%s' variable:'%s' 'flags' field.",classLabel,varLabel);
          }
        }
      }
      return rc;
    }
    */
    
    rc_t _parse_udp_var_proxy_string( const char* proxyStr, var_desc_t* var_desc )
    {
      rc_t rc       = kOkRC;

      const char* period;

      // find the separating period
      if((period = firstMatchChar(proxyStr,'.')) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The separating '.' could not be found in the proxy string '%s'.",cwStringNullGuard(proxyStr));
        goto errLabel;
      }

      // validate the length of the proc inst label
      if( period-proxyStr == 0 )
      {
        rc = cwLogError(kSyntaxErrorRC,"No proxy proc instance was found in the proxy string '%s'.",cwStringNullGuard(proxyStr));
        goto errLabel;        
      }

      // validate the length of the var label
      if( textLength(period+1) == 0 )
      {
        rc = cwLogError(kSyntaxErrorRC,"No proxy var was found in the proxy string '%s'.",cwStringNullGuard(proxyStr));
        goto errLabel;        
      }

      
      var_desc->proxyProcLabel = mem::duplStr(proxyStr,period-proxyStr);
      var_desc->proxyVarLabel  = mem::duplStr(period+1);

    errLabel:
      return rc;
    }


    rc_t _parse_class_var_attribute_flags(const object_t* var_flags_obj, unsigned& flags_ref)
    {
      rc_t     rc           = kOkRC;
      unsigned result_flags = 0;
      
      flags_ref = 0;
      
      if( !var_flags_obj->is_list() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The variable description 'flags' field must be a list.");
        goto errLabel;
      }
        
      for(unsigned i=0; i<var_flags_obj->child_count(); ++i)
      {
        const object_t* flag_obj   = var_flags_obj->child_ele(i);
        const char*     flag_label = nullptr;
        unsigned        flag       = 0;

        // validate the flag syntax
        if( flag_obj == nullptr || !flag_obj->is_string() || flag_obj->value(flag_label)!=kOkRC )
        {
          rc = cwLogError(kSyntaxErrorRC,"Invalid variable description flag syntax on flag index %i.",i);
          goto errLabel;
        }

        // parse the flag
        if((flag = var_desc_attr_label_to_flag(flag_label)) == kInvalidVarDescFl )
        {
          rc = cwLogError(kInvalidArgRC,"The variable description flag ('%s') at flag index %i is not valid.",cwStringNullGuard(flag_label),i);
          goto errLabel;
        }

        result_flags |= flag;        
      }

      flags_ref = result_flags;
    errLabel:
      return rc;
      
    }
    
    rc_t _parse_class_var_cfg(flow_t* p, class_desc_t* class_desc, const object_t* var_desc_pair, var_desc_t*& var_desc_ref )
    {
      rc_t            rc                  = kOkRC;      
      const object_t* var_flags_obj       = nullptr;
      const char*     var_value_type_str  = nullptr;
      const char*     var_label           = nullptr;
      const char*     proxy_string        = nullptr;
      var_desc_t*     vd                  = nullptr;

      var_desc_ref = nullptr;

      if(var_desc_pair==nullptr || !var_desc_pair->is_pair() || var_desc_pair->pair_label()==nullptr || (var_label = var_desc_pair->pair_label())==nullptr || var_desc_pair->pair_value()==nullptr || !var_desc_pair->pair_value()->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"An invalid variable description syntax was encountered.");
        goto errLabel;
      }

      // Allocate the var. desc record
      if((rc = var_desc_create( var_label, var_desc_pair->pair_value(), vd)) != kOkRC )
      {
        rc = cwLogError(kObjAllocFailRC,"Variable description allocation failed.");
        goto errLabel;
      }
      
      // get the variable description 
      if((rc = vd->cfg->readv("doc",   kReqFl, vd->docText,
                              "flags", kOptFl, var_flags_obj,
                              "type",  kOptFl, var_value_type_str,
                              "value", kOptFl, vd->val_cfg,
                              "fmt",   kOptFl, vd->fmt_cfg,
                              "ui",    kOptFl, vd->ui_cfg,
                              "proxy", kOptFl, proxy_string,
                              "mult_ref", kOptFl, vd->mult_ref_var_label)) != kOkRC )
      {
        rc = cwLogError(rc,"Parsing optional fields failed.");
        goto errLabel;
      }
      
      // convert the type string to a numeric type flag
      if( var_value_type_str  != nullptr )
      {
        if( (vd->type = value_type_label_to_flag( var_value_type_str )) == kInvalidTId )
        {
          rc = cwLogError(kSyntaxErrorRC,"Invalid variable description type flag: '%s' was encountered.", var_value_type_str );
          goto errLabel;            
        }
      }

      // if this is a 'record' type with a 'fmt' specifier
      if( vd->type & kRBufTFl && vd->fmt_cfg != nullptr )
      {
        if((rc = recd_format_create( vd->fmt.recd_fmt, vd->fmt_cfg )) != kOkRC )
        {
          rc = cwLogError(rc,"The record type associated with the 'fmt' field could not be created.");
          goto errLabel;
        }
      }

      // parse the proxy string into it's two parts: <proc>.<var>
      if( proxy_string != nullptr )
      {
        if((rc = _parse_udp_var_proxy_string( proxy_string, vd )) != kOkRC )
          goto errLabel;
      }

      // parse the var desc attribute flags
      if( var_flags_obj != nullptr )
      {
        vd->flags = 0;
        if((rc = _parse_class_var_attribute_flags(var_flags_obj, vd->flags)) != kOkRC )
          goto errLabel;
      }

      var_desc_ref = vd;

    errLabel:
      if( rc != kOkRC )
      {
        rc = cwLogError(rc,"A variable description create failed on class desc:'%s' var:'%s'.",cwStringNullGuard(class_desc->label),cwStringNullGuard(var_label));
        var_desc_destroy(vd);
      }

      return rc;
    }

    rc_t _create_class_ui_desc( class_desc_t* desc )
    {
      desc->ui = mem::allocZ<ui_proc_desc_t>();
      desc->ui->label = desc->label;
      for(class_preset_t* p0 = desc->presetL; p0!=nullptr; p0=p0->link)
        desc->ui->presetN += 1;

      desc->ui->presetA = mem::allocZ<ui_preset_t>(desc->ui->presetN);

      unsigned i=0;
      for(class_preset_t* p0 = desc->presetL; i<desc->ui->presetN; ++i,p0=p0->link)
      {
        desc->ui->presetA[i].label = p0->label;
        desc->ui->presetA[i].preset_idx = i;
      }

      return kOkRC;
    }

    rc_t _create_preset_list( class_preset_t*& presetL, const object_t* presetD )
    {
      rc_t rc = kOkRC;
      
      // parse the preset dictionary
      if( presetD != nullptr )
      {
        if( !presetD->is_dict() )
        {
          rc = cwLogError(rc,"The preset dictionary is not a dictionary." );
          goto errLabel;                      
        }

        // for each preset in the class desc.
        for(unsigned j=0; j<presetD->child_count(); ++j)
        {
          const object_t* pair = presetD->child_ele(j);

          if( !pair->pair_value()->is_dict() )
          {
            rc = cwLogError(kSyntaxErrorRC,"The preset '%s' is not a dictionary.", cwStringNullGuard(pair->pair_label()));
            goto errLabel;
          }

          class_preset_t* preset =  mem::allocZ< class_preset_t >();
              
          preset->label = pair->pair_label();
          preset->cfg   = pair->pair_value();
          preset->link  = presetL;
          presetL   = preset;
        }
      }

    errLabel:
      return rc;
    }
    
    rc_t  _parse_class_cfg(flow_t* p, const object_t* classCfg)
    {
      rc_t rc = kOkRC;

      if( !classCfg->is_dict() )
        return cwLogError(kSyntaxErrorRC,"The class description dictionary does not have dictionary syntax.");
              
      p->classDescN = classCfg->child_count();
      p->classDescA = mem::allocZ<class_desc_t>( p->classDescN );      

      // for each class description
      for(unsigned i=0; i<p->classDescN; ++i)
      {
        const object_t* class_obj = classCfg->child_ele(i);
        const object_t* varD      = nullptr;
        const object_t* presetD   = nullptr;
        class_desc_t*   cd        = p->classDescA + i;

        cd->cfg    = class_obj->pair_value();
        cd->label  = class_obj->pair_label();
        
        // get the variable description 
        if((rc = cd->cfg->getv_opt("vars",  varD,
                                   "presets", presetD,
                                   "poly_limit_cnt", cd->polyLimitN)) != kOkRC )
        {
          rc = cwLogError(rc,"Parsing failed while parsing class desc:'%s'", cwStringNullGuard(cd->label) );
          goto errLabel;                      
        }

        if((rc = _create_preset_list( cd->presetL, presetD )) != kOkRC )
        {
          rc = cwLogError(rc,"The presets for the class desc: '%s' could not be parsed.",cwStringNullGuard(cd->label));
          goto errLabel;
        }
        

        // create the class descripiton
        if((rc = _create_class_ui_desc(cd)) != kOkRC )
        {          
          cwLogError(rc,"Class desc UI record create failed on '%s'.",cwStringNullGuard(cd->label));
          goto errLabel;
        }

        
        // parse the variable dictionary
        if( varD != nullptr )
        {
          if( !varD->is_dict() )
          {
            rc = cwLogError(rc,"The value dictionary is not a dictionary on class desc:'%s'", cwStringNullGuard(cd->label) );
            goto errLabel;                      
          }
          
          // get the class member functions
          if((cd->members = _find_library_record(cd->label)) == nullptr )
          {
            rc = cwLogError(kSyntaxErrorRC,"The '%s' class member function record could not be found.", cd->label );
            goto errLabel;                    
          }

          // for each class value description
          for(unsigned j=0; j<varD->child_count(); ++j)
          {
            const object_t* var_obj       = varD->child_ele(j);
            var_desc_t* vd = nullptr;
            
            if((rc = _parse_class_var_cfg(p, cd, var_obj, vd )) != kOkRC )
            {
              rc = cwLogError(rc,"Variable description created failed on the class desc '%s' on the variable description at index '%i'.",cwStringNullGuard(cd->label),j);
              goto errLabel;
            }

            if( vd->type == kInvalidTFl )
            {
              rc = cwLogError(rc,"The variable description '%s' in class description '%s' does not have a valid 'type' field.",cwStringNullGuard(vd->label),cwStringNullGuard(cd->label));
              goto errLabel;
            }

            if( vd->proxyProcLabel != nullptr || vd->proxyVarLabel != nullptr )
            {
              cwLogWarning("The 'proxy' field in the variable description '%s' on class description '%s' will be ignored because the variable is not part of a UDP definition.",cwStringNullGuard(vd->label),cwStringNullGuard(cd->label));
            }

            if( cwIsFlag(vd->flags,kUdpOutVarDescFl ) )
            {
              cwLogWarning("The 'out' flag in the variable description '%s' on class description '%s' will be ignored because the variable is not part of a UDP definition.",cwStringNullGuard(vd->label),cwStringNullGuard(cd->label));
            }
            
            vd->link     = cd->varDescL;
            cd->varDescL = vd;
          }
        }
      }

    errLabel:
      return rc;
    }

    rc_t _find_udp_proc_class_desc( flow_t* p, const object_t* udpProcD, const char* procInstLabel, const class_desc_t*& class_desc_ref )
    {
      rc_t                rc          = kOkRC;
      const object_t*     procInstD   = nullptr;
      const object_t*     classStr    = nullptr;
      const char*         class_label = nullptr;

      class_desc_ref = nullptr;
        
      // find the proc inst dict in the UDP
      if((procInstD = udpProcD->find_child(procInstLabel)) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The proc instance '%s' from the proxy var list could not be foud in the UDP.",cwStringNullGuard(procInstLabel));
        goto errLabel;
      }

      // find the proc class label of the proc inst
      if((classStr = procInstD->find_child("class")) == nullptr || (rc = classStr->value(class_label))!=kOkRC)
      {
        rc = cwLogError(kSyntaxErrorRC,"The 'class' field could not be found in the '%s' proc instance record.", cwStringNullGuard(procInstLabel));
        goto errLabel;
      }

      // find the associated class desc record
      if((class_desc_ref = class_desc_find(p,class_label)) == nullptr)
      {
        rc = cwLogError(kEleNotFoundRC,"The class desc record '%s' for the proc instance '%s' could not be found.",class_label,cwStringNullGuard(procInstLabel));
        goto errLabel;
      }

    errLabel:
      
      return rc;
    }


    rc_t _create_udp_var_desc( flow_t* p, class_desc_t* udpClassDesc, const object_t* udpProcD, const object_t* varDescPair, var_desc_t*& vd_ref )
    {
      rc_t                rc               = kOkRC;
      const class_desc_t* proxy_class_desc = nullptr;
      const var_desc_t*   proxy_var_desc   = nullptr;
      var_desc_t*         var_desc         = nullptr;
      
      vd_ref = nullptr;

      // parse the variable descripiton and create a var_desc_t record
      if((rc = _parse_class_var_cfg(p, udpClassDesc, varDescPair, var_desc )) != kOkRC )
      {
        goto errLabel;
      }

      if( var_desc->type != 0 )
      {
        cwLogWarning("The 'type' field int the variable description '%s' on the class description '%s' will be ignored because the variable is proxied.",cwStringNullGuard(udpClassDesc->label),cwStringNullGuard(var_desc->label));
      }

      // verify that a proxy-proc-label and proxy-var-label were specified in the variable descripiton
      if( var_desc->proxyProcLabel == nullptr || var_desc->proxyVarLabel == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The UDP variable description '%s' in the UDP '%s' must have a valid 'proxy' field.",cwStringNullGuard(var_desc->label),cwStringNullGuard(udpClassDesc->label));
        goto errLabel;
      }
      
      // locate the class desc associated with proxy proc
      if((rc = _find_udp_proc_class_desc( p, udpProcD, var_desc->proxyProcLabel, proxy_class_desc )) != kOkRC )
      {
        goto errLabel;
      }

      // locate the var desc associated with the proxy proc var
      if((proxy_var_desc = var_desc_find( proxy_class_desc, var_desc->proxyVarLabel)) == nullptr )
      {
        rc = cwLogError(kEleNotFoundRC,"The UDP proxied variable desc '%s.%s' could not be found in UDP '%s'.",cwStringNullGuard(var_desc->proxyProcLabel),cwStringNullGuard(var_desc->proxyVarLabel),cwStringNullGuard(udpClassDesc->label));
        goto errLabel;                        
      }

      // get the UDP var_desc type from the proxied var_desc
      var_desc->type  = proxy_var_desc->type;

      // augment the udp var_desc flags from the proxied var_desc
      var_desc->flags |= proxy_var_desc->flags;

      // if no default value was given to the UDP var desc then get it from the proxied var desc
      if( var_desc->val_cfg == nullptr )
        var_desc->val_cfg = proxy_var_desc->val_cfg;

      vd_ref = var_desc;

    errLabel:

      if( rc != kOkRC )
      {        
        rc = cwLogError(rc,"The creation of proxy var '%s' failed.",var_desc==nullptr ? "<unknown>" : cwStringNullGuard(var_desc->label));
        var_desc_destroy(var_desc);
      }
      
      return rc;
    }
    
    rc_t _parse_udp_vars( flow_t* p, class_desc_t* class_desc, const object_t* udpProcD, const object_t* varD )
    {
      rc_t rc = kOkRC;

      unsigned varN = 0;
      
      if( !varD->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The proxy variable dictionary is invalid.");
        goto errLabel;
      }

      varN = varD->child_count();

      // Fill the class_Desc.varDescL list from the UDP 'vars' dictioanry
      for(unsigned i=0; i<varN; ++i)
      {
        const object_t* child_pair = varD->child_ele(i);
        var_desc_t*     var_desc   = nullptr;

        if((rc = _create_udp_var_desc( p, class_desc, udpProcD, child_pair, var_desc )) != kOkRC )
          goto errLabel;


        var_desc->link = class_desc->varDescL;
        class_desc->varDescL = var_desc;

        //printf("Wrapper var-desc created: %i of %i : %s:%s proxy:%s:%s flags:%i.\n", i, varN, class_desc->label, var_desc->label, var_desc->proxyProcLabel,var_desc->proxyVarLabel,var_desc->flags);
        
      }

    errLabel:
      return rc;
    }

    rc_t _create_udp_class_desc( flow_t* p, const object_t* class_obj, class_desc_t* class_desc )
    {
      rc_t            rc                  = kOkRC;
      const object_t* varD                = nullptr;
      const object_t* udpD             = nullptr;
      const object_t* udpProcD         = nullptr;
      const object_t* udpPresetD       = nullptr;          
      const char*     udpProcDescLabel = nullptr;

      // Validate the UDP proc desc label and value
      if( class_obj==nullptr || !class_obj->is_pair() || class_obj->pair_value()==nullptr || !class_obj->pair_value()->is_dict() || (udpProcDescLabel = class_obj->pair_label()) == nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"An invalid UDP description '%s' was encountered.",cwStringNullGuard(udpProcDescLabel));
        goto errLabel;
      }

      // verify that another UDP with the same name does not already exist
      if( class_desc_find(p,udpProcDescLabel) != nullptr )
      {
        rc = cwLogError(kInvalidStateRC,"A UDP named '%s' already exists.",udpProcDescLabel);
        goto errLabel;
      }

      class_desc->cfg    = class_obj->pair_value();
      class_desc->label  = class_obj->pair_label();

      // get the 'UDP' members record
      if((class_desc->members = _find_library_record("user_def_proc")) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The 'UDP' class member function record could not be found." );
        goto errLabel;                    
      }
                
      // get the variable description 
      if((rc = class_desc->cfg->getv_opt("vars",  varD,
                                         "network", udpD)) != kOkRC )
      {
        rc = cwLogError(rc,"Parse failed while parsing UDP desc:'%s'", cwStringNullGuard(class_desc->label) );
        goto errLabel;                      
      }

      // get the UDP proc and preset dictionaries
      if((rc = udpD->getv("procs",   udpProcD,
                             "presets", udpPresetD)) != kOkRC )
      {
        rc = cwLogError(rc,"Parse failed on the 'network' element.");
        goto errLabel;
      }

      // fill class_desc.varDescL from the UDP vars dictionary
      if((rc = _parse_udp_vars( p, class_desc, udpProcD, varD )) != kOkRC )
      {
        rc = cwLogError(rc,"UDP 'vars' processing failed.");
        goto errLabel;          
      }

      
    errLabel:
      if(rc != kOkRC )
        rc = cwLogError(rc,"'proc' class description creation failed for the UDP '%s'. ",cwStringNullGuard(udpProcDescLabel));
        
      return rc;
    }
        
  
    rc_t  _parse_udp_cfg(flow_t* p, const object_t* udpCfg)
    {
      rc_t rc = kOkRC;
      
      if( !udpCfg->is_dict() )
        return cwLogError(kSyntaxErrorRC,"The UDP class description dictionary does not have dictionary syntax.");
              
      unsigned udpDescN = udpCfg->child_count();
      p->udpDescA = mem::allocZ<class_desc_t>( udpDescN );      

      // for each UDP description
      for(unsigned i=0; i<udpDescN; ++i)
      {
        const object_t* udp_obj = udpCfg->child_ele(i);

        if((rc = _create_udp_class_desc(p, udp_obj, p->udpDescA + i )) != kOkRC )
        {
          rc = cwLogError(rc,"UDP class description create failed on the UDP at index:%i.",i);
          goto errLabel;
        }

        // We have to update the size of the UDP class array
        // as we go because we may want be able to search p->udpDescA[]
        // aand to do that we must now the current length.
        p->udpDescN += 1;
      }

      assert( udpDescN == p->udpDescN );
    errLabel:

      if( rc != kOkRC )
      {
        rc = cwLogError(rc,"UDP processing failed.");
        
      }
      
      return rc;

    }

    rc_t _parse_preset_array(flow_t* p, const object_t* netCfg )
    {
      rc_t            rc           = kOkRC;
      unsigned        presetAllocN = 0;
      const object_t* presetD      = nullptr;

      if((rc = netCfg->getv_opt("presets",presetD)) != kOkRC )
      {
        rc = cwLogError(rc,"An error ocurred while locating the network 'presets' configuration.");
        goto errLabel;
      }

      // if this network does not have any presets
      if( presetD == nullptr )
        return rc;

      // the 'preset' cfg must be a dictionary
      if( !presetD->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The network preset list is not a dictionary.");
        goto errLabel;
      }

      presetAllocN = presetD->child_count();
      p->presetA  = mem::allocZ<network_preset_t>(presetAllocN);
      p->presetN  = 0;

      // parse each preset_label pair
      for(unsigned i=0; i<presetAllocN; ++i)
      {
        const object_t* preset_pair_cfg   = presetD->child_ele(i);
        network_preset_t&  network_preset = p->presetA[ p->presetN ];
          
        // validate the network preset pair
        if( preset_pair_cfg==nullptr || !preset_pair_cfg->is_pair() || preset_pair_cfg->pair_label()==nullptr || preset_pair_cfg->pair_value()==nullptr )
        {
          rc = cwLogError(kSyntaxErrorRC,"Invalid syntax encountered on a network preset.");
          goto errLabel;
        }

        // get the preset type id
        switch( preset_pair_cfg->pair_value()->type_id() )
        {
          case kDictTId: // 'value-list' preset
            network_preset.tid = kPresetVListTId;
            break;

          case kListTId: // dual preset
            network_preset.tid = kPresetDualTId;
            break;

          default:
            rc = cwLogError(kAssertFailRC,"Unknown preset type on network preset: '%s'.",cwStringNullGuard(network_preset.label));
            goto errLabel;
        }

        network_preset.label    = preset_pair_cfg->pair_label();
        
        p->presetN += 1;
      }
      
    errLabel:
      if(rc != kOkRC )
      {
        mem::release(p->presetA);
        p->presetN = 0;
      }

      return rc;      
    }

    void _release_class_desc_array( class_desc_t*& classDescA, unsigned classDescN )
    {
      // release the class records
      for(unsigned i=0; i<classDescN; ++i)
      {
        class_desc_t* cd  = classDescA + i;
        class_desc_destroy(cd);
      }

      mem::release(classDescA);      
    }
    
    rc_t _destroy( flow_t*& p)
    {
      rc_t rc = kOkRC;

      if( p == nullptr )
        return rc;

      network_destroy(p->net);

      global_var_t* gv=p->globalVarL;
      while( gv != nullptr )
      {
        global_var_t* gv0 = gv->link;
        mem::release(gv->var_label);
        mem::release(gv->blob);
        mem::release(gv);
        gv = gv0;
      }

      
      
      _release_class_desc_array(p->classDescA,p->classDescN);
      _release_class_desc_array(p->udpDescA,p->udpDescN);
      mem::release(p->presetA);
      p->presetN = 0;
      p->classDescN = 0;
      p->udpDescN = 0;
      
      mem::release(p);
      
      return rc;
    }

    void _make_flow_to_ui_callback( flow_t* p )
    {
      
      // There is no concurrent contention for the linked list when
      // this function is called and so all accesses use relaxed memory order.

      // Get the first variable to send to the UI
      variable_t* var = p->ui_var_tail->ui_var_link.load(std::memory_order_relaxed);
      
      while( var!=nullptr)
      {
        // Send the var to the UI
        if( p->ui_callback != nullptr )
          p->ui_callback( p->ui_callback_arg, var->ui_var );

        // Get the next var to send to the UI
        variable_t* var0 = var->ui_var_link.load(std::memory_order_relaxed);

        // Nullify the list links as they are used
        var->ui_var_link.store(nullptr,std::memory_order_relaxed);
        
        var = var0;
      }

      // Empty the UI message list.
      p->ui_var_head.store(&p->ui_var_stub,std::memory_order_relaxed);
      p->ui_var_tail    = &p->ui_var_stub;
      p->ui_var_stub.ui_var_link.store(nullptr,std::memory_order_relaxed);
      
    }
    
  }
}

void cw::flow::print_abuf( const abuf_t* abuf )
{
  printf("Abuf: sr:%7.1f chs:%3i frameN:%4i %p",abuf->srate,abuf->chN,abuf->frameN,abuf->buf);
}

void cw::flow::print_external_device( const external_device_t* dev )
{
  cwLogPrint("Dev: %10s type:%3i fl:0x%x : ", cwStringNullGuard(dev->devLabel),dev->typeId,dev->flags);
  if( dev->typeId == kAudioDevTypeId )
    print_abuf(dev->u.a.abuf);
  cwLogPrint("\n");
}



cw::rc_t cw::flow::create( handle_t&          hRef,
                           const object_t*    classCfg,
                           const object_t*    pgmCfg,
                           const object_t*    udpCfg,
                           const char*        proj_dir,
                           ui_callback_t      ui_callback,
                           void*              ui_callback_arg)
{
  rc_t            rc               = kOkRC;
  bool            printClassDictFl = false;
  unsigned        maxCycleCount    = kInvalidCnt;
  double          durLimitSecs     = 0;
  unsigned        uiUpdateMs       = 50;
  
  if(( rc = destroy(hRef)) != kOkRC )
    return rc;

  flow_t* p   = mem::allocZ<flow_t>();

  // parse the class description array
  if((rc = _parse_class_cfg(p,classCfg)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the class description list.");
    goto errLabel;    
  }

  // parse the UDP descriptions
  if( udpCfg != nullptr )
    if((rc = _parse_udp_cfg(p,udpCfg)) != kOkRC )
    {
      rc = cwLogError(kSyntaxErrorRC,"Error parsing the UDP list.");
      goto errLabel;          
    }


  p->pgmCfg             = pgmCfg;
  p->framesPerCycle     = kDefaultFramesPerCycle;
  p->sample_rate        = kDefaultSampleRate;
  p->maxCycleCount      = kInvalidCnt;
  p->uiUpdateCycleCount = 1;
  p->proj_dir           = proj_dir;
  p->printLogHdrFl      = true;
  p->ui_create_fl       = false;
  p->prof_fl            = false;
  p->ui_callback        = ui_callback;
  p->ui_callback_arg    = ui_callback_arg;
  p->ui_var_head.store(&p->ui_var_stub);
  p->ui_var_tail        = &p->ui_var_stub;
  
  // parse the optional args
  if((rc = pgmCfg->readv("network",              0,      p->networkCfg,
                         "non_real_time_fl",     kOptFl, p->non_real_time_fl,
                         "frames_per_cycle",     kOptFl, p->framesPerCycle,
                         "sample_rate",          kOptFl, p->sample_rate,
                         "max_cycle_count",      kOptFl, maxCycleCount,
                         "dur_limit_secs",       kOptFl, durLimitSecs,
                         "ui_update_ms",         kOptFl, uiUpdateMs,
                         "ui_create_fl",         kOptFl, p->ui_create_fl,
                         "profile_fl",           kOptFl, p->prof_fl,
                         "preset",               kOptFl, p->init_net_preset_label,
                         "print_class_dict_fl",  kOptFl, printClassDictFl,
                         "print_network_fl",     kOptFl, p->printNetworkFl,
                         "multiPriPresetProbFl", kOptFl, p->multiPriPresetProbFl,
                         "multiSecPresetProbFl", kOptFl, p->multiSecPresetProbFl,
                         "multiPresetInterpFl",  kOptFl, p->multiPresetInterpFl)) != kOkRC )
  {
    rc = cwLogError(kSyntaxErrorRC,"Error parsing the network system parameters.");
    goto errLabel;
  }

  if((rc = _parse_preset_array(p, p->networkCfg )) != kOkRC )
  {
    rc = cwLogError(rc,"Preset dictionary parsing failed.");
    goto errLabel;
  }

  // Validate the sample rate.
  if( p->sample_rate <= 0 )
  {
    cwLogInfo("An invalid sample rate:%d was encountered. Setting sample rate to %d.",p->sample_rate,kDefaultSampleRate);
    p->sample_rate = kDefaultSampleRate;
  }

  if( p->framesPerCycle <= 0 )
  {
    cwLogInfo("An invalid frames/cycle:%i was encountered. Setting frames/cycle to %i.",p->framesPerCycle,kDefaultFramesPerCycle);
    p->framesPerCycle = kDefaultFramesPerCycle;
  }
  
  // if a maxCycle count was given
  if( maxCycleCount != kInvalidCnt )
    p->maxCycleCount = maxCycleCount;
  else
  {
    // if a durLimitSecs was given - use it to setMaxCycleCount
    if( durLimitSecs != 0.0 )
      p->maxCycleCount = (unsigned)((durLimitSecs * p->sample_rate) / p->framesPerCycle);    
  }

  
  p->uiUpdateCycleCount = std::max(1U, (unsigned)(((uiUpdateMs * p->sample_rate) / 1000.0) / p->framesPerCycle));


  // print the class dict
  if( printClassDictFl )
      class_dict_print( p );
  
  hRef.set(p);

  TRACE_REG("flow",0,p->trace_id);
  
 errLabel:
  
  if( rc != kOkRC )
    _destroy(p);
  
  return rc;  
}

bool cw::flow::is_non_real_time( handle_t h )
{
  flow_t* p = _handleToPtr(h);
  return p->non_real_time_fl;
}

double   cw::flow::sample_rate(      handle_t h )
{
  flow_t* p = _handleToPtr(h);
  return p->sample_rate;
}

unsigned cw::flow::frames_per_cycle( handle_t h )
{
  flow_t* p = _handleToPtr(h);
  return p->framesPerCycle;  
}

unsigned cw::flow::preset_count( handle_t h )
{
  flow_t* p = _handleToPtr(h);
  return p->presetN;
}

const char* cw::flow::preset_label( handle_t h, unsigned preset_idx )
{
  flow_t* p = _handleToPtr(h);
  
  if( preset_idx >= p->presetN )
  {
    cwLogError(kInvalidArgRC,"The preset index %i is invalid.",preset_idx);
    return nullptr;
  }
  
  return p->presetA[ preset_idx ].label;
}


unsigned cw::flow::preset_cfg_flags( handle_t h )
{
  flow_t*  p     = _handleToPtr(h);
  unsigned flags = 0;
  
  if( p->multiPriPresetProbFl )
    flags |= kPriPresetProbFl;
  
  if( p->multiSecPresetProbFl )
    flags |= kSecPresetProbFl;
  
  if( p->multiPresetInterpFl )
    flags |= kInterpPresetFl;

  return flags;
}

cw::rc_t cw::flow::initialize( handle_t h,
                               external_device_t* deviceA,
                               unsigned           deviceN,
                               unsigned           preset_idx  )
{
  rc_t        rc        = kOkRC;
  variable_t* proxyVarL = nullptr;
  flow_t*     p         = _handleToPtr(h);
  const char* root_label = "root";
  
  p->deviceA    = deviceA;
  p->deviceN    = deviceN;
  
  for(unsigned i=0; i<deviceN; ++i)
    if( deviceA[i].typeId == kAudioDevTypeId )
    {
      if( deviceA[i].u.a.abuf == NULL )
      {
        rc = cwLogError(kInvalidArgRC,"The audio '%s' device does not have a valid audio buffer.",cwStringNullGuard(deviceA[i].devLabel));
        goto errLabel;
      }
      else
        if( deviceA[i].u.a.abuf->frameN != p->framesPerCycle )
          cwLogWarning("The audio frame count (%i) for audio device '%s' does not match the Flow framesPerCycle (%i).",deviceA[i].u.a.abuf->frameN,p->framesPerCycle);
    }

  // if an initialization preset was given
  if( preset_idx != kInvalidIdx )
  {
    const char* preset_label_str;
    if((preset_label_str = preset_label(h,preset_idx)) == nullptr )
    {
      cwLogError(kInvalidArgRC,"The preset index '%i' could not be resolved to an existing preset.");
      goto errLabel;
    }

    // override the program assigned 'preset' 
    p->init_net_preset_label = preset_label_str;
  }
  
  // instantiate the network
  if((rc = network_create(p,&root_label,&p->networkCfg,1,proxyVarL,1,p->net)) != kOkRC )
  {
    rc = cwLogError(rc,"Network creation failed.");
    goto errLabel;
  }

  // if a network preset was specified apply it here
  if( p->init_net_preset_label != nullptr && p->net != nullptr )
    network_apply_preset(*p->net,p->init_net_preset_label);


  if( p->printNetworkFl && p->net != nullptr )
    network_print(*p->net);

  // The network preset may have been applied as each proc. was instantiated.
  // This way any 'init' only preset values will have been applied and the
  // proc's custom create is more likely to see the values from the preset.
  // Now that the network is fully instantiated however we will apply it again
  // to be sure that the final state of the network is determined by selected preset.
  if( p->init_net_preset_label != nullptr && p->net != nullptr )
    network_apply_preset( *p->net, p->init_net_preset_label );

  // form the UI description
  if((rc = create_net_ui_desc(p)) != kOkRC )
  {
    rc = cwLogError(rc,"UI description formation failed.");
    goto errLabel;
  }
  
  p->isInRuntimeFl = true;
  cwLogInfo("Entering runtime.");

 errLabel:
  
  return rc;   
}


cw::rc_t cw::flow::destroy( handle_t& hRef )
{
  rc_t    rc = kOkRC;
  flow_t* p  = nullptr;;
  
  if( !hRef.isValid() )
    return rc;

  p = _handleToPtr(hRef);

  if( p->prof_fl )
    profile_report(hRef);

  _destroy(p);

  hRef.clear();
  
  return rc;
}


const cw::flow::ui_net_t* cw::flow::ui_net( handle_t h )
{
  flow_t* p  = _handleToPtr(h);

  if( p->net == nullptr )
  {
    cwLogError(kInvalidStateRC,"No UI net exists because the no net exists.");
    return nullptr;
  }
  
  if( p->net->ui_net == nullptr )
    return nullptr;

  return p->net->ui_net;  
}

cw::rc_t cw::flow::exec_cycle( handle_t h )
{
  rc_t    rc = kOkRC;;
  flow_t* p  = _handleToPtr(h);
  
  TRACE_TIME(p->trace_id,tracer::kBegEvtId,p->cycleIndex,0);

  if( p->cycleIndex == 0 )
    mem::set_warn_on_alloc();

  
  if( p->maxCycleCount!=kInvalidCnt && p->cycleIndex >= p->maxCycleCount )
  {
    rc = kEofRC;
    cwLogInfo("'maxCycleCnt' reached: %i. Shutting down flow.",p->maxCycleCount);
  }
  else
  {
    
    rc = exec_cycle(*p->net);
    
    // Execute one cycle of the network
    if(rc == kOkRC )
    {
      time::spec_t t0;
      if( p->prof_fl )
          time::get(t0);

      // is it time to update the UI?
      if( p->uiUpdateCycleIndex >= p->uiUpdateCycleCount )
      {
        // During network execution variables which need to update the UI
        // are collected in a linked list based on p->ui_var_tail.
        // Callback to the UI with those variables here.
        _make_flow_to_ui_callback(p);
        
        p->uiUpdateCycleIndex = 0;
      }
      
      if( p->prof_fl )
        time::accumulate_elapsed_current(p->prof_ui_dur,t0);
    
    }
    
    p->cycleIndex += 1;
    p->uiUpdateCycleIndex += 1;
    
  }  

  TRACE_TIME(p->trace_id,tracer::kEndEvtId,p->cycleIndex-1,0);

  if( rc == kEofRC )
    mem::clear_warn_on_alloc();
    
  return rc;
}

cw::rc_t cw::flow::exec(    handle_t h )
{
  rc_t    rc = kOkRC;
  flow_t* p  = _handleToPtr(h);
  time::spec_t t0 = time::current_time();

  while( rc == kOkRC )
    rc = exec_cycle(h);

  unsigned dms = time::elapsedMs(t0);

  cwLogInfo("Exec time: %i ms Cycles:%i",dms,p->cycleIndex);
  
  return rc;
}

cw::rc_t cw::flow::send_ui_updates( handle_t h )
{
  rc_t    rc = kOkRC;
  flow_t* p  = _handleToPtr(h);
  
  _make_flow_to_ui_callback(p);

  return rc;
}


cw::rc_t cw::flow::apply_preset( handle_t h, const char* presetLabel )
{
  flow_t* p  = _handleToPtr(h);
  return network_apply_preset(*p->net,presetLabel);
}

cw::rc_t cw::flow::apply_dual_preset( handle_t h, const char* presetLabel_0, const char* presetLabel_1, double coeff )
{
  flow_t* p  = _handleToPtr(h);
  return network_apply_dual_preset(*p->net,presetLabel_0, presetLabel_1, coeff );
}

cw::rc_t cw::flow::apply_preset( handle_t h, const multi_preset_selector_t& mps )
{
  flow_t* p  = _handleToPtr(h);
  return network_apply_preset(*p->net,mps);
}

cw::rc_t cw::flow::set_variable_user_arg( handle_t h, const ui_var_t* ui_var, void* arg )
{ return set_variable_user_arg( *_handleToPtr(h)->net, ui_var, arg );  }


cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, bool value )
{ return set_variable_value( *_handleToPtr(h)->net, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, int value )
{ return set_variable_value( *_handleToPtr(h)->net, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, unsigned value )
{ return set_variable_value( *_handleToPtr(h)->net, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, float value )
{ return set_variable_value( *_handleToPtr(h)->net, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::set_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, double value )
{ return set_variable_value( *_handleToPtr(h)->net, inst_label, var_label, chIdx, value ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, bool& valueRef )
{ return get_variable_value( *_handleToPtr(h)->net, inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, int& valueRef )
{ return get_variable_value( *_handleToPtr(h)->net, inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, unsigned& valueRef )
{ return get_variable_value( *_handleToPtr(h)->net, inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, float& valueRef )
{ return get_variable_value( *_handleToPtr(h)->net, inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::flow::get_variable_value( handle_t h, const char* inst_label, const char* var_label, unsigned chIdx, double& valueRef )
{ return get_variable_value( *_handleToPtr(h)->net, inst_label, var_label, chIdx, valueRef ); }

cw::rc_t cw::flow::set_variable_value( handle_t h, const ui_var_t* ui_var, bool value     )
{ return set_variable_value( *_handleToPtr(h)->net, ui_var, value );  }
cw::rc_t cw::flow::set_variable_value( handle_t h, const ui_var_t* ui_var, int value      )
{ return set_variable_value( *_handleToPtr(h)->net, ui_var, value );  }
cw::rc_t cw::flow::set_variable_value( handle_t h, const ui_var_t* ui_var, unsigned value )
{ return set_variable_value( *_handleToPtr(h)->net, ui_var, value );  }
cw::rc_t cw::flow::set_variable_value( handle_t h, const ui_var_t* ui_var, float value    )
{ return set_variable_value( *_handleToPtr(h)->net, ui_var, value );  }
cw::rc_t cw::flow::set_variable_value( handle_t h, const ui_var_t* ui_var, double value   )
{ return set_variable_value( *_handleToPtr(h)->net, ui_var, value );  }
cw::rc_t cw::flow::set_variable_value( handle_t h, const ui_var_t* ui_var, const char* value   )
{ return set_variable_value( *_handleToPtr(h)->net, ui_var, value );  }

cw::rc_t cw::flow::get_variable_value( handle_t h, const ui_var_t* ui_var, bool& value_ref     )
{ return get_variable_value( *_handleToPtr(h)->net, ui_var, value_ref );  }
cw::rc_t cw::flow::get_variable_value( handle_t h, const ui_var_t* ui_var, int& value_ref      )
{ return get_variable_value( *_handleToPtr(h)->net, ui_var, value_ref );  }
cw::rc_t cw::flow::get_variable_value( handle_t h, const ui_var_t* ui_var, unsigned& value_ref )
{ return get_variable_value( *_handleToPtr(h)->net, ui_var, value_ref );  }
cw::rc_t cw::flow::get_variable_value( handle_t h, const ui_var_t* ui_var, float& value_ref    )
{ return get_variable_value( *_handleToPtr(h)->net, ui_var, value_ref );  }
cw::rc_t cw::flow::get_variable_value( handle_t h, const ui_var_t* ui_var, double& value_ref   )
{ return get_variable_value( *_handleToPtr(h)->net, ui_var, value_ref );  }
cw::rc_t cw::flow::get_variable_value( handle_t h, const ui_var_t* ui_var, const char*& value_ref   )
{ return get_variable_value( *_handleToPtr(h)->net, ui_var, value_ref );  }


void cw::flow::print_class_list( handle_t h )
{
  flow_t* p = _handleToPtr(h);
  class_dict_print(p);
}

void cw::flow::print_network( handle_t h )
{
  flow_t* p = _handleToPtr(h);
  
  for(unsigned i=0; i<p->deviceN; ++i)
    print_external_device( p->deviceA + i );
  
  network_print(*p->net);
}





void cw::flow::profile_report( handle_t h )
{
  flow_t* p = _handleToPtr(h);
  if( p->net != nullptr )
  {
    if( p->prof_fl )
    {
      double dur = time::seconds(p->prof_dur);
      double ui_dur = time::seconds(p->prof_ui_dur);
      printf("%8.5fs UI:%8.5fs\n",dur,ui_dur);
    
      network_profile_report(*p->net);
    }
  }
}
