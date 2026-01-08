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
#undef cwTRACER
#include "cwTracer.h"

#include "cwAudioFile.h"
#include "cwVectOps.h"
#include "cwMtx.h"
#include "cwDspTypes.h" // real_t, sample_t
#include "cwTime.h"
#include "cwMidiDecls.h"
#include "cwFlowDecl.h"
#include "cwFlowValue.h"
#include "cwFlowTypes.h"
#include "cwFlowNet.h"
#include "cwFlowProc.h"


namespace cw
{
  namespace flow
  {

    
    
    //=======================================================================================================
    //
    // network creation
    //
    
    typedef enum {
      kLocalProcTypeId  = 0x01,
      kLocalVarTypeId   = 0x02,
      kRemoteProcTypeId = 0x04,
      kRemoteVarTypeId  = 0x08
    } io_ele_type_id_t;
    
    typedef struct io_ele_str
    {
      io_ele_type_id_t typeId;       // See k???TypeId above
      char*            label;        // label of in or src id
      unsigned         base_sfx_id;  // Literal base_sfx_id or kInvalidId if the base_sfx_id was not given or 'is_iter_fl' is false
      unsigned         sfx_id;       // 'sfx_id' is only used by _io_stmt_connect_vars()
      unsigned         sfx_id_count; // Literal sfx_id_count or kInvalidCnt if not given
      unsigned         is_iter_fl;   // This id included an '_' (underscore)
      unsigned         has_sfx_fl;   // true if a suffix was specified (differentiates label0 from label)
    } io_ele_t;

    typedef struct io_stmt_str
    {
      io_ele_t    in_proc_ele;      // in-proc element
      io_ele_t    in_var_ele;       // in-var element
      io_ele_t    src_proc_ele;     // src-proc element
      io_ele_t    src_var_ele;      // src-var element

      io_ele_t*   local_proc_ele;
      io_ele_t*   local_var_ele;

      char*       remote_net_label;
      network_t*  remote_net;
      io_ele_t*   remote_proc_ele;
      io_ele_t*   remote_var_ele;

      bool        local_proc_iter_fl;  // Is the local proc iterating (this is a poly iteration rather than a var iterator)      
      
      const io_ele_t* iter_cnt_ctl_ele; // Pointer to the ele which is controlling the iteration count (or null if in-var is non-iterating)
      unsigned        iter_cnt;         // Count of iterations or 0 if in-var is non-iterating.


      // in_stmt only fields
      var_desc_t* local_var_desc;   // Pointer to the in-var var_desc.
      bool        in_create_fl;     // True if the in_var needs to be created with an sfx_id, false create the var by the default process (w/o sfx_id)

      
    } io_stmt_t;
    
    typedef struct proc_inst_parse_statestr
    {
      char*           proc_label;        //
      unsigned        proc_label_sfx_id; //
      const char*     proc_clas_label;   //
      const object_t* preset_labels;     //
      const object_t* presets_dict;
      const object_t* arg_cfg;           //
      const object_t* log_labels;        //
      
      const object_t* in_dict_cfg;  // cfg. node to the in-list
      io_stmt_t*      iStmtA;
      unsigned        iStmtN;

      const object_t* out_dict_cfg; // cfg. node to the out-list
      io_stmt_t*      oStmtA;
      unsigned        oStmtN;

      const object_t* ui_cfg; // cfg. node to ui
      
    } proc_inst_parse_state_t;

    void _preset_value_destroy( preset_value_t* v )
    {
      mem::release(v);
    }
    
    void _network_preset_destroy(network_preset_t& network_preset)
    {
      if( network_preset.tid == kPresetVListTId )
      {
        preset_value_t* pv = network_preset.u.vlist.value_head;
        while( pv != nullptr )
        {
          preset_value_t* pv0 = pv->link;
          _preset_value_destroy(pv);
          pv = pv0;
        }
      
        network_preset.u.vlist.value_head = nullptr;
        network_preset.u.vlist.value_tail = nullptr;
      }
      
    }
    
    void _network_preset_array_destroy( network_t& net )
    {
      for(unsigned i=0; i<net.presetN; ++i)
        _network_preset_destroy( net.presetA[i] );
      mem::release(net.presetA);
      net.presetN=0;
    }

    rc_t _destroy_ui_net( ui_net_t*& ui_net)
    {
      rc_t rc = kOkRC;
      if( ui_net == nullptr )
        return rc;

      for(unsigned i=0; i<ui_net->procN; ++i)
      {
        ui_net_t* iun = ui_net->procA[i].internal_net;
        while( iun!=nullptr )
        {
          ui_net_t* iun0 = iun->poly_link;
          _destroy_ui_net(iun);
          iun = iun0;
        }
          
        mem::release(ui_net->procA[i].varA);

      }

      mem::release(ui_net->procA);
      mem::release(ui_net->presetA);
      mem::release(ui_net);
      return rc;
    }

    
    rc_t _network_destroy_one( network_t*& net )
    {
      rc_t rc = kOkRC;

      if( net == nullptr )
        return rc;
      
      for(unsigned i=0; i<net->procN; ++i)
        proc_destroy(net->procA[i]);

      mem::release(net->procA);
      net->procN = 0;

      _network_preset_array_destroy(*net);

      mem::release(net->preset_pairA);
      net->preset_pairN = 0;

      mem::release(net->recdFmtRegA);
      net->recdFmtRegN = 0;

      _destroy_ui_net(net->ui_net);
      
      mem::release(net);

      return rc;      
    }
    
    rc_t _network_destroy( network_t*& net )
    {
      rc_t rc = kOkRC;

      while( net != nullptr )
      {
        network_t* n0 = net->poly_link;
        rc_t rc0;
        if((rc0 = _network_destroy_one(net)) != kOkRC )
          rc = cwLogError(rc0,"A network destroy failed.");
        net = n0;
      }
      
      return rc;
    }
    
    bool _is_non_null_pair( const object_t* cfg )
    { return cfg != nullptr && cfg->is_pair() && cfg->pair_label()!=nullptr && cfg->pair_value()!=nullptr; }

    // Get the count of digits at the end of a string.
    unsigned _digit_suffix_char_count( const char* s )
    {
      unsigned digitN = 0;
      unsigned sn = textLength(s);
      if( sn==0 )
        return 0;
      
      const char* s0 = s + (textLength(s)-1);
      
      // go backward from the last char until the begin-of-string or a non-digit is found
      for(; s0>=s; --s0)
      {
        if(!isdigit(*s0) )
          break;
        ++digitN;
      }

      return digitN;      
    }

    rc_t _io_stmt_parse_proc_var_string( char* str, const char*& in_proc_label, const char*& in_var_label )
    {
      rc_t  rc     = kOkRC;
      char* period = nullptr;

      if((period = firstMatchChar(str,'.')) == nullptr )
      {
        in_proc_label = nullptr;
        in_var_label = str;
      }
      else
      {
        *period = '\0';
        in_proc_label = str;
        in_var_label = period + 1;
      }
      
      return rc;
    }

    rc_t _io_stmt_parse_net_proc_var_string( char* str, char*& src_net_label, const char*& src_proc_label, const char*& src_var_label )
    {
      rc_t rc = kOkRC;
      char* period0 = nullptr;
      char* period1 = nullptr;
      
      // locate the separator period on the src proc/var id
      if((period0 = firstMatchChar(str,'.')) == nullptr )
      {
        cwLogError(kSyntaxErrorRC,"No period separator was found in the src net/proc/var for the src specifier:%s.",str);
        goto errLabel;
      }

      *period0 = 0;
      
      if((period1 = firstMatchChar(period0+1,'.')) != nullptr )
      {
        *period1 = 0;
        src_var_label = period1 + 1;  // Set a pointer to the src var label
        src_proc_label = period0 + 1;
        src_net_label = mem::duplStr(str);
      }
      else
      {
        src_var_label = period0 + 1;
        src_proc_label = str;
        src_net_label = nullptr;
      }

      if( textLength(src_var_label) == 0 )
        rc = cwLogError(kSyntaxErrorRC,"The 'src-var' label has length 0.");

      if( textLength(src_proc_label) == 0 )
        rc = cwLogError(kSyntaxErrorRC,"The 'src-proc' label has length 0.");


    errLabel:
      return rc;
    }

    
    rc_t _io_stmt_parse_ele( const char* id_str, io_ele_t& r, bool inProcFl=false  )
    {
      rc_t rc = kOkRC;
      unsigned bufN;

      r.base_sfx_id  = kInvalidId;
      r.sfx_id_count = kInvalidCnt;
      
      if((bufN = textLength(id_str)) == 0 )
      {
        if( !inProcFl )         
          rc = cwLogError(kSyntaxErrorRC,"A blank id string was encountered.");
        goto errLabel;
      }
      else
      {
        char* underscore = nullptr;
        char* digit      = nullptr;
        int   offs       = inProcFl ? 1 : 0; 
        char  buf[ bufN+(1+offs) ];

        // in-proc's don't have a leading label - add one here to please the parser
        if(inProcFl)
          buf[0] = 'x';

        // copy the id string into a non-const scratch buffer 
        textCopy(buf+offs,bufN+1,id_str);

        // locate the last underscore
        if((underscore = lastMatchChar(buf,'_')) != nullptr )
        {
          *underscore  = 0;   // terminate the string prior to the underscore
          
          for(digit  = underscore + 1; *digit; digit++)
            if( !isdigit(*digit) )
              break;

          // if the underscore was followed by a number
          // or if the underscore was the last char
          // in the string - then digit will point to
          // the terminating zero - otherwise the
          // underscore did not indicate an iterating id
          if( *digit != 0 )
          {
            *underscore = '_';  // replace the underscore - its part of the label
            underscore = nullptr;
          }
          else
          {
            r.is_iter_fl = true;
            r.has_sfx_fl = true;

            // if there is a number following the underscore then this is the secInt
            if( textLength(underscore + 1) )
            {
              // a literal iteration count was given - parse it into an integer
              if((rc = string_to_number(underscore + 1,r.sfx_id_count)) != kOkRC )
              {
                rc = cwLogError(rc,"Unable to parse the secondary integer in the id label '%s'.",cwStringNullGuard(id_str));
                goto errLabel;
              }
            }              
          }
        }

        // verify that some content remains in the id string
        if( textLength(buf) == 0 )
        {
          rc = cwLogError(kSyntaxErrorRC,"Unable to parse the id string '%s'.",cwStringNullGuard(id_str));
          goto errLabel;
        }

        // go backward from the last char until the begin-of-string or a non-digit is found
        for(digit=buf + textLength(buf)-1; digit>=buf; --digit)
          if(!isdigit(*digit) )
          {
            ++digit; // advance to the first digit in the number
            break;
          }

        // if a label without a leading alpha was encountered
        if( digit == buf )
        {
          rc = cwLogError(kSyntaxErrorRC,"An id (%s) without a leading underscore or letter was encountered.",cwStringNullGuard(id_str));
          goto errLabel;
        }
        
        // if a digit was found then this is the 'priInt'
        if( digit>buf && textLength(digit) )
        {
          assert( buf <= digit-1 && digit-1 <= buf + bufN );
          
          // a literal base-sfx-id was given - parse it into an integer
          if((rc = string_to_number(digit,r.base_sfx_id)) != kOkRC )
          {
            rc = cwLogError(rc,"Unable to parse the primary integer in the id '%s'.",cwStringNullGuard(id_str));
            goto errLabel;            
          }

          *digit = 0; // zero terminate the label

          r.has_sfx_fl = true;

        }

        // verify that some content remains in the id string
        if( textLength(buf) == 0 )
        {
          rc = cwLogError(kSyntaxErrorRC,"Unexpected invalid id '%s'.",cwStringNullGuard(id_str));
          goto errLabel;

        }
        else
        {
          // store the label
          if( !inProcFl )
            r.label = mem::duplStr(buf);
        }
      }
      

    errLabel:
      return rc;
    }


    // Recursively search the tree of networks rooted on 'net' for the
    // network named 'net_proc_label'.
    network_t*  _io_stmt_find_labeled_network( network_t& net, const char* net_proc_label )
    {
      network_t* labeled_net = nullptr;

      // for each proc instance in the network
      for(unsigned i=0; i<net.procN && labeled_net==nullptr; ++i)
      {
        proc_t* proc =  net.procA[i];
          
        // if this proc instance has an  internal network
        if( proc->internal_net != nullptr )
        {
          // if the name of the network matches the key ...
          if( textIsEqual(proc->label,net_proc_label) )            
            labeled_net = proc->internal_net; // .. we are done
          else
          {
            // ... otherwise recurse
            labeled_net = _io_stmt_find_labeled_network(*proc->internal_net,net_proc_label);
          }
        }
        
      }

      if( labeled_net == nullptr && net.poly_link != nullptr )
        labeled_net = _io_stmt_find_labeled_network(*net.poly_link,net_proc_label );

      
      return labeled_net;
    }



    // If the local-var is iterating then the count of iterations must be controlled by exactly one
    // of the 3 parts of the io-stmt: local-var,remote_proc, or remote_var.  This function determines
    // which element is used to determine the iteration count.
    rc_t _io_stmt_determine_iter_count_ctl_ele(network_t& net,
                                               proc_t* proc,
                                               const io_ele_t& localVar,
                                               const io_ele_t& remoteProc,
                                               const io_ele_t& remoteVar,
                                               const char* localLabel,
                                               const char* remoteLabel,
                                               const io_ele_t*& result_ref )
    {
      assert( localVar.is_iter_fl );
      rc_t rc = kOkRC;

      result_ref = nullptr;
      
      const io_ele_t* iter_cnt_ctl_ele = nullptr;

      // if the local-var gives a literal count - then it determines the count
      if( localVar.sfx_id_count != kInvalidCnt )
      {
        // if the local-var gives a literal count then the remote-proc cannot give one
        if( remoteProc.sfx_id_count != kInvalidCnt )
        {
          rc = cwLogError(kSyntaxErrorRC,"The %s-var provided a literal iteration count therefore the %s-proc cannot.",localLabel,remoteLabel);
          goto errLabel;
        }
          
        // if the local-var gives a literal count then the remote-var cannot give one
        if( remoteVar.sfx_id_count != kInvalidCnt )
        {
          rc = cwLogError(kSyntaxErrorRC,"The %s-var provided a literal iteration count therefore the %s-var cannot.",localLabel,remoteLabel);
          goto errLabel;
        }

        iter_cnt_ctl_ele = &localVar;
          
      }
      else // the remote-proc or remote-var must control the iter count
      {
        // if the remote-proc gives a literal count - then it determines the count
        if( remoteProc.sfx_id_count != kInvalidCnt )
        {
          // then the remote-var cannot give a literal count
          if( remoteVar.sfx_id_count != kInvalidCnt )
          {
            rc = cwLogError(kSyntaxErrorRC,"The %s-proc provided a literal iteration count therefore the %s-var cannot.",remoteLabel,remoteLabel);
            goto errLabel;
          }

          iter_cnt_ctl_ele = &remoteProc;
            
        }
        else
        {
          // if the remote-var gives a literal count - then it determines the count
          if( remoteVar.sfx_id_count != kInvalidCnt )
          {
            iter_cnt_ctl_ele = &remoteVar;
          }
          else // no literal count was given - we need to get the implied count
          {
            // if the remote-proc is iterating then it will provide the count
            if( remoteProc.is_iter_fl )
            {
              // the remote-var cannot be iterating if the remote-proc is iterating
              if( remoteVar.is_iter_fl )
              {
                rc = cwLogError(kSyntaxErrorRC,"The %s-proc is iterating therefore the %s-var cannot.",remoteLabel,remoteLabel);
                goto errLabel;
              }
                
              iter_cnt_ctl_ele = &remoteProc;
            }
            else // the remote-proc isn't iterating check the remote-var
            {
              if( remoteVar.is_iter_fl )
              {
                iter_cnt_ctl_ele = &remoteVar;
              }
              else // no iteration count control was found
              {
                rc = cwLogError(kSyntaxErrorRC,"No iteration count control was specified.");
                goto errLabel;                  
              }
            }
          }
        }
      }               
      
    errLabel:
      
      if( rc == kOkRC )
        result_ref = iter_cnt_ctl_ele;
      
      return rc;
    }

    rc_t _io_stmt_calc_proc_ele_count(network_t& net, const io_ele_t& proc_ele, const char* in_or_src_label,  unsigned& cnt_ref)
    {
      rc_t rc = kOkRC;
      cnt_ref = 0;
      
      // if a literal proc sfx_id was given then use it otherwise use the default base-sfx-id (0)
      unsigned sfx_id = proc_ele.base_sfx_id==kInvalidCnt ? kBaseSfxId : proc_ele.base_sfx_id;
      unsigned n;
      for(n=0; proc_find(net, proc_ele.label, sfx_id ) != nullptr; ++n )
        sfx_id += 1;

      if( n == 0 )
      {
        rc = cwLogError(kSyntaxErrorRC,"The %s-proc '%s:%i' was not found.",in_or_src_label,cwStringNullGuard(proc_ele.label),sfx_id);
        goto errLabel;        
      }

      if( proc_ele.sfx_id_count == kInvalidCnt )
        cnt_ref = n;
      else
      {
        if( proc_ele.sfx_id_count > n )
        {
          rc = cwLogError(kSyntaxErrorRC,"The given literal sfx-id count %i execeeds the maximimum possible value of %i on  %s-proc '%s:%i' .",proc_ele.sfx_id_count,n,in_or_src_label,cwStringNullGuard(proc_ele.label),sfx_id);
          goto errLabel;
        }

        cnt_ref = proc_ele.sfx_id_count;
      }
      
      
    errLabel:
      return rc;
    }
    
    rc_t _io_stmt_calc_var_ele_count(network_t& net, const io_ele_t& proc_ele, const io_ele_t& var_ele, const char* in_or_src_label, unsigned& cnt_ref)
    {
      rc_t     rc          = kOkRC;

      cnt_ref = 0;

      proc_t*  proc        = nullptr;
      unsigned proc_sfx_id = proc_ele.base_sfx_id==kInvalidCnt ? kBaseSfxId : proc_ele.base_sfx_id;
        
      // locate the proc which owns this var
      if((proc = proc_find(net,proc_ele.label,proc_sfx_id)) == nullptr )
      {
        cwLogError(kSyntaxErrorRC,"The %s-proc inst instance '%s:%i' could not be found.",in_or_src_label,cwStringNullGuard(proc_ele.label),proc_sfx_id);
        goto errLabel;
      }
      else
      {
        // if a starting var sfx_id was given by the id then use it otherwise use the default base-sfx-id (0)
        unsigned sfx_id = var_ele.base_sfx_id==kInvalidCnt ? kBaseSfxId : var_ele.base_sfx_id;
        unsigned n;
        for(n=0; var_exists(proc,var_ele.label, sfx_id, kAnyChIdx ); ++n )
          sfx_id += 1;


        if( n == 0 )
        {
          cwLogError(kSyntaxErrorRC,"The %s-var '%s:%i' was not found.",in_or_src_label,cwStringNullGuard(var_ele.label),sfx_id);
          goto errLabel;
        }


        if( var_ele.sfx_id_count == kInvalidCnt )
          cnt_ref = n;
        else
        {
          if( var_ele.sfx_id_count > n )
          {
            rc = cwLogError(kSyntaxErrorRC,"The given literal sfx-id count %i execeeds the maximimum possible value of %i on  %s-var '%s:%i-%s:%i' .",var_ele.sfx_id_count,n,in_or_src_label,cwStringNullGuard(proc_ele.label),proc_ele.sfx_id,cwStringNullGuard(var_ele.label),sfx_id);
            goto errLabel;
          }

          cnt_ref = var_ele.sfx_id_count;
        }

      }
      
    errLabel:
      return rc;
    }

    rc_t _io_stmt_determine_iter_count( network_t& net,proc_t* proc, const char* local_label, const char* remote_label, io_stmt_t& io_stmt )
    {
      rc_t rc = kOkRC;

      // it has already been determined that this an iterating io-stmt
      // and a iteration count control element has been identified.
      assert( io_stmt.local_var_ele->is_iter_fl );
      assert( io_stmt.iter_cnt_ctl_ele != nullptr );

      switch( io_stmt.iter_cnt_ctl_ele->typeId )
      {
        case kLocalVarTypeId:
            
          assert( io_stmt.iter_cnt_ctl_ele->sfx_id_count != kInvalidCnt );
            
          if((io_stmt.iter_cnt = io_stmt.iter_cnt_ctl_ele->sfx_id_count) == 0 )
            rc = cwLogError(rc,"The literal %s-var iteration count on '%s:%i' must be greater than zero.", local_label, cwStringNullGuard(io_stmt.iter_cnt_ctl_ele->label),io_stmt.iter_cnt_ctl_ele->base_sfx_id);            
          break;
            
        case kRemoteProcTypeId:
          if((rc = _io_stmt_calc_proc_ele_count( *io_stmt.remote_net, *io_stmt.remote_proc_ele, remote_label, io_stmt.iter_cnt )) != kOkRC )
            rc = cwLogError(rc,"Unable to determine the %s-stmt iteration count based on the iteration control %s-proc '%s'.",local_label,remote_label,cwStringNullGuard(io_stmt.remote_proc_ele->label));
          break;
            
        case kRemoteVarTypeId:
          if((rc = _io_stmt_calc_var_ele_count( *io_stmt.remote_net, *io_stmt.remote_proc_ele, *io_stmt.remote_var_ele, remote_label, io_stmt.iter_cnt )) != kOkRC )
            rc = cwLogError(rc,"Unable to determine the %s-stmt iteration count based on the iteration control %s-var '%s'.",local_label,remote_label,cwStringNullGuard(io_stmt.remote_var_ele->label));

          break;
            
        default:
          rc = cwLogError(kInvalidStateRC,"An unknown %s-stmt element type was encountered.",local_label);
      }
      
      return rc;
    }
    
    void _io_stmt_destroy( io_stmt_t& s )
    {
      if( s.local_proc_ele != nullptr )
        mem::release(s.local_proc_ele->label);

      if( s.local_var_ele != nullptr )
        mem::release(s.local_var_ele->label);

      mem::release(s.remote_net_label);

      if( s.remote_proc_ele )
        mem::release(s.remote_proc_ele->label);

      if( s.remote_var_ele )
        mem::release(s.remote_var_ele->label);
    }
    
    void _io_stmt_array_destroy( io_stmt_t*& io_stmtA, unsigned io_stmtN )
    {
      if( io_stmtA != nullptr )
      {  
        for(unsigned i=0; i<io_stmtN; ++i)
          _io_stmt_destroy(io_stmtA[i] );

        mem::release(io_stmtA);
      }
    }

    rc_t _io_stmt_array_parse( network_t& net, proc_t* proc, const char* io_label, const object_t* io_dict_cfg, io_stmt_t*& ioArray_Ref, unsigned& ioArrayN_Ref )
    {
      rc_t       rc         = kOkRC;
      unsigned   stmtAllocN = 0;
      unsigned   stmtN      = 0;
      io_stmt_t* stmtA      = nullptr;

      ioArray_Ref = nullptr;
      ioArrayN_Ref = 0;

      // if there is no io-dict-cfg 
      if( io_dict_cfg == nullptr )
        goto errLabel;

      // validate the out-dict 
      if( !io_dict_cfg->is_dict() )
      {
        cwLogError(kSyntaxErrorRC,"The '%s' dict in proc instance '%s' is not a valid dictionary.",io_label,proc->label);
        goto errLabel;
      }
   
      if( io_dict_cfg->child_count() == 0 )
        goto errLabel;

      stmtAllocN = io_dict_cfg->child_count();
      stmtA      = mem::allocZ<io_stmt_t>(stmtAllocN);

      // for each input variable in the 'in' set
      for(unsigned i=0; i<stmtAllocN; ++i)
      {
        const object_t* io_stmt_pair = io_dict_cfg->child_ele(i);
        const char* s = nullptr;
          
        // validate the stmt pair syntax
          if( io_stmt_pair==nullptr 
            || !io_stmt_pair->is_pair()
            || textLength(io_stmt_pair->pair_label())==0
            || io_stmt_pair->pair_value()==nullptr
            || !io_stmt_pair->pair_value()->is_string()
            || (io_stmt_pair->pair_value()->value(s)) != kOkRC
            || textLength(s)==0 )
        {
          rc = cwLogError(kSyntaxErrorRC,"A syntax error was encoutered while attempting to parse the %s-stmt on the proc %s:%i.",io_label,cwStringNullGuard(proc->label),proc->label_sfx_id);
          goto errLabel;
        }

        stmtN += 1;
      }

      ioArray_Ref  = stmtA;
      ioArrayN_Ref = stmtN;
      
    errLabel:
      if( rc != kOkRC )
        _io_stmt_array_destroy(stmtA,stmtN);
      
      return rc;
      
    }

    // Set  'in_stmt.src_net' based on 'in_stmt.src_net_label'
    rc_t _io_stmt_locate_remote_net(network_t& net,proc_t* proc, io_stmt_t& io_stmt)
    {
      rc_t       rc      = kOkRC;
      network_t* remote_net = nullptr;

      io_stmt.remote_net = nullptr;
      
      if( io_stmt.remote_net_label == nullptr )
        remote_net = &net;
      else
      {        
        if( textIsEqual(io_stmt.remote_net_label,"_") )
          remote_net = proc->ctx->net;
        else
        {
          if((remote_net = _io_stmt_find_labeled_network(*proc->ctx->net,io_stmt.remote_net_label)) == nullptr )
          {
            rc = cwLogError(kSyntaxErrorRC,"The source net '%s' was not found.",cwStringNullGuard(io_stmt.remote_net_label));
            goto errLabel;
          }
        } 
      }
    errLabel:
      io_stmt.remote_net = remote_net;

      if( io_stmt.remote_net == nullptr )
        rc = cwLogError(kSyntaxErrorRC,"No remote net was found.");
      
      return rc;
    }
    

    rc_t _io_stmt_create( network_t& net,
                          proc_t*     proc,
                          io_stmt_t&  io_stmt,
                          const char* local_proc_var_str,
                          const char* remote_net_proc_var_str,
                          const char* local_label,
                          const char* remote_label)
    {
      rc_t rc = kOkRC;
      
      unsigned local_char_cnt  = textLength(local_proc_var_str);
      unsigned remote_char_cnt = textLength(remote_net_proc_var_str);      
      unsigned str_char_cnt    = std::max( local_char_cnt, remote_char_cnt );
      
      //const char* remote_net_label  = nullptr;
      const char* remote_proc_label = nullptr;
      const char* remote_var_label  = nullptr;
      const char* local_proc_label  = nullptr;
      const char* local_var_label   = nullptr;
        
      char  str[ str_char_cnt+1 ];

      io_stmt.remote_proc_ele->typeId = kRemoteProcTypeId;
      io_stmt.remote_var_ele->typeId  = kRemoteVarTypeId;
      io_stmt.local_proc_ele->typeId  = kLocalProcTypeId;
      io_stmt.local_var_ele->typeId   = kLocalVarTypeId;
      
      //
      //  Parse the remote net/proc/var 
      //
      
      // put the remote net/proc/var string into a non-const scratch buffer
      textCopy(str, remote_char_cnt+1, remote_net_proc_var_str );

      // parse the src part into it's 3 parts
      if((rc = _io_stmt_parse_net_proc_var_string(str, io_stmt.remote_net_label, remote_proc_label, remote_var_label )) != kOkRC )          
      {
        cwLogError(rc,"Unable to parse the '%s' part of an 'io-stmt'.",remote_label);
        goto errLabel;
      }
      
      // parse the rem-proc
      if((rc = _io_stmt_parse_ele( remote_proc_label, *io_stmt.remote_proc_ele  )) != kOkRC )
      {
        rc = cwLogError(rc,"Unable to parse the %s-proc from '%s'.",remote_label,cwStringNullGuard(str));
        goto errLabel;
      }
      
      // parse the remote-var
      if((rc = _io_stmt_parse_ele( remote_var_label, *io_stmt.remote_var_ele )) != kOkRC )
      {
        rc = cwLogError(rc,"Unable to parse the %s-var from '%s'.",remote_label,cwStringNullGuard(str));
        goto errLabel;
      }


      //
      // Parse the local proc/var
      //
        
      textCopy(str, local_char_cnt+1, local_proc_var_str );

      // parse the 'local' part into it's 2 parts
      if((rc = _io_stmt_parse_proc_var_string(str, local_proc_label, local_var_label )) != kOkRC )          
      {
        cwLogError(rc,"Unable to parse the '%s' part of an 'io-stmt'.",local_label);
        goto errLabel;
      }

      // parse the local-proc
      if((rc = _io_stmt_parse_ele( local_proc_label, *io_stmt.local_proc_ele, true  )) != kOkRC )
      {
        rc = cwLogError(rc,"Unable to parse the %s-proc from '%s'.",local_label,cwStringNullGuard(str));
        goto errLabel;
      }
        
      // parse the local-var
      if((rc = _io_stmt_parse_ele( local_var_label, *io_stmt.local_var_ele  )) != kOkRC )
      {
        rc = cwLogError(rc,"Unable to parse the %s-var from '%s'.",local_label,cwStringNullGuard(str));
        goto errLabel;
      }


      // get the var class desc. for the local-var (only used by in-stmt)
      if(( io_stmt.local_var_desc = var_desc_find(proc->class_desc,io_stmt.local_var_ele->label)) == nullptr )
      {
        rc = cwLogError(kEleNotFoundRC,"Unable to locate the var class desc for the %s-var from '%s'.",local_label,cwStringNullGuard(io_stmt.local_var_ele->label));
        goto errLabel;
      }
        
      // get the remote net
      if((rc = _io_stmt_locate_remote_net(net,proc,io_stmt)) != kOkRC )
      {
        rc = cwLogError(rc,"Unable to locate the %s-net '%s'.",remote_label, cwStringNullGuard(io_stmt.remote_net_label));
        goto errLabel;
      }

        
      // verify that both the local-proc and local-var are not iterating
      if( io_stmt.local_proc_ele->is_iter_fl && io_stmt.local_var_ele->is_iter_fl )
      {
        rc = cwLogError(kSyntaxErrorRC,"Both the '%s' proc and '%s' var cannot be iterating. See:'%s'",local_label,local_label,cwStringNullGuard(local_proc_var_str));
        goto errLabel;
      }

      // if the in-var has an sfx_id, or is iterating, then the var needs to be created (the dflt creation process assumes an sfx-id of 0)
      if( io_stmt.local_var_ele->base_sfx_id != kInvalidId || io_stmt.local_var_ele->is_iter_fl )
      {
        io_stmt.in_create_fl = true;
        if( io_stmt.local_var_ele->base_sfx_id == kInvalidId )
          io_stmt.local_var_ele->base_sfx_id = kBaseSfxId;
      }

      // if the remote-proc is not iterating and the remote-proc was not given a literal sfx-id and the remote is on the same net as the proc ...
      if( io_stmt.remote_proc_ele->is_iter_fl==false && io_stmt.remote_proc_ele->base_sfx_id==kInvalidId && io_stmt.remote_net==&net)
        io_stmt.remote_proc_ele->base_sfx_id = proc->label_sfx_id; // ... then the remote proc takes this proc's sfx id
      // (This results in poly proc's connecting to other poly procs with the same sfx-id by default).

      // if this is not an iterating in-stmt ... 
      if( !io_stmt.local_var_ele->is_iter_fl )
      {
        io_stmt.iter_cnt = 1;  // ... then it must be a simple 1:1 connection (Note if in-proc is iterating then it this must also be true)
      }
      else
      {
        // if the in-stmt is iterating then determine the in-stmt element which controls the iteration count
        if((rc = _io_stmt_determine_iter_count_ctl_ele(net,proc,
                                                       *io_stmt.local_var_ele,
                                                       *io_stmt.remote_proc_ele,
                                                       *io_stmt.remote_var_ele,
                                                       local_label,remote_label,
                                                       io_stmt.iter_cnt_ctl_ele)) != kOkRC )
        {
          rc = cwLogError(rc,"Unable to determine the iter count control ele.");
          goto errLabel;
        }

        // if the local-stmt is iterating then determine the iteration count
        if((rc = _io_stmt_determine_iter_count(net,proc,local_label,remote_label,io_stmt)) != kOkRC )
        {
          cwLogError(rc,"Unable to determine the %s-stmt iteration count.",local_label);
          goto errLabel;
        }
      }
      
      
    errLabel:
      if( rc != kOkRC )
        _io_stmt_destroy(io_stmt);
      
      return rc;
      
    }
    
    rc_t _io_stmt_connect_vars(network_t&       net,
                               proc_t*          proc,
                               const char*      local_label,
                               const char*      remote_label,
                               const io_stmt_t* ioStmtA,
                               unsigned         ioStmtN)
    {
      rc_t rc = kOkRC;

      // for each io-stmt
      for(unsigned i=0; i<ioStmtN; ++i)
      {
        const io_stmt_t& io_stmt = ioStmtA[i];

        // all local-stmts are iterating (but most only iterate once)
        for(unsigned j=0; j<io_stmt.iter_cnt; ++j)
        {
          variable_t* local_var   = nullptr;
          network_t*  remote_net  = io_stmt.remote_net;
          proc_t*     remote_proc = nullptr;
          variable_t* remote_var  = nullptr;

          //const char* local_proc_label  = io_stmt.local_proc_ele->label;
          const char* local_var_label   = io_stmt.local_var_ele->label;
          const char* remote_proc_label = io_stmt.remote_proc_ele->label;
          const char* remote_var_label  = io_stmt.remote_var_ele->label;

          unsigned local_var_sfx_id   = kInvalidId; 
          unsigned remote_proc_sfx_id = kInvalidId;
          unsigned remote_var_sfx_id  = kInvalidId;

          // if a literal in-var sfx id was not given ...
          if( io_stmt.local_var_ele->base_sfx_id == kInvalidId )
            local_var_sfx_id = kBaseSfxId; // ... then use the default sfx-id
          else
            local_var_sfx_id = io_stmt.local_var_ele->base_sfx_id; 

          // if a literal src-proc sfx id was not given ...
          if( io_stmt.remote_proc_ele->base_sfx_id == kInvalidId )
            remote_proc_sfx_id = kBaseSfxId; // ... then use the sfx_id of the in-var proc
          else
            remote_proc_sfx_id = io_stmt.remote_proc_ele->base_sfx_id; // ... otherwise use the given literal

          // if a literal src-var sfx id was not given ...
          if( io_stmt.remote_var_ele->base_sfx_id == kInvalidId )
            remote_var_sfx_id = kBaseSfxId; // ... then use the base-sfx-id
          else
            remote_var_sfx_id = io_stmt.remote_var_ele->base_sfx_id; // ... otherwise use the given literal

          // When the in-proc is iterating then we incr by the in-proc sfx-id (in this case j will never exceed 0)
          // otherwise increment by j - the current iteration count
          unsigned iter_incr = io_stmt.local_proc_ele->is_iter_fl ? proc->label_sfx_id : j;

          // both in-var and in-proc cannot be iterating
          assert( !(io_stmt.local_var_ele->is_iter_fl && io_stmt.local_proc_ele->is_iter_fl) );

          // if the in-var is iterating then incr. the in-var sfx-id
          if( io_stmt.local_var_ele->is_iter_fl )
            local_var_sfx_id += iter_incr;

          // if this is an iterating src-proc then iter the src-proc-sfx-id
          if( io_stmt.remote_proc_ele->is_iter_fl ) 
            remote_proc_sfx_id += iter_incr;

          // if this is an iterating src-var then iter the src-var-sfx-id
          if( io_stmt.remote_var_ele->is_iter_fl )  
            remote_var_sfx_id += iter_incr;
                    
          // locate local var
          if((rc = var_find( proc, local_var_label, local_var_sfx_id, kAnyChIdx, local_var )) != kOkRC )
          {
            rc = cwLogError(rc,"The %s-var '%s:%i' was not found.", local_label, io_stmt.local_var_ele->label, io_stmt.local_var_ele->base_sfx_id + j);
            goto errLabel;        
          }
          
          // locate remote proc instance 
          if((remote_proc = proc_find(*remote_net, remote_proc_label, remote_proc_sfx_id )) == nullptr )
          {
            rc = cwLogError(kSyntaxErrorRC,"The %s-proc '%s:%i' was not found.", remote_label, io_stmt.remote_proc_ele->label, remote_proc_sfx_id );
            goto errLabel;
          }

          // locate remote variable
          if((rc = var_find( remote_proc, remote_var_label, remote_var_sfx_id, kAnyChIdx, remote_var)) != kOkRC )
          {
            rc = cwLogError(rc,"The %s-var '%s:%i' was not found.", remote_label, io_stmt.remote_var_ele->label, remote_var_sfx_id);
            goto errLabel;
          }

          // verify that the remote_value type is included in the local_value type flags
          if( cwIsNotFlag(local_var->varDesc->type, remote_var->varDesc->type) )
          {
            rc = cwLogError(kSyntaxErrorRC,"The type flags don't match on %s:%s:%i (type:0x%x) %s:%s:%i.%s:%i (type:0x%x).", local_label,local_var_label, local_var_sfx_id, local_var->varDesc->type, remote_label, remote_proc_label, remote_proc_sfx_id, remote_var_label, remote_var_sfx_id, remote_var->varDesc->type);        
            goto errLabel;                
          }

          // verify that the source exists
          if( remote_var->value == nullptr )
          {
            rc = cwLogError(kSyntaxErrorRC,"The %s value is null on the connection %s::%s:%i %s:%s:%i.%s:%i .", remote_label, local_label, local_var_label, local_var_sfx_id, remote_label, remote_proc_label, remote_proc_sfx_id, remote_var_label, remote_var_sfx_id);        
            goto errLabel;
          }

          // if this is an 'in-stmt' ...
          if( io_stmt.local_proc_ele == &io_stmt.in_proc_ele )
            var_connect( remote_var, local_var );
          else
          {

            if( is_connected_to_source(remote_var) )
            {
              rc = cwLogError(kSyntaxErrorRC,"The 'out' connection from %s:%i.%s:%i to %s:%i.%s:%i failed because the destination already has a source.", local_var->proc->label, local_var->proc->label_sfx_id, local_var->label, local_var->label_sfx_id, remote_var->proc->label, remote_var->proc->label_sfx_id, remote_var->label, remote_var->label_sfx_id);
              goto errLabel;
            }

            // Disconnect any source that was previously connected to the 'in' var
            // (we do this for feedback connections (out-stmts), but not for in-stmts)
            // var_disconnect( remote_var );
            
            var_connect( local_var, remote_var ); // ... otherwise it is an out-stmt
          }
        }                
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Connection failed on proc '%s:%i'.",proc->label,proc->label_sfx_id);
      return rc;
    }


    // Find the proxy var associated with the proxied var 'procLabel:varLabel'
    const variable_t* _in_stmt_find_proxy_var( const char* procLabel, const char* varLabel, const variable_t* proxyVarL )
    {
      for(const variable_t* proxyVar=proxyVarL; proxyVar!=nullptr; proxyVar=proxyVar->var_link)
        if( textIsEqual(proxyVar->varDesc->proxyProcLabel,procLabel) && textIsEqual(proxyVar->varDesc->proxyVarLabel,varLabel) )
          return proxyVar;
      return nullptr;
    }
    
    rc_t _in_stmt_parse_in_list( network_t& net, proc_t* proc, variable_t* proxyVarL, proc_inst_parse_state_t& pstate )
    {
      rc_t        rc           = kOkRC;
      const char* local_label  = "in";
      const char* remote_label = "src";

      if((rc = _io_stmt_array_parse( net, proc, "in", pstate.in_dict_cfg, pstate.iStmtA, pstate.iStmtN)) != kOkRC )
        goto errLabel;


      for(unsigned i=0; i<pstate.iStmtN; ++i)
      {
        io_stmt_t* in_stmt = pstate.iStmtA + i;

        const char* src_net_proc_var_str = nullptr;
        const char* in_proc_var_str      = pstate.in_dict_cfg->child_ele(i)->pair_label();
        
        in_stmt->local_proc_ele  = &in_stmt->in_proc_ele;
        in_stmt->local_var_ele   = &in_stmt->in_var_ele;
        in_stmt->remote_proc_ele = &in_stmt->src_proc_ele;
        in_stmt->remote_var_ele  = &in_stmt->src_var_ele;


        // The validity of all the data elements in this statement was confirmed previously in _io_stmt_array_parse()
        pstate.in_dict_cfg->child_ele(i)->pair_value()->value(src_net_proc_var_str);

        assert( src_net_proc_var_str != nullptr );

        // create the io_stmt record
        if((rc = _io_stmt_create( net, proc, *in_stmt, in_proc_var_str, src_net_proc_var_str, local_label, remote_label )) != kOkRC )
        {
          rc = cwLogError(rc,"in-stmt create failed on '%s':%s on proc %s:%i", in_proc_var_str, src_net_proc_var_str, proc->label, proc->label_sfx_id );
          goto errLabel;
        }
        
        // if the in-var has an sfx_id, or is iterating, then the var needs to be created (the dflt creation process assumes an sfx-id of 0)
        if( in_stmt->in_var_ele.base_sfx_id != kInvalidId || in_stmt->in_var_ele.is_iter_fl )
        {
          in_stmt->in_create_fl = true;
          if( in_stmt->in_var_ele.base_sfx_id == kInvalidId )
            in_stmt->in_var_ele.base_sfx_id = kBaseSfxId;
        }

        // create the var
        if( in_stmt->in_create_fl )
        {
          const variable_t* proxy_var;
          
          // a variable cannot be in the 'in' list if it is a proxied variable - because it will
          // be connected to a proxy var.
          if((proxy_var = _in_stmt_find_proxy_var(proc->label, in_stmt->in_var_ele.label, proxyVarL)) != nullptr )
          {
            rc = cwLogError(kSyntaxErrorRC,"The variable:'%s' cannot be used as the in-var of an 'in' statement if it is a subnet variable: '%s'.",cwStringNullGuard(in_stmt->in_var_ele.label),cwStringNullGuard(proxy_var->label));
            goto errLabel;
          }

          
          for(unsigned i=0; i<in_stmt->iter_cnt; ++i)
          {
            variable_t* dum = nullptr;        

            if((rc = var_create( proc,
                                 in_stmt->local_var_desc->label,
                                 in_stmt->in_var_ele.base_sfx_id + i,
                                 kInvalidId,
                                 kAnyChIdx,
                                 in_stmt->local_var_desc->val_cfg,
                                 kInvalidTFl,
                                 dum )) != kOkRC )
            {
              rc = cwLogError(rc,"in-stmt var create failed on '%s:%s'.",cwStringNullGuard(in_proc_var_str),cwStringNullGuard(src_net_proc_var_str));
              goto errLabel;
            }
          }
        }        
      }

    errLabel:
      return rc;
    }

    rc_t _out_stmt_processing(network_t& net, proc_t* proc, proc_inst_parse_state_t& pstate)
    {
      rc_t rc = kOkRC;

      const char* local_label  = "src";
      const char* remote_label = "in";

      // parse the out-stmt list
      if((rc = _io_stmt_array_parse( net, proc, "out", pstate.out_dict_cfg, pstate.oStmtA, pstate.oStmtN)) != kOkRC )
        goto errLabel;

      // for each out-stmt
      for(unsigned i=0;  i<pstate.oStmtN; ++i)
      {
        io_stmt_t*  out_stmt            = pstate.oStmtA + i;
        const char* in_net_proc_var_str = nullptr;
        const char* src_proc_var_str    = pstate.out_dict_cfg->child_ele(i)->pair_label();
        
        out_stmt->local_proc_ele  = &out_stmt->src_proc_ele;
        out_stmt->local_var_ele   = &out_stmt->src_var_ele;
        out_stmt->remote_proc_ele = &out_stmt->in_proc_ele;
        out_stmt->remote_var_ele  = &out_stmt->in_var_ele;

        // The validity of all the data elements in this statement was confirmed previously in _io_stmt_array_parse()
        pstate.out_dict_cfg->child_ele(i)->pair_value()->value(in_net_proc_var_str);

        assert( in_net_proc_var_str != nullptr );

        // create the io_stmt record
        if((rc = _io_stmt_create( net, proc, *out_stmt, src_proc_var_str, in_net_proc_var_str, local_label, remote_label )) != kOkRC )
        {
          rc = cwLogError(rc,"out-stmt create failed on '%s':%s on proc %s:%i", src_proc_var_str, in_net_proc_var_str, proc->label, proc->label_sfx_id );
          goto errLabel;
        }
      }

      // create the connections 
      if((rc = _io_stmt_connect_vars(net, proc, local_label, remote_label, pstate.oStmtA, pstate.oStmtN)) != kOkRC )
        goto errLabel;

    errLabel:
      return rc;
        
    }
    

    // This function is used to create the variables on subnet procs
    // which are represented by interface variables on the subnet proxy (wrapper) proc.
    // 'proc' is a proc on the subnet's internal proc list
    // 'wrap_varL' is a list of all the variables on the wrapper proc.
    // These wrapper variables mirror variables on the internal subnet proc's.
    // This function finds the variables in wrap_varL that mirror
    // variables in 'proc' and instantiates them.  
    rc_t  _subnet_create_proxied_vars( proc_t* proc, variable_t* wrap_varL )
    {
      rc_t rc = kOkRC;

      // for each proxy var
      for(variable_t* wrap_var=wrap_varL; wrap_var!=nullptr; wrap_var=wrap_var->var_link )
      {
        // if this proxy var is on this internal proc (proc->label)
        if( textIsEqual(wrap_var->varDesc->proxyProcLabel,proc->label) )
        {
          
          variable_t* var;
          
          // create the proxied var
          if((rc = var_create( proc,
                               wrap_var->varDesc->proxyVarLabel,
                               wrap_var->label_sfx_id,
                               kInvalidId,
                               wrap_var->chIdx,
                               nullptr,
                               kInvalidTFl,
                               var )) != kOkRC )
          {
            rc = cwLogError(rc,"Subnet variable creation failed for %s:%s on wrapper variable:%s:%s.",cwStringNullGuard(wrap_var->varDesc->proxyProcLabel),cwStringNullGuard(wrap_var->varDesc->proxyVarLabel),cwStringNullGuard(wrap_var->proc->label),cwStringNullGuard(wrap_var->label));
            goto errLabel;
          }

          //printf("Proxy matched: %s %s %s : flags:%i.\n",proc->label, wrap_var->varDesc->proxyVarLabel, wrap_var->label,wrap_var->varDesc->flags );

          var->flags |= kProxiedVarFl;
          
          if( cwIsFlag(wrap_var->varDesc->flags,kUdpOutVarDescFl) )
            var->flags |= kProxiedOutVarFl;
        }
      }

    errLabel:
      return rc;
    }

    variable_t* _subnet_find_proxy_var( variable_t* wrap_varL, variable_t* var )
    {
      for(variable_t* wrap_var=wrap_varL; wrap_var!=nullptr; wrap_var=wrap_var->var_link)
        if( textIsEqual(wrap_var->varDesc->proxyProcLabel,var->proc->label) && textIsEqual(wrap_var->varDesc->proxyVarLabel,var->label) && (wrap_var->label_sfx_id==var->label_sfx_id) )
          return wrap_var;
        
      return nullptr;
    }
    
    rc_t _subnet_connect_proxy_vars( proc_t* proc, variable_t* wrap_varL )
    {
      rc_t rc = kOkRC;
      for(variable_t* var=proc->varL; var!=nullptr; var=var->var_link)
      {
        if( cwIsFlag(var->flags,kProxiedVarFl) )
        {
          variable_t* wrap_var;
          if((wrap_var = _subnet_find_proxy_var(wrap_varL,var)) == nullptr )
          {
            rc = cwLogError(kEleNotFoundRC,"The wrapped variable '%s:%i' not found on '%s:%i'.",var->label,var->label_sfx_id,proc->label,proc->label_sfx_id);
            goto errLabel;
          }
          
          if( cwIsFlag(var->flags,kProxiedOutVarFl) )
          {
            //printf("Proxy connection: %i %s:%i-%s:%i -> %s:%i-%s:%i\n",var->flags,
            //       var->proc->label,var->proc->label_sfx_id,var->label,var->label_sfx_id,
            //       wrap_var->proc->label,wrap_var->proc->label_sfx_id,wrap_var->label,wrap_var->label_sfx_id );
            
            var_connect(var,wrap_var);
          }
          else
          {
            //printf("Proxy connection: %i %s:%i-%s:%i -> %s:%i-%s:%i\n",var->flags,
            //       wrap_var->proc->label,wrap_var->proc->label_sfx_id,wrap_var->label,wrap_var->label_sfx_id,
            //       var->proc->label,var->proc->label_sfx_id,var->label,var->label_sfx_id );
            
            var_connect(wrap_var,var);
          }          
        }
      }

      errLabel:
        return rc;
    }

    // Check if the var named 'label' already exists in 'proc->varL'.
    bool _is_var_proc_already_created( proc_t* proc, const char* var_label, const proc_inst_parse_state_t& pstate )
    {
      for(unsigned i=0; i<pstate.iStmtN; ++i)
        if( textIsEqual(pstate.iStmtA[i].in_var_ele.label,var_label) && pstate.iStmtA[i].in_create_fl )
          return true;

      for(variable_t* var=proc->varL; var!=nullptr; var=var->var_link)
        if( textIsEqual(var->label,var_label) )
          return true;
      
      return false;
    }
    
    rc_t  _var_map_id_to_index(  proc_t* proc, unsigned vid, unsigned chIdx, unsigned& idxRef );

    rc_t _proc_create_var_map( proc_t* proc )
    {
      rc_t        rc        = kOkRC;
      unsigned    max_vid   = kInvalidId;
      unsigned    max_chIdx = 0;
      variable_t* var       = nullptr;

      // determine the max variable vid and max channel index value among all variables
      for(var=proc->varL; var!=nullptr; var = var->var_link )
      {

        if( var->vid != kInvalidId )
        {
          if( max_vid == kInvalidId || var->vid > max_vid )
            max_vid = var->vid;

          if( var->chIdx != kAnyChIdx && (var->chIdx+1) > max_chIdx )
            max_chIdx = (var->chIdx + 1);

        }
      }

      // If there are any variables
      if( max_vid != kInvalidId )
      {
        // create the variable map array
        proc->varMapChN    = max_chIdx + 1;
        proc->varMapIdN    = max_vid + 1;
        proc->varMapN      = proc->varMapIdN * proc->varMapChN;
        proc->varMapA      = mem::allocZ<variable_t*>( proc->varMapN );
        proc->modVarMapN   = 0;
        proc->modVarMapA   = nullptr;

        // assign each variable to a location in the map
        for(variable_t* var=proc->varL; var!=nullptr; var=var->var_link)
        {
          if( var->vid != kInvalidId )
          {
            unsigned idx = kInvalidIdx;

            if((rc = _var_map_id_to_index( proc, var->vid, var->chIdx, idx )) != kOkRC )
              goto errLabel;

          
            // verify that there are not multiple variables per map position          
            if( proc->varMapA[ idx ] != nullptr )
            {
              variable_t* v0 = proc->varMapA[idx];
              rc = cwLogError(kInvalidStateRC,"The variable '%s' id:%i ch:%i and '%s' id:%i ch:%i share the same variable map position on proc instance: %s. This is usually cased by duplicate variable id's.",
                              v0->label,v0->vid,v0->chIdx, var->label,var->vid,var->chIdx,proc->label);

              goto errLabel;
            }

            // assign this variable to a map position
            proc->varMapA[ idx ] = var;

            if( var->chIdx != kAnyChIdx && var->value == nullptr )
            {
              rc = cwLogError(kInvalidStateRC,"The value of the variable '%s' ch:%i on proc instance:'%s' has not been set.",var->label,var->chIdx,proc->label);
              goto errLabel;
            }

            if( cwIsFlag(var->varDesc->flags,kNotifyVarDescFl) )
            {
              if( var->value == nullptr )
              {
                rc = var_error(var,kInvalidStateRC,"The variable value was unexpectedly not set.");
                goto errLabel;
              }
              
              //assert( var->value != nullptr );

              if( !value_can_auto_notify(var->value) )
              {
                //rc = cwLogError(kSyntaxErrorRC,"The variable '%s:%i.%s:%i' is marked for notification but the data type '%s' does not support it.",var->proc->label,var->proc->label_sfx_id,var->label,var->label_sfx_id,value_to_type_label(var->value));
                //goto errLabel;
              }
              else
              {
                proc->modVarMapN += 1;
              }
            }

          }
        }

        // if there are variables marked for notification ...
        if( proc->modVarMapN )
        {
          // ... then allocate space in modVarMapA[] 
          proc->modVarMapN *= proc->varMapChN; // (assume that all variables have multiple channels)
          proc->modVarMapA   = mem::allocZ<variable_t*>( proc->modVarMapN );
        }
      }
    errLabel:
      return rc;
      
    }

    /*
    void _complete_input_connections( proc_t* proc )
    {
      for(variable_t* var=proc->varL; var!=nullptr; var=var->var_link)
        if(var->chIdx == kAnyChIdx && is_connected_to_source_proc(var) )
        {

          variable_t* base_src_var = var->src_var;

          // since 'var' is on the 'any' channel the 'src' var must also be on the 'any' channel
          assert( base_src_var->chIdx == kAnyChIdx );
          
          //printf("%s %s\n",proc->label,var->label);
          
          // for each var channel in the input var
          for(variable_t* in_var = var->ch_link; in_var != nullptr; in_var=in_var->ch_link)
          {
            // locate the matching channel on the 'src' var
            variable_t* svar = base_src_var;
            for(; svar!=nullptr; svar=svar->ch_link)
              if( svar->chIdx == in_var->chIdx )
                break;

            // connect the src->input var
            _connect_vars( svar==nullptr ? base_src_var : svar, in_var);
          }
        }
    }
    */

    // On return label_ref hold a pointer to a string which must be released with a call to mem::release()
    rc_t _proc_parse_var_log_label( const char* var_label, char*& label_ref, unsigned& sfx_id_ref )
    {
      rc_t rc = kOkRC;
      unsigned label_char_cnt = 0;
      
      label_ref = nullptr;
      sfx_id_ref = kInvalidId;

      const unsigned sn = textLength(var_label);
      const char* s = nullptr;
      if( sn == 0 )
      {
        rc = cwLogError(kSyntaxErrorRC,"A zero-length log var label was encountered.");
        goto errLabel;
      }

      // set 's' to point to the last character in the string
      s = var_label + textLength(var_label)-1;

      // backup until 's' is pointing to the first non-digit in the string
      for(; s >= var_label; --s )
        if( !isdigit(*s) )
          break;

      // if no digits were found
      if( s == var_label + textLength(var_label)-1 )
      {
        label_char_cnt = sn;
        sfx_id_ref = kBaseSfxId;
      }
      else // ... suffix digits were round
      {
        
        // if only digits were found
        if( s < var_label )
        {
          rc = cwLogError(kSyntaxErrorRC,"The log var label appears to contain only digits.");
          goto errLabel;
        }

        // a literal base-sfx-id was given - parse it into an integer
        if((rc = string_to_number(s+1,sfx_id_ref)) != kOkRC )
        {
          rc = cwLogError(rc,"The suffix id portion of a log variable could not be parsed.");
          goto errLabel;
        }

        label_char_cnt = (s+1) - var_label;
      }

      label_ref = mem::duplStr(var_label,label_char_cnt);
      
    errLabel:
      if( rc != kOkRC )
      {
        mem::release(label_ref);
        sfx_id_ref = kInvalidId;
      }
      
      return rc;
    }

    
    void _proc_append_var_to_log_list( proc_t* proc, unsigned log_var_verbosity, bool log_init_fl, bool log_rt_fl, variable_t* var )
    {
      // either log_init_fl or log_rt_fl must be set
      assert( log_init_fl || log_rt_fl );
      
      bool already_on_list_fl = cwIsFlag(var->flags,kLogInitVarFl | kLogRtVarFl);
      
      // if this variable is logged on init (cycle==0)
      if( log_init_fl )
        var->flags |= kLogInitVarFl;

      // if this variable is logged on (cycle>=0)
      if( log_rt_fl )
        var->flags |= kLogRtVarFl;
      
      // if this variable has already been added to the proc->logVarL
      if( already_on_list_fl )
        return;
   
      // turn on the 'log' flag for this variable.
      var->log_verbosity = log_var_verbosity;

      // append the variable to be logged to the end of proc->logVarL.
      if( proc->logVarL == nullptr )
        proc->logVarL = var;
      else
      {
        variable_t* v = proc->logVarL;
        while( v->log_link != nullptr )
          v = v->log_link;

        v->log_link = var;
        var->log_link = nullptr;
      }      
    }

    void _proc_append_var_channels_to_log_list( proc_t* proc, unsigned log_var_verbosity, bool log_init_fl, bool log_rt_fl, variable_t* var )
    {
      assert( var->chIdx == kAnyChIdx );
      
      // if this var has no channels then append the base channel ...
      if( var->ch_link == nullptr )
        _proc_append_var_to_log_list( proc, log_var_verbosity, log_init_fl, log_rt_fl, var );
      else
      {
        // ... otherwise append each of the channels
        for(variable_t* ch_var = var->ch_link; ch_var != nullptr; ch_var=ch_var->ch_link)
          _proc_append_var_to_log_list( proc, log_var_verbosity, log_init_fl, log_rt_fl, ch_var );
      }
      
    }
    
    // 
    rc_t _proc_find_and_append_var_to_log_list( proc_t* proc, unsigned log_var_verbosity, const char* var_label, unsigned label_sfx_id )
    {
      rc_t        rc             = kOkRC;
      variable_t* var            = proc->varL;
      bool        found_fl       = false;
      const bool  log_on_init_fl = false;
      const bool  log_rt_fl      = true;

      // for each var
      for(; var != nullptr; var=var->var_link )
      {
        // if the label matches, the sfx_id matches, and the chidx == kAnyChIdx
        if( (textIsEqual(var->label,var_label)  && (label_sfx_id == kInvalidId || var->label_sfx_id == label_sfx_id)) && var->chIdx == kAnyChIdx )
        {

          _proc_append_var_channels_to_log_list( proc, log_var_verbosity, log_on_init_fl, log_rt_fl, var );
          
          found_fl = true;
          
          break;
        }
      }
      
      if( !found_fl )
        rc = cwLogError(kSyntaxErrorRC,"The requested log variable:%s:%i was not be found.",cwStringNullGuard(var_label),label_sfx_id);      

    errLabel:
      return rc;
    }

    rc_t _proc_process_log_var_label( proc_t* proc, unsigned log_var_verbosity, const object_t* log_var_string )
    {
      rc_t        rc             = kOkRC;
      const char* full_var_label = nullptr;
      char*       var_label      = nullptr;
      unsigned    label_sfx_id   = kInvalidId;

      log_var_string->value(full_var_label);

      // break the full var label into it's label and sfx_id parts
      if((rc = _proc_parse_var_log_label( full_var_label, var_label, label_sfx_id )) != kOkRC )
      {
        goto errLabel;
      }

      // find the variable and append it to the proc log list
      if((rc= _proc_find_and_append_var_to_log_list( proc, log_var_verbosity, var_label, label_sfx_id )) != kOkRC )
      {
        goto errLabel;
      }

    errLabel:
      mem::release(var_label);
      return rc;
    }

    rc_t _proc_log_stmt_parse_flags( proc_t* proc, const object_t* flags_cfgL, bool& log_all_vars_fl_ref, bool& all_vars_on_init_fl_ref )
    {
      rc_t            rc      = kOkRC;
      log_all_vars_fl_ref     = false;
      all_vars_on_init_fl_ref = false;
      const object_t* ele     = nullptr;

      if( flags_cfgL == nullptr )
        return rc;
      
      while( (ele = flags_cfgL->next_child_ele(ele)) != nullptr )
      {
        const char* s = nullptr;
        if( !ele->is_string() )
        {
          rc = cwLogError(kSyntaxErrorRC,"Logging flags must be identifiers.");
          goto errLabel;
        }
        else
        {
          if((rc = ele->value(s)) != kOkRC )
          {
            rc = cwLogError(rc,"A logging flag could not be parsed.");
            goto errLabel;
          }
          else
          {
            if( textIsEqual(s,"all") )
            {
              log_all_vars_fl_ref = true;
              continue;
            }
            if( textIsEqual(s,"init") )
            {
              all_vars_on_init_fl_ref = true;
              continue;
            }

            rc = cwLogError(kSyntaxErrorRC,"An unknown logging flag '%s' was encountered.",cwStringNullGuard(s));
            goto errLabel;
          }
        }
      }

    errLabel:
      return rc;
      
    }
    
    rc_t _proc_log_stmt_var_list( proc_t* proc, unsigned verbosity, const object_t* logCfgL )
    {
      rc_t            rc      = kOkRC;
      const object_t* ele_cfg = nullptr;
      unsigned        ele_idx = 0;

      if( logCfgL == nullptr )
        return rc;

      // the log stmt must be a list
      if( !logCfgL->is_list() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The 'log' var list statement does not have list syntax.");
        goto errLabel;
      }

      // for each element in the log list
      while( (ele_cfg = logCfgL->next_child_ele(ele_cfg)) != nullptr )
      {
        // each element in the log statement list must be a string
        if( ele_cfg->is_string() )
        {
          // process this string as a 'var' identifier and update the proc->logVarL
          if((rc = _proc_process_log_var_label(proc,verbosity,ele_cfg)) != kOkRC )
          {
            rc = cwLogError(rc,"'log' statement processing failed on the variable label at index %i.",ele_idx);
            goto errLabel;
          }
        }
        else
        {
          rc = cwLogError(kSyntaxErrorRC,"Invalid 'log' statement element syntax at var label index %i.",ele_idx);
          goto errLabel;
        }

        ele_idx += 1;
      }
      
    errLabel:
      return rc;
    }

    rc_t _proc_log_stmt_var_all( proc_t* proc, unsigned verbosity, bool all_on_init_fl, bool all_fl )
    {
      rc_t        rc  = kOkRC;
      variable_t* var = proc->varL;

      // for each var
      for(; var != nullptr; var=var->var_link )
      {
        // if the label matches, the sfx_id matches, and the chidx == kAnyChIdx
        if( var->chIdx == kAnyChIdx )
        {
          _proc_append_var_channels_to_log_list( proc, verbosity, all_on_init_fl, all_fl, var );
        }
        
      }
      
      errLabel:
        return rc;
    }

    rc_t _proc_log_stmt_parse_level( const object_t* level_cfg, log::logLevelId_t& level_ref )
    {
      rc_t         rc        = kOkRC;
      const char*  s         = nullptr;
      
      if( level_cfg == nullptr )
        return rc;

      if( !level_cfg->is_string() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The log level was not given as a string.");
        goto errLabel;
      }

      if((rc = level_cfg->value(s)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"The log level could not be parsed.");
        goto errLabel;
      }

      if((level_ref = log::levelFromString(s)) == log::kInvalid_LogLevel )
      {
        rc = cwLogError(kSyntaxErrorRC,"The log level '%s' was not recognized.",cwStringNullGuard(s));
        goto errLabel;
      }

    errLabel:
      if( rc != kOkRC )
      {
        level_ref = log::kInvalid_LogLevel;
        rc = cwLogError(rc,"log level assignment failed. The log level must set to one of:'debug','info','warn','error','fatal'.");
      }

      return rc;
    }
    
    rc_t _proc_log_stmt_parse_verbosity( const object_t* verb_cfg, unsigned& verbosity_ref )
    {
      rc_t rc = kOkRC;
      
      verbosity_ref = kMinimalValPrintVerb;
      
      if( verb_cfg == nullptr )
        return rc;

      // if verbosity was given as a string: silent,minimal,summary,all
      if( verb_cfg->is_string() )
      {
        const char* s    = nullptr;
        unsigned    verb = kInvalidValPrintVerb;
        
        if((rc = verb_cfg->value(s)) != kOkRC )
        {
          rc = cwLogError(rc,"The 'log' verbosity level could not be parsed as a string.");
          goto errLabel;
        }

        if((verb = value_print_verbosity_from_string(s)) == kInvalidValPrintVerb )
        {
          rc = cwLogError(rc,"The 'log' verbosity '%s' was not recognized. Verbosity set at 'minimal'.",cwStringNullGuard(s));
          goto errLabel;
        }

        verbosity_ref = verb;
        return rc;
      }

      // if verbosity was given as a number: 0,1,2,3
      if( verb_cfg->is_numeric() )
      {
        unsigned verb;
        if((rc = verb_cfg->value(verb)) != kOkRC )
        {
          rc = cwLogError(rc,"The 'log' verbosity level could not be parsed as a number.");
          goto errLabel;
        }

        if( verb > kMaxValPrintVerb )
        {
          cwLogWarning("The log verbosity setting has been reduced to the maximum setting:%i.",kMaxValPrintVerb);
          verb = kMaxValPrintVerb;
        }
        
        verbosity_ref = verb;
        return rc;
      }

      rc = cwLogError( kSyntaxErrorRC, "The 'log' verbosity must be a string or number.");

    errLabel:
      return rc;
    }

    rc_t _proc_process_log_stmt(proc_t* proc, const object_t* logCfgD )
    {
      rc_t            rc                 = kOkRC;
      const object_t* verb_cfg           = nullptr;
      const object_t* level_cfg          = nullptr;
      const object_t* var_cfgL           = nullptr;
      const object_t* flags_cfgL         = nullptr;
      bool            log_all_vars_fl    = false;
      bool            log_all_on_init_fl = false;
      unsigned        verbosity          = kMinimalValPrintVerb;

      if( logCfgD == nullptr )
        return rc;
      
      if((rc = logCfgD->readv("verbosity", kOptFl, verb_cfg,
                              "level",     kOptFl, level_cfg,
                              "flags",     kOptFl, flags_cfgL,
                              "varL",      kOptFl, var_cfgL)) != kOkRC )
      {
        rc = cwLogError(rc,"The 'log' statement parse failed.");
        goto errLabel;
      }

      if((rc = _proc_log_stmt_parse_level( level_cfg, proc->logLevel )) != kOkRC )
      {
        goto errLabel;
      }
      
      if((rc = _proc_log_stmt_parse_verbosity( verb_cfg, verbosity )) != kOkRC )
      {
        goto errLabel;
      }

      // if verbosity is set to silent then don't bother building proc->varL
      if( verbosity == kSilentValPrintVerb )
        goto errLabel;

      if((rc = _proc_log_stmt_parse_flags( proc, flags_cfgL, log_all_vars_fl, log_all_on_init_fl )) != kOkRC )
      {
        goto errLabel;
      }

      if( log_all_vars_fl || log_all_on_init_fl )
      {
        if((rc = _proc_log_stmt_var_all( proc, verbosity, log_all_on_init_fl, log_all_vars_fl )) != kOkRC )
        {
          goto errLabel;
        }        
      }

      if( var_cfgL != nullptr )
      {
        if((rc = _proc_log_stmt_var_list( proc, verbosity, var_cfgL )) != kOkRC )
        {
          goto errLabel;
        }
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"'log' statement processing failed on '%s:%i.",cwStringNullGuard(proc->label),proc->label_sfx_id);
      return rc;
      
    }

    // Form an array (proc->manualNotifyVarA[]) of destination
    // variables used by this proc which require notification, but
    // which are connected to by source variables (on this proc) which
    // do not support notification.

    void _proc_create_manual_notification_array( proc_t* proc )
    {
      unsigned n = 0;
      variable_t* var = proc->varL;
      for(; var!=nullptr; var=var->var_link )
        if( cwIsFlag(var->varDesc->flags,kNotifyVarDescFl) && is_connected_to_source(var) && !value_can_auto_notify(var->src_var->value) )
        {
          ++n;
        }
      
      if( n > 0 )
      {
        proc->manualNotifyVarA = mem::allocZ<manual_notify_t>(n);
        proc->manualNotifyVarN = 0;

        var = proc->varL;
        for(; var!=nullptr && proc->manualNotifyVarN<n; var=var->var_link )
          if( cwIsFlag(var->varDesc->flags,kNotifyVarDescFl) && is_connected_to_source(var) && !value_can_auto_notify(var->src_var->value) )
          {
            proc->manualNotifyVarA[ proc->manualNotifyVarN   ].check_ele_cnt_fl = value_supports_an_ele_count(var->src_var->value);
            proc->manualNotifyVarA[ proc->manualNotifyVarN++ ].var = var;
          }
        
      }

      assert( n == proc->manualNotifyVarN );
      
    }

    
    rc_t _proc_do_pre_runtime_variable_notification( proc_t* proc )
    {
      rc_t rc  = kOkRC;
      rc_t rc1 = kOkRC;

      // Schedule all variables marked for notification as changed.
      for(unsigned i=0; i<proc->varMapN; ++i)
      {
        if( proc->varMapA[i] != nullptr && proc->varMapA[i]->vid != kInvalidId )
        {
          variable_t* var = proc->varMapA[i];

          if((rc = var_schedule_notification( var )) != kOkRC )
            rc1 = cwLogError(rc,"The proc inst instance '%s:%i' reported an invalid valid on variable:%s chIdx:%i.", var->proc->label, var->proc->label_sfx_id, var->label, var->chIdx );
        }
      }

      // Call proc->notify() on all variables marked for notification.
      rc = proc_notify(proc, kCallbackPnFl | kQuietPnFl );
      
      return rcSelect(rc,rc1);
    }

    rc_t _proc_parse_ui_var_list_cfg( proc_t* proc, const object_t* varL_cfg )
    {
      rc_t            rc         = kOkRC;
      const object_t* var_ui_cfg = nullptr;
      
      if( !varL_cfg->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The 'ui' cfg must be a list.");
        goto errLabel;
      }

      while((var_ui_cfg = varL_cfg->next_child_ele(var_ui_cfg)) != nullptr )
      {
        const char* var_label;
        const object_t* var_recd = nullptr;
        
        unsigned        sfx_id = kBaseSfxId;
        unsigned        ch_idx = kAnyChIdx;
        const char*     title  = nullptr;
        const object_t* flagsL = nullptr;
        variable_t*     var = nullptr;
        
        if((var_label = var_ui_cfg->pair_label()) == nullptr )
        {
          rc = cwLogError(rc,"An error occurred while accessing a 'ui' variable label.");
          goto errLabel;                    
        }

        if((var_recd = var_ui_cfg->pair_value()) == nullptr )
        {
          rc = cwLogError(rc,"An error occurred while accessing 'ui' variable record for '%s'",cwStringNullGuard(var_label));
          goto errLabel;                    
        }

        if((rc = var_recd->readv( "sfx_id", kOptFl, sfx_id,
                                  "ch_idx", kOptFl, ch_idx,
                                  "title",  kOptFl, title,
                                  "flags",  kOptFl, flagsL )) != kOkRC )
        {
          rc = cwLogError(rc,"An error occurred while parsing the 'ui' variable record for '%s'",cwStringNullGuard(var_label));
          goto errLabel;          
        }

        if((rc = var_find(proc, var_label, sfx_id, ch_idx, var )) != kOkRC )
        {
          rc = cwLogError(rc,"The variable '%s:%i' ch:%i referenced in the 'ui' cfg. could not be found.",cwStringNullGuard(var_label),sfx_id,ch_idx);
          goto errLabel;
        }

        if( title != nullptr )
        {
          var->ui_title = mem::duplStr(title);
        }


        if( flagsL != nullptr )
        {
          const object_t* flag = nullptr;
          while((flag = flagsL->next_child_ele(flag)) != nullptr)
          {
            enum { kHideId, kShowId, kEnableId, kDisableId };
            idLabelPair_t flagA[] = { {kHideId,"hide"}, {kShowId,"show"}, {kEnableId,"enable"}, {kDisableId,"disable"}, {kInvalidId,nullptr} };
            
            const char* flag_str = nullptr;
            if( flag->value(flag_str ) != kOkRC )
            {
              rc = cwLogError(kSyntaxErrorRC,"A 'ui' flag value could not be parsed.");
              goto errLabel;
            }

            switch( labelToId(flagA,flag_str, kInvalidId ) )
            {
              case kHideId:
                var->ui_hide_fl = true;
                break;
                
              case kShowId:
                var->ui_hide_fl = false;
                break;
                
              case kEnableId:
                var->ui_disable_fl = false;
                break;
                
              case kDisableId:
                var->ui_disable_fl = true;
                break;
                
              case kInvalidId:
                rc = cwLogError(kSyntaxErrorRC,"The 'ui' flag attribute '%s' is not valid.",cwStringNullGuard(flag_str));
                goto errLabel;
            }
            
          }
        }
      }

      
      
    errLabel:
      if( rc != kOkRC )
      {
        rc = cwLogError(rc,"Parsing the 'ui' cfg. failed for the proc instance '%s:%i'.",cwStringNullGuard(proc->label),proc->label_sfx_id);
      }

      return rc;
      
    }

    rc_t _proc_parse_ui_cfg(proc_t* proc, const proc_inst_parse_state_t& pstate)
    {
      rc_t rc = kOkRC;
      bool ui_create_fl = false;
      const object_t* varsL_cfg = nullptr;

      if( pstate.ui_cfg == nullptr )
        goto errLabel;

      if( pstate.ui_cfg->is_dict() == false )
      {
        rc = cwLogError(kSyntaxErrorRC,"The UI cfg. record is not a dictionary.");
        goto errLabel;
      }

      if( pstate.ui_cfg->find("create_fl") == nullptr )
        ui_create_fl = proc->ctx->ui_create_fl;
      
      // parse the optional args
      if((rc = pstate.ui_cfg->getv_opt("create_fl",  ui_create_fl,
                                       "vars", varsL_cfg)) != kOkRC )
      {
        goto errLabel;        
      }
      
      if( ui_create_fl )
        proc->flags |= kUiCreateProcFl;

      if( varsL_cfg  != nullptr )
        rc = _proc_parse_ui_var_list_cfg(proc,varsL_cfg);
      
    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(kSyntaxErrorRC,"The proc instance cfg. '%s:%i' UI configuration parse failed.",pstate.proc_label,pstate.proc_label_sfx_id);

      return rc;
    }
    
    rc_t _proc_verify_required_record_fields( const proc_t* proc )
    {
      rc_t rc = kOkRC;
      const variable_t* var;
      for(var=proc->varL; var!=nullptr; var=var->var_link )
        if( is_connected_to_source(var)
            && cwIsFlag(var->value->tflag,kRBufTFl)
            && var->classVarDesc->fmt.recd_fmt != nullptr
            && var->classVarDesc->fmt.recd_fmt->req_fieldL != nullptr )
        {
          const rbuf_t* rbuf = nullptr;

          if((rc = var_get(var,rbuf)) != kOkRC )
          {
            rc = cwLogError(rc,"The connected rbuf field on var '%s:%i' could not be accessed.",cwStringNullGuard(var->label),var->label_sfx_id);
            goto errLabel;
          }

          const object_t* fieldLabelL = var->classVarDesc->fmt.recd_fmt->req_fieldL;
          unsigned        fld_cnt     = fieldLabelL->child_count();
          
          for(unsigned i=0; i<fld_cnt; ++i)
          {
            const char* field_label = nullptr;
            
            if((rc = fieldLabelL->child_ele(i)->value(field_label)) != kOkRC )
            {
              rc = cwLogError(rc,"Unable to parse required record field label on var '%s:%i'.",cwStringNullGuard(var->label),var->label_sfx_id);
              goto errLabel;
            }

            if(recd_type_field_index( rbuf->type, field_label) == kInvalidIdx )
            {
              rc = cwLogError(rc,"The required field '%s' does not exist on var '%s:%i'.",cwStringNullGuard(field_label),cwStringNullGuard(var->label),var->label_sfx_id);
              goto errLabel;
            }

            //printf("required field '%s' verified on '%s:%i-%s:%i'.\n",field_label,cwStringNullGuard(proc->label),proc->label_sfx_id,cwStringNullGuard(var->label),var->label_sfx_id);
            
          }
        }

    errLabel:
      if(rc != kOkRC )
        cwLogError(rc,"Required field processing failed on '%s:%i-%s%i'.",cwStringNullGuard(proc->label),proc->label_sfx_id);
      
      return rc;
    }
    
    // Set pstate.proc_label and pstate.label_sfx_id
    rc_t  _proc_parse_inst_label( const network_t& net, const char* proc_label_str, proc_inst_parse_state_t& pstate )
    {
      rc_t     rc         = kOkRC;
      unsigned digitCharN = 0;
      unsigned sfx_id     = kInvalidId;
      unsigned sN         = textLength(proc_label_str);
      char     s[sN+1];

      if( sN == 0 )
      {
        rc = cwLogError(kSyntaxErrorRC,"A blank proc-instance label was encountered.");
        goto errLabel;
      }
      
      textCopy(s,sN+1,proc_label_str,sN);

      // if this label has no digit suffix
      if((digitCharN = _digit_suffix_char_count( s )) > 0)
      {
        if( digitCharN == sN )
        {
          rc = cwLogError(kSyntaxErrorRC,"A proc-instance label ('%s') was encountered that appears to be a number rather than  identifier.",s);
          goto errLabel;
        }
        else
        {
          if( string_to_number(s + sN-digitCharN,sfx_id) != kOkRC )
          {
            rc = cwLogError(kOpFailRC,"A proc-instance numeric suffix (%s) could not be converted into an integer.",s);
            goto errLabel;
          }
          
          s[sN-digitCharN] = '\0';
        }
      }

      // if the parsed sfx-id was not explicitly set then set it to the network index
      if( sfx_id == kInvalidId )
      {
        sfx_id = net.poly_idx;
      }
      
      // if this net is part of a network array then the proc suffix id is used distinguish this
      // proc instance from the same proc instance in sibling networks and so it can't
      // be changed to some other value by providing an explicit suffix id
      if( net.polyN > 1 && sfx_id != net.poly_idx )
      {
        rc = cwLogError(kInvalidStateRC,"The proc instance '%s' numeric suffix id (%i) conflicts with the network poly index (%i).",cwStringNullGuard(proc_label_str),pstate.proc_label_sfx_id,net.poly_idx);
        goto errLabel;
      }

      pstate.proc_label        = mem::duplStr(s);
      pstate.proc_label_sfx_id = sfx_id;

    errLabel:
      return rc;
      
    }
    
    rc_t _proc_parse_cfg( network_t& net, const object_t* proc_inst_cfg, proc_inst_parse_state_t& pstate )
    {
      rc_t            rc       = kOkRC;
      const object_t* arg_dict = nullptr;
      const object_t* network  = nullptr;
      
      // validate the syntax of the proc_inst_cfg pair
      if( !_is_non_null_pair(proc_inst_cfg))
      {
        rc = cwLogError(kSyntaxErrorRC,"The proc instance cfg. is not a valid pair.");
        goto errLabel;
      }

      pstate.proc_label_sfx_id = kInvalidId;
      
      // extract the proc instance label and (sfx-id suffix)
      if((rc = _proc_parse_inst_label( net, proc_inst_cfg->pair_label(), pstate )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Parsing failed on the label and sfx-id for '%s'.",cwStringNullGuard(proc_inst_cfg->pair_label()));
        goto errLabel;
      }
      
      // verify that the proc instance label is unique
      if( proc_find(net,pstate.proc_label,pstate.proc_label_sfx_id) != nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The proc instance label '%s:%i' has already been used.",pstate.proc_label,pstate.proc_label_sfx_id);
        goto errLabel;
      }
            
      // parse the optional args
      if((rc = proc_inst_cfg->pair_value()->readv(   "class",    kReqFl, pstate.proc_clas_label,
                                                     "args",     kOptFl, arg_dict,
                                                     "in",       kOptFl, pstate.in_dict_cfg,
                                                     "out",      kOptFl, pstate.out_dict_cfg,
                                                     "ui",       kOptFl, pstate.ui_cfg,
                                                     "preset",   kOptFl, pstate.preset_labels,
                                                     "presets",  kOptFl, pstate.presets_dict,
                                                     "network",  kOptFl, network,
                                                     "log",      kOptFl, pstate.log_labels )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"The proc instance cfg. '%s:%i' parse failed.",pstate.proc_label,pstate.proc_label_sfx_id);
        goto errLabel;        
      }

      // if an argument dict was given in the proc instance cfg
      if( arg_dict != nullptr  )
      {
        //bool rptErrFl = true;

        // verify the arg. dict is actually a dict.
        if( !arg_dict->is_dict() )
        {
          cwLogError(kSyntaxErrorRC,"The proc instance argument dictionary on proc instance '%s:%i' is not a dictionary.",pstate.proc_label,pstate.proc_label_sfx_id);
          goto errLabel;
        }


        pstate.arg_cfg = arg_dict;
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(kSyntaxErrorRC,"Configuration parsing failed on proc instance: '%s:%i'.", cwStringNullGuard(pstate.proc_label),pstate.proc_label_sfx_id);
      
      return rc;
    }


    //============================================================================================================================================
    //
    // Class Preset and Arg Value application
    //
    
    rc_t _var_channelize( proc_t* proc, const char* preset_label, const char* var_label, unsigned label_sfx_id, const object_t* value )
    {
      rc_t rc = kOkRC;
      
      variable_t* dummy = nullptr;
      var_desc_t* vd    = nullptr;

      // verify that a valid value exists
      if( value == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"Unexpected missig value on preset '%s' proc instance '%s:%i-%s:%i'.", preset_label, proc->label, proc->label_sfx_id, cwStringNullGuard(var_label), label_sfx_id );
        goto errLabel;
      }
      else
      {
        bool is_var_cfg_type_fl = (vd = var_desc_find( proc->class_desc, var_label ))!=nullptr && cwIsFlag(vd->type,kCfgTFl);
        bool is_list_fl         = value->is_list();
        bool is_list_of_list_fl = is_list_fl && value->child_count() > 0 && value->child_ele(0)->is_container();
        bool parse_list_fl      = (is_list_fl && !is_var_cfg_type_fl) || (is_list_of_list_fl && is_var_cfg_type_fl);

        // if a list of values was given and the var type is not a 'cfg' type or if a list of lists was given
        if( parse_list_fl )
        {
          // then each value in the list is assigned to the associated channel
          for(unsigned chIdx=0; chIdx<value->child_count(); ++chIdx)
            if((rc = var_channelize( proc, var_label, label_sfx_id, chIdx, value->child_ele(chIdx), kInvalidId, dummy )) != kOkRC )
              goto errLabel;
        }
        else // otherwise a single value was given
        {          
          if((rc = var_channelize( proc, var_label, label_sfx_id, kAnyChIdx, value, kInvalidId, dummy )) != kOkRC )
            goto errLabel;
        }
      }
        
    errLabel:
      return rc;
    }
    
    rc_t _preset_channelize_vars( proc_t* proc, const char* preset_label, const object_t* preset_cfg )
    {
      rc_t rc = kOkRC;

      //cwLogInfo("Channelizing '%s' preset %i vars for '%s'.",type_src_label, preset_cfg==nullptr ? 0 : preset_cfg->child_count(), proc->label );
      
      // validate the syntax of the preset record
      if( !preset_cfg->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The preset record '%s' on'%s' is not a dictionary.", preset_label, proc->class_desc->label );
        goto errLabel;
      }


      // for each preset variable
      for(unsigned i=0; i<preset_cfg->child_count(); ++i)
      {
        const object_t* value     = preset_cfg->child_ele(i)->pair_value();
        const char*     var_label = preset_cfg->child_ele(i)->pair_label();

        //cwLogInfo("variable:%s",var_label);
        
        if((rc = _var_channelize( proc, preset_label, var_label, kBaseSfxId, value )) != kOkRC )
          goto errLabel;
        
        
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Apply preset failed on proc instance:%s:%i class:%s preset:%s.", proc->label, proc->label_sfx_id, proc->class_desc->label, preset_label );

      return rc;
    }

    
    rc_t _class_preset_channelize_vars( proc_t* proc, const char* preset_label )
    {
      rc_t            rc = kOkRC;
      const class_preset_t* pr;

      if( preset_label == nullptr )
        return kOkRC;
      
      // locate the requestd preset record
      if((pr = class_preset_find(proc->class_desc, preset_label)) == nullptr )
      {
        rc = cwLogError(kInvalidIdRC,"The preset '%s' could not be found for the proc instance '%s:%i'.", preset_label, proc->label, proc->label_sfx_id);
        goto errLabel;
      }
      
      rc = _preset_channelize_vars( proc, preset_label, pr->cfg);
      
    errLabel:                  
      return rc;
    }


    rc_t _class_apply_presets( proc_t* proc, const object_t* preset_labels )
    {
      rc_t        rc = kOkRC;
      const char* s  = nullptr;
      
      // if preset_labels is a string
      if( preset_labels->is_string() && preset_labels->value(s)==kOkRC )
        return _class_preset_channelize_vars(proc,s);

      // if the preset_labels is not a list
      if( !preset_labels->is_list() )
        rc = cwLogError(kSyntaxErrorRC,"The preset list on proc instance '%s' is neither a list nor a string.",proc->label);
      else        
      {
        // preset_labels is a list.
        
        // for each label listed in the preset label list
        for(unsigned i=0; i<preset_labels->child_count(); ++i)
        {
          const object_t* label_obj = preset_labels->child_ele(i);

          // verify that the label is a strng
          if( !label_obj->is_string() || label_obj->value(s) != kOkRC )
          {
            rc = cwLogError(kSyntaxErrorRC,"The preset list does not contain string on proc instance '%s'.",proc->label);
            goto errLabel;
          }

          // apply a preset label
          if((rc = _class_preset_channelize_vars( proc, s)) != kOkRC )
            goto errLabel;          
        }
      }
      
    errLabel:
      return rc;
    }


    rc_t _process_args_stmt( proc_t* proc, const object_t* arg_cfg )
    {
      rc_t rc = kOkRC;
      
      if( arg_cfg == nullptr )
        return rc;
      
      unsigned argN = arg_cfg->child_count();

      for(unsigned i=0; i<argN; ++i)
      {
        const object_t* arg_pair = arg_cfg->child_ele(i);
        io_ele_t r;

        // validate the arg pair 
        if( arg_pair==nullptr || !arg_pair->is_pair() || arg_pair->pair_label()==nullptr || arg_pair->pair_value()==nullptr )
        {
          rc = cwLogError(kSyntaxErrorRC,"Invalid 'arg' pair.");
          goto errLabel;
        }

        // parse the var label string
        if((rc = _io_stmt_parse_ele( arg_pair->pair_label(), r  )) != kOkRC )
        {
          goto errLabel;
        }

        // if the arg expr is iterating but no count was given
        if(r.is_iter_fl && r.sfx_id_count == kInvalidCnt )
        {
          const var_desc_t* vd = nullptr;

          // get the var_desc for this var
          if( (vd=var_desc_find( proc->class_desc, r.label )) == nullptr )
          {
            rc = cwLogError(kSyntaxErrorRC,"The variable '%s', as referenced in the proc '%s' 'args' statement, was not found.",cwStringNullGuard(r.label),cwStringNullGuard(proc->label));
            goto errLabel;
          }
          else
          {
            // if this variable does not have a 'mult_ref' variable to get the iteration count then stop
            if(vd->mult_ref_var_label == nullptr )
            {
              rc = cwLogError(kSyntaxErrorRC,"The variable '%s', as referenced in the proc '%s' 'args' statement, is iterating but does not have a 'mult_ref' variable.",cwStringNullGuard(r.label),cwStringNullGuard(proc->label));
              goto errLabel;
            }
            else
            {
              unsigned sfx_id_cnt;

              // get the iteration count from the 'mult_ref' variable.
              if((sfx_id_cnt = var_mult_count(proc,vd->mult_ref_var_label)) != 0)
              {
                r.sfx_id_count = sfx_id_cnt;
              }
              else
              {
                rc = cwLogError(kSyntaxErrorRC,"The 'mult_ref' variable '%s', as referenced by the variable '%s' in the proc '%s' 'args' statement, is iterating but does not have a 'mult_ref' variable.",vd->mult_ref_var_label,cwStringNullGuard(r.label),cwStringNullGuard(proc->label));
                goto errLabel;
              }
            }
          }
        }
        
        // if the arg expr is not iterating then set the iter count to 1
        if( r.sfx_id_count == kInvalidCnt )
        {
          r.sfx_id_count = 1;
        }
        
        // if no base sfx id was given then set the base sfx id to kBaseSfxId
        if( r.base_sfx_id == kInvalidId )
          r.base_sfx_id = kBaseSfxId;

        // 
        for(unsigned sfx_id=r.base_sfx_id; sfx_id< r.base_sfx_id + r.sfx_id_count; ++sfx_id)
        {
          // if this var has not been created yet - then create it
          if( !var_exists(proc, r.label,sfx_id, kAnyChIdx) )
          {
            variable_t* dum = nullptr;
              
            if((rc = var_create( proc,
                                 r.label,
                                 sfx_id,
                                 kInvalidId,
                                 kAnyChIdx,
                                 nullptr,
                                 kInvalidTFl,
                                 dum )) != kOkRC )
            {
              rc = cwLogError(rc,"Variable create failed on '%s %s:%i'.",cwStringNullGuard(proc->label),cwStringNullGuard(r.label),sfx_id);
              goto errLabel;
            }
          }

          if((rc= _var_channelize( proc, "args",  r.label, sfx_id, arg_pair->pair_value() )) != kOkRC )
          {
            rc = cwLogError(rc,"Channeliize failed on '%s %s:%i'.",cwStringNullGuard(proc->label),cwStringNullGuard(r.label),sfx_id);
            goto errLabel;
          }
        }

        mem::release(r.label);
      }

        
    errLabel:
      if(rc != kOkRC )
        rc = cwLogError(rc,"'args' processing failed on proc '%s'.",cwStringNullGuard(proc->label));
      
      return rc;
    }


    /*
    rc_t  _process_net_preset(proc_t* proc, const object_t* net_preset_cfgD)
    {
      rc_t rc = kOkRC;
      const object_t* proc_preset_cfg;

      // if no network preset dict exists or if this proc. is not mentioned in it then there is nothing to do
      if( net_preset_cfgD==nullptr || (proc_preset_cfg=net_preset_cfgD->find_child(proc->label)) == nullptr )
        return rc;
      
      switch( proc_preset_cfg->type_id() )
      {
        case kDictTId:
          if((rc = _process_args_stmt(proc,proc_preset_cfg)) != kOkRC )
            goto errLabel;
          break;
          
        case kListTId:
        case kStringTId:
          if((rc = _class_apply_presets(proc,proc_preset_cfg)) != kOkRC )
            goto errLabel;
          break;
          
        default:
          rc = cwLogError(kInvalidStateRC,"A network preset must be either a dictionary, list or string.");
          goto errLabel;
          break;
      }
      
    errLabel:
      if(rc != kOkRC )
        rc = cwLogError(rc,"Network preset application failed for proc instance:%s.",cwStringNullGuard(proc->label));
      
      return rc;
    }
    */
    
    void _pstate_destroy( proc_inst_parse_state_t pstate )
    {
      _io_stmt_array_destroy(pstate.iStmtA,pstate.iStmtN);
      _io_stmt_array_destroy(pstate.oStmtA,pstate.oStmtN);

      mem::release(pstate.proc_label);
    }

    // Count of proc instances which exist in the network with a given class.
    unsigned _poly_copy_count( const network_t& net, const char* proc_clas_label )
    {
      unsigned n = 0;
      
      for(unsigned i=0; i<net.procN; ++i)
        if( textIsEqual(net.procA[i]->class_desc->label,proc_clas_label) )
          ++n;
      return n;
    }

    // This function is defined in cwFlow.cpp
    rc_t _create_preset_list( class_preset_t*& presetL, const object_t* presetD );

    
    rc_t _proc_create( flow_t*         p,
                       const object_t* proc_inst_cfg,
                       network_t&      net,
                       variable_t*     proxyVarL,
                       proc_t*&        proc_ref )
    {
      rc_t                    rc              = kOkRC;
      proc_inst_parse_state_t pstate          = {};
      proc_t*                 proc            = nullptr;
      class_desc_t*           class_desc      = nullptr;

      proc_ref = nullptr;

      // parse the proc instance configuration 
      if((rc = _proc_parse_cfg( net, proc_inst_cfg, pstate )) != kOkRC )
        goto errLabel;
      
      // locate the proc class desc
      if(( class_desc = class_desc_find(p,pstate.proc_clas_label)) == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"The flow class '%s' was not found.",cwStringNullGuard(pstate.proc_clas_label));
        goto errLabel;
      }

      // if the poly proc instance count has been exceeded for this proc inst class ...
      if(class_desc->polyLimitN > 0 && _poly_copy_count(net,pstate.proc_clas_label) >= class_desc->polyLimitN )
      {
        // ... then silently skip this instantiation
        cwLogDebug("The poly class copy count has been exceeded for '%s' - skipping instantiation of sfx_id:%i.",pstate.proc_label,pstate.proc_label_sfx_id);
        goto errLabel;
      }
      
      // instantiate the proc instance
      proc = mem::allocZ<proc_t>();

      proc->ctx           = p;
      proc->label         = mem::duplStr(pstate.proc_label);
      proc->label_sfx_id  = pstate.proc_label_sfx_id;
      proc->proc_cfg      = proc_inst_cfg->pair_value();
      proc->class_desc    = class_desc;
      proc->net           = &net;
      proc->logLevel      = log::kInvalid_LogLevel;

      TRACE_REG(proc->label,proc->label_sfx_id,proc->trace_id);

      // create the proc instance preset list
      if((rc = _create_preset_list(proc->presetL, pstate.presets_dict )) != kOkRC )
      {
        rc = cwLogError(rc,"Proc instance preset parse failed on proc instane: '%s:%i'.",cwStringNullGuard(proc->label),pstate.proc_label_sfx_id);
        goto errLabel;
      }

      
      // parse the in-list ,fill in pstate.in_array, and create var instances for var's referenced by in-list
      if((rc = _in_stmt_parse_in_list( net, proc, proxyVarL, pstate )) != kOkRC )
      {
        rc = cwLogError(rc,"in-list parse failed on proc inst instance '%s:%i'.",cwStringNullGuard(proc->label),pstate.proc_label_sfx_id);
        goto errLabel;
      }

      // if this is a subnet wrapper proc then create the vars that are connected to the proxy vars
      if((rc = _subnet_create_proxied_vars( proc, proxyVarL )) != kOkRC )
      {
        rc = cwLogError(rc,"Proxy vars create failed on proc inst instance '%s:%i'.",cwStringNullGuard(proc->label),pstate.proc_label_sfx_id);
        goto errLabel;
      }

      // Instantiate all the variables in the class description - that were not already created in _in_stmt_parse_in_list()
      // or _subnet_create_proxied_vars().
      for(var_desc_t* vd=class_desc->varDescL; vd!=nullptr; vd=vd->link)
        if( !_is_var_proc_already_created( proc, vd->label, pstate ) && cwIsNotFlag(vd->type,kRuntimeTFl) )
        {
          variable_t* var = nullptr;        
          if((rc = var_create( proc, vd->label, kBaseSfxId, kInvalidId, kAnyChIdx, vd->val_cfg, kInvalidTFl, var )) != kOkRC )
            goto errLabel;
        }

      // All the variables that can be used by this proc instance have now been created
      // and the chIdx of each variable is set to 'any'.
      
      // If a 'preset' field was included in the class cfg then apply the specified class preset
      // Note that the 'preset' field can be a string or list of strings.
      if( pstate.preset_labels != nullptr )      
        if((rc = _class_apply_presets(proc, pstate.preset_labels )) != kOkRC )
          goto errLabel;

      // The requested class presets values have now been set and those variables
      // that were expressed with a list have numeric channel indexes assigned.

      // Apply the proc inst instance 'args:{}' values.
      if( pstate.arg_cfg != nullptr )
      {
        if((rc = _process_args_stmt(proc,pstate.arg_cfg)) != kOkRC )
          goto errLabel;
      }

      // If the the network preset holds a preset for this proc
      //if( net_preset_cfgD!=nullptr && (proc_preset_cfg=net_preset_cfgD->find_child(proc->label)) != nullptr )
      //{
      //  if((rc = _process_net_preset(proc,net_preset_cfgD)) != kOkRC )
      //    goto errLabel;
      //}
      
      // All the proc instance arg values have now been set and those variables
      // that were expressed with a list have numeric channel indexes assigned.

      // Connect the var's that are referenced in the in-stmt to their respective sources
      if((rc = _io_stmt_connect_vars(net, proc, "in", "src", pstate.iStmtA, pstate.iStmtN)) != kOkRC )
      {
        rc = cwLogError(rc,"Input connection processing failed.");
        goto errLabel;
      }

      // Connect the proxied vars in this subnet proc to their respective proxy vars.
      if((rc = _subnet_connect_proxy_vars( proc, proxyVarL )) != kOkRC )
      {
        rc = cwLogError(rc,"Proxy connection processing failed.");
        goto errLabel;
      }

      // verify that the required fields exist in  all 'record' vars that act as 'src' variables 
      if((rc = _proc_verify_required_record_fields(proc)) != kOkRC )
        goto errLabel;      

      
      // Complete the instantiation of the proc inst instance by calling the custom proc instance creation function.

      // Call the custom proc instance create() function.
      if((rc = class_desc->members->create( proc )) != kOkRC )
      {
        rc = cwLogError(kInvalidArgRC,"Custom instantiation failed." );
        goto errLabel;
      }

      // Create the proc instance->varMap[] lookup array
      if((rc =_proc_create_var_map( proc )) != kOkRC )
      {
        rc = cwLogError(rc,"Variable map creation failed.");
        goto errLabel;
      }

      // create the feedback connections
      if((rc = _out_stmt_processing( net, proc, pstate )) != kOkRC )
      {
        rc = cwLogError(rc,"'out' statement processing failed.");
        goto errLabel;
      }
      
      // the custom creation function may have added channels to in-list vars fix up those connections here.
      //_complete_input_connections(proc);

      // set the log flags again so that vars created by the proc instance can be included in the log output
      if((rc = _proc_process_log_stmt(proc,pstate.log_labels)) != kOkRC )
        goto errLabel;

      // create a list of variables that require special notification handling
      _proc_create_manual_notification_array( proc );
      
      // call the 'notify()' function to inform the proc instance of the current value of all of it's variables.
      if((rc = _proc_do_pre_runtime_variable_notification( proc )) != kOkRC )
        goto errLabel;


      // parse the proc UI cfg record
      if(pstate.ui_cfg != nullptr )
        if((rc = _proc_parse_ui_cfg(proc,pstate)) != kOkRC )
          goto errLabel;

      // validate the proc's state.
      if((rc = proc_validate(proc)) != kOkRC )
      {
        rc = cwLogError(rc,"proc inst '%s:%i' validation failed.", cwStringNullGuard(proc->label),proc->label_sfx_id );
        goto errLabel;
      }

      proc_ref = proc;
      
    errLabel:
      if( rc != kOkRC )
      {
        rc = cwLogError(rc,"Proc instantiation failed on '%s:%i'.",cwStringNullGuard(pstate.proc_label),pstate.proc_label_sfx_id);
        proc_destroy(proc);
      }
      
      _pstate_destroy(pstate);
      
      return rc;      
    }

    //
    //  Network preset pair table
    //

    rc_t _network_preset_pair_count( const network_t& net, unsigned& count_ref  )
    {
      rc_t rc = kOkRC;
      
      count_ref = 0;
      unsigned n = 0;
      for(unsigned i=0; i<net.procN; ++i)
      {
        const proc_t* proc = net.procA[i];
        for(const variable_t* var=proc->varL; var!=nullptr; var=var->var_link)
          if( var->chIdx == kAnyChIdx )
          {
            unsigned varChCnt = 0;
            if((rc = var_channel_count(var,varChCnt)) != kOkRC )
            {
              rc = cwLogError(rc,"The network preset pair count operation failed.");
              goto errLabel;
            }
            
            n += varChCnt + 1; // Add 1 for the kAnyCh
          }
      }
      
      count_ref = n;
    errLabel:
      
      return rc;
    }

    rc_t _network_preset_pair_fill_table( const network_t& net, network_preset_pair_t* nppA, unsigned nppN)
    {
      rc_t     rc = kOkRC;
      unsigned j  = 0;
      for(unsigned i=0; i<net.procN; ++i)
      {
        const proc_t* proc = net.procA[i];
        for(const variable_t* var=proc->varL; var!=nullptr; var=var->var_link)
          if( var->chIdx == kAnyChIdx )
          {
            unsigned varChCnt                          = 0;
            if((rc = var_channel_count(var,varChCnt)) != kOkRC )
              goto errLabel;

            unsigned k=0;
            for(const variable_t* ch_var=var; ch_var!=nullptr; ch_var = ch_var->ch_link, ++j,++k )
            {
              if( j >= nppN )
              {
                rc = cwLogError(kInvalidStateRC,"Unexpected end of preset-pair table was encountered.");
                goto errLabel;
              }
              
              nppA[j].proc  = proc;
              nppA[j].var   = ch_var;
              nppA[j].chIdx = var->chIdx;;
              nppA[j].chN   = varChCnt;;
            }

            if( k != varChCnt+1 )
            {
              rc = cwLogError(kInvalidStateRC,"An inconsistent var channel count was encountered on '%s:%i'-'%s:%i'.",
                              cwStringNullGuard(proc->label),proc->label_sfx_id,
                              cwStringNullGuard(var->label),var->label_sfx_id);
              goto errLabel;
            }
            
          }
      }

      if( j != nppN )
        rc = cwLogError(kInvalidStateRC,"The expected count of entries in the preset_pair table (%i) does not match the actual count (%i).",nppN,j);

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Preset pair table fill failed.");
      return rc;
    }

    rc_t _network_preset_pair_create_table( network_t& net )
    {
      rc_t     rc         = kOkRC;
      unsigned pair_count = 0;

      // get the total count of variables in this network
      if((rc = _network_preset_pair_count(net, pair_count  )) != kOkRC )
        goto errLabel;
      
      // allocate the preset pair table
      net.preset_pairA = mem::allocZ<network_preset_pair_t>(pair_count);
      net.preset_pairN = pair_count;

      // fill the preset pair table
      if((rc= _network_preset_pair_fill_table(net, net.preset_pairA, net.preset_pairN)) != kOkRC )
        goto errLabel;
      

    errLabel:
      if( rc != kOkRC )
      {
        rc = cwLogError(rc,"Network preset pair table create failed.");
        mem::release(net.preset_pairA);
        net.preset_pairN = 0;
      }
      
      return rc;
    }

    unsigned _network_preset_pair_find_index( const network_t& net, const variable_t* var )
    {      
      for(unsigned i=0; i<net.preset_pairN; ++i)
      {
        const network_preset_pair_t* npp = net.preset_pairA + i;
        
        if( var->proc == npp->proc && var == npp->var )
        {
          assert( var->chIdx == npp->var->chIdx );
          return i;
        }
      }

      return kInvalidIdx;
    }

    //==================================================================================================================
    //
    //  Preset processing
    //
    
    rc_t _parse_network_proc_label( const network_t& net, const network_preset_t& network_preset, const char* proc_label, io_ele_t& proc_id )
    {
      rc_t rc = kOkRC;
      
      // parse the proc label
      if((rc= _io_stmt_parse_ele( proc_label, proc_id )) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"Parse failed on the proc label '%s' of preset '%s'.",cwStringNullGuard(proc_label),cwStringNullGuard(network_preset.label));
        goto errLabel;
      }

      // if the proc label sfx id was not explicitely set then set it to the network poly index
      if( proc_id.base_sfx_id == kInvalidId )
        proc_id.base_sfx_id = net.poly_idx;

      // if not iterating
      if( !proc_id.is_iter_fl )
      {
        proc_id.sfx_id_count = 1;
      }
      else
      {
        if( proc_id.sfx_id_count == kInvalidCnt )
          proc_id.sfx_id_count = proc_mult_count(net, proc_id.label );
      }

    errLabel:
      return rc;
    }

    rc_t _parse_network_proc_var_label( network_t& net, const char* network_preset_label, const object_t* var_pair, const char* proc_label, unsigned proc_label_sfx_id, io_ele_t& var_id )
    {
      rc_t        rc        = kOkRC;
      const char* var_label = nullptr;
          
      if( var_pair==nullptr || !var_pair->is_pair() || (var_label=var_pair->pair_label())==nullptr || var_pair->pair_value() == nullptr )
      {
        rc = cwLogError(kSyntaxErrorRC,"A syntax error was encountered on a preset pair value on preset '%s:%s'.",cwStringNullGuard(network_preset_label),cwStringNullGuard(proc_label));
        goto errLabel;
      }

      // 
      if((rc= _io_stmt_parse_ele( var_label, var_id )) != kOkRC )
      {
        rc = cwLogError(rc,"Parse failed on the var label of preset '%s:%s:%s'.",cwStringNullGuard(network_preset_label),cwStringNullGuard(proc_label),cwStringNullGuard(var_label));
        goto errLabel;
      }

      // set the var_id base sfx id
      if( var_id.base_sfx_id == kInvalidId )
        var_id.base_sfx_id = kBaseSfxId;

      // if not iterating
      if( !var_id.is_iter_fl )
      {
        var_id.sfx_id_count = 1;
      }
      else
      {

        proc_t* proc;

        // find the var proc
        if((proc = proc_find(net,proc_label,proc_label_sfx_id)) == nullptr )
        {
          rc = cwLogError(kEleNotFoundRC,"The proc '%s' could not be found for network preset '%s'.",cwStringNullGuard(proc_label),cwStringNullGuard(network_preset_label));
          goto errLabel;
        }
                  
        if( var_id.sfx_id_count == kInvalidCnt )
          var_id.sfx_id_count = var_mult_count(proc, var_id.label );
      }
      
    errLabel:
      return rc;
      
    }

    rc_t _network_preset_get_preset_cfg_dict(  network_t&       net,
                                           const char*      network_preset_label,
                                           const char*      proc_label,
                                           unsigned         proc_sfx_id,
                                           const char*      class_preset_label,
                                           const object_t*& class_preset_cfg_ref )
    {
      rc_t                  rc                 = kOkRC;
      const class_preset_t* class_preset       = nullptr;
      proc_t*               proc               = nullptr;

      class_preset_cfg_ref = nullptr;
      
      // locate the proc this preset will be applied to
      if((proc = proc_find(net, proc_label, proc_sfx_id )) == nullptr )
      {
        rc = cwLogError(rc,"The proc '%s:%i' could not be found for the preset:'%s'",cwStringNullGuard(proc_label),proc_sfx_id,cwStringNullGuard(network_preset_label));
        goto errLabel;
      }

      // look for the preset in the proc inst first
      if((class_preset = proc_preset_find( proc, class_preset_label)) == nullptr )
      {
      
        // get the preset record for this proc/preset_label
        if((class_preset = class_preset_find( proc->class_desc, class_preset_label )) == nullptr )
        {
          rc = cwLogError(rc,"The class preset '%s' for proc '%s:%i' could not be found.",cwStringNullGuard(network_preset_label),cwStringNullGuard(proc_label),proc_sfx_id);
          goto errLabel; 
        }
      }
        
      class_preset_cfg_ref = class_preset->cfg;

    errLabel:
      return rc;
    }

    void _network_preset_link_in_value( network_preset_t& network_preset, preset_value_t* preset_value )
    {
      if( network_preset.u.vlist.value_head == nullptr )
        network_preset.u.vlist.value_head = preset_value;
      else
        network_preset.u.vlist.value_tail->link = preset_value;
      
      network_preset.u.vlist.value_tail = preset_value;
    }

    rc_t _network_preset_create_channel_value( network_t&        net,
                                               network_preset_t& network_preset,
                                               proc_t*           proc,
                                               variable_t*       var,
                                               unsigned          chN,
                                               const object_t*   value_cfg )
    {
      rc_t            rc           = kOkRC;
      unsigned        pairTblIdx   = kInvalidIdx;
      preset_value_t* preset_value = mem::allocZ<preset_value_t>();

      // cfg to value
      if((rc = value_from_cfg( value_cfg, preset_value->u.pvv.value )) != kOkRC )
      {
        rc = cwLogError(rc,"The preset cfg to value conversion failed on '%s:%i'-'%s:%i'.",cwStringNullGuard(var->label),var->label_sfx_id,cwStringNullGuard(proc->label),proc->label_sfx_id);
        goto errLabel;
      }

      // locate the the pair table index for this variable (this index is will be used for 'dual' preset processing)
      if((pairTblIdx = _network_preset_pair_find_index(net, var )) == kInvalidIdx )
      {
        rc = cwLogError(rc,"The preset pair record could not be found for '%s:%i'-'%s:%i'.",cwStringNullGuard(var->label),var->label_sfx_id,cwStringNullGuard(proc->label),proc->label_sfx_id);
        goto errLabel;        
      }

      preset_value->tid              = kDirectPresetValueTId;
      preset_value->u.pvv.proc       = proc;
      preset_value->u.pvv.var        = var;
      preset_value->u.pvv.pairTblIdx = pairTblIdx;

      _network_preset_link_in_value(network_preset,preset_value);
      
      
      //printf("%s%s %s:%i: %s:%i\n",(network_preset.u.vlist.value_head == preset_value ? "HEAD ":""),network_preset.label,preset_value->proc->label,preset_value->proc->label_sfx_id,preset_value->var->label,preset_value->var->label_sfx_id);

    errLabel:
      if(rc != kOkRC )
        _preset_value_destroy(preset_value);
      
      return rc;
    }

    rc_t _network_preset_find_or_create_variable( proc_t* proc, const char* var_label, unsigned var_sfx_id, unsigned chIdx, const object_t* value_cfg, bool allow_create_fl, variable_t*& var_ref )
    {
      rc_t        rc  = kOkRC;
      variable_t* var = nullptr;
      var_ref         = nullptr;
      
      // if the var was not found on 'chIdx'
      if((rc = var_find(proc, var_label, var_sfx_id, chIdx, var )) != kOkRC )
      {

        if( !allow_create_fl )
        {
          rc = cwLogError(kEleNotFoundRC,"The preset variable '%s:%i' ch:%i could not be found on proc: '%s:%i'.",
                          cwStringNullGuard(var_label),var_sfx_id,chIdx, cwStringNullGuard(proc->label),proc->label_sfx_id );
          goto errLabel;
        }
        else
        {
          //variable_t* base_var                                             = nullptr;
          // get the base var 
          if((rc = var_find(proc, var_label, kBaseSfxId, kAnyChIdx, var)) != kOkRC )
          {
            rc = cwLogError(rc,"The base variable '%s:%i' ch:%i could not be found to pre-emptively create the variable '%s:%i' ch:%i on proc: '%s:%i'.",
                            cwStringNullGuard(var_label),kBaseSfxId,kAnyChIdx,
                            cwStringNullGuard(var_label),var_sfx_id,chIdx,
                            cwStringNullGuard(proc->label),proc->label_sfx_id);
            goto errLabel;
          }

          // create the variable
          if((rc = var_create( proc, var_label, var_sfx_id, kInvalidId, chIdx, value_cfg, kInvalidTFl, var )) != kOkRC )
          {
            rc = cwLogError(rc,"Pre-emptive variable creation failed for '%s:%i' ch:%i on proc:'%s:%i'.",
                            cwStringNullGuard(var_label),var_sfx_id,chIdx,
                            cwStringNullGuard(proc->label),proc->label_sfx_id);
            goto errLabel;
          }
        }        
      }

      var_ref = var;
      
    errLabel:
      return rc;
    }
      
    
    rc_t _network_preset_create_value( network_t&        net,
                                       network_preset_t& network_preset,
                                       const char*       proc_label,
                                       unsigned          proc_sfx_id,
                                       const char*       var_label,
                                       unsigned          var_sfx_id,
                                       const object_t*   value_cfg )
    {
      rc_t        rc       = kOkRC;
      var_desc_t* var_desc = nullptr;
      proc_t*     proc     = nullptr;
      
      
      // locate the proc this preset will be applied to
      if((proc = proc_find(net, proc_label, proc_sfx_id )) == nullptr )
      {
        rc = cwLogError(kOpFailRC,"The proc '%s:%i' does not exist.",cwStringNullGuard(proc_label),proc_sfx_id);
        goto errLabel;
      }
      else
      {
        bool is_var_cfg_type_fl = (var_desc = var_desc_find( proc->class_desc, var_label ))!=nullptr && cwIsFlag(var_desc->type,kCfgTFl);
        bool is_list_fl         = value_cfg->is_list();
        bool is_list_of_list_fl = is_list_fl && value_cfg->child_count() > 0 && value_cfg->child_ele(0)->is_list();
        bool parse_list_fl      = (is_list_fl && !is_var_cfg_type_fl) || (is_list_of_list_fl && is_var_cfg_type_fl);

        // Case 1: By default we assume a single variable instance on channel 'kAnyChIdx' ....
        unsigned        valueN = 1;
        unsigned        chIdx  = kAnyChIdx;
        const object_t* vobj   = value_cfg;

        // Case 2: ... however if a list of preset values was given and the var type is not a 'cfg' type or if a list of lists was given
        // then we are going to iterate through a list of preset values each on a successive channel index
        if( parse_list_fl )
        {
          chIdx  = 0;
          valueN = value_cfg->child_count();
          //vobj   = value_cfg->child_ele(0);
        }
      
        // Iterate over each channel
        for(unsigned i = 0; i<valueN; ++i,chIdx++)
        {
          variable_t* var = nullptr;

          // Case 2:
          if( parse_list_fl )
            vobj   = value_cfg->child_ele(i);
          

          // find the var referenced in the preset
          if((rc = _network_preset_find_or_create_variable( proc, var_label, var_sfx_id, chIdx, vobj, false, var )) != kOkRC )
            goto errLabel;

          // create a 'preset_value_t' record and prepend it to 'network_preset.valueL'
          if((rc = _network_preset_create_channel_value(net, network_preset, proc, var, valueN, vobj )) != kOkRC )
            goto errLabel;
          

        }
        //printf("%s %s:%i-%s:%i\n",network_preset.label,proc_label,proc_sfx_id,var_label,var_sfx_id);
      }
    errLabel:
      
      return rc;
    }

    rc_t _network_preset_parse_dual_label(network_t& net, const object_t* list_cfg, unsigned idx, const char* pri_sec_label, const char* network_preset_label, const network_preset_t*& vlist_ref )
    {
      rc_t rc = kOkRC;
      const char* preset_label = nullptr;
      //const preset_value_list_t* vlist = nullptr;

      vlist_ref = nullptr;
      
      if( !list_cfg->child_ele(idx)->is_string() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The dual preset list %s preset is not a string on network preset:'%s'.",pri_sec_label,cwStringNullGuard(network_preset_label));
        goto errLabel;
      }

      if((rc = list_cfg->child_ele(idx)->value(preset_label)) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"The dual preset %s preset could not be parsed on network preset:'%s'.",pri_sec_label,cwStringNullGuard(network_preset_label));
        goto errLabel;
      }

      if((vlist_ref = network_preset_from_label(net, preset_label )) == nullptr )
      {
        rc = cwLogError(kEleNotFoundRC,"The dual preset %s preset could not be found on network preset:'%s'.",pri_sec_label,cwStringNullGuard(network_preset_label));
        goto errLabel;
      }

    errLabel:
      return rc;
    }
    
    rc_t _network_preset_parse_dual(flow_t* p, network_t& net, const object_t* dual_list_cfg, network_preset_t& network_preset )
    {
      rc_t rc = kOkRC;


      if( dual_list_cfg==nullptr || !dual_list_cfg->is_list() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The dual preset specification is not a list on network preset:'%s'.",cwStringNullGuard(network_preset.label));
        goto errLabel;
      }

      if( dual_list_cfg->child_count() != 3 )
      {
        rc = cwLogError(kSyntaxErrorRC,"The dual preset list does not have 3 elements on network preset:'%s'.",cwStringNullGuard(network_preset.label));
        goto errLabel;
      }

      if((rc = _network_preset_parse_dual_label(net,dual_list_cfg, 0, "primary", network_preset.label, network_preset.u.dual.pri )) != kOkRC )
        goto errLabel;
      

      if((rc = _network_preset_parse_dual_label(net,dual_list_cfg, 1, "secondary", network_preset.label, network_preset.u.dual.sec )) != kOkRC )
        goto errLabel;
        
      
      if((rc = dual_list_cfg->child_ele(2)->value(network_preset.u.dual.coeff)) != kOkRC )
      {
        rc = cwLogError(kSyntaxErrorRC,"The dual preset coeff could not be parsed on network preset:'%s'.",cwStringNullGuard(network_preset.label));
        goto errLabel;
      }
      
      network_preset.tid = kPresetDualTId;


    errLabel:
      return rc;
    }

    rc_t _network_preset_handle_poly_preset_ref(network_preset_t& network_preset, network_t* poly_net, const char* preset_label )      
    {
      rc_t            rc           = kOkRC;
      preset_value_t* preset_value = nullptr;

      network_preset_t* poly_net_preset = nullptr;
      
      for(unsigned i=0; i<poly_net->presetN; ++i)
        if( textIsEqual(poly_net->presetA[i].label,preset_label) )
        {
          poly_net_preset = poly_net->presetA + i;
          break;
        }

      if( poly_net_preset == nullptr )
      {
        rc = cwLogError(kEleNotFoundRC,"The preset '%s' could not be found on the net '%s:%i'.",preset_label,cwStringNullGuard(poly_net->label),poly_net->poly_idx);
        goto errLabel;
      }

      preset_value = mem::allocZ<preset_value_t>();

      preset_value->tid                   = kNetRefPresetValueTId;
      preset_value->u.npv.net_preset      = poly_net_preset;
      preset_value->u.npv.net_preset_net  = poly_net;
        
      _network_preset_link_in_value( network_preset, preset_value );

            
    errLabel:
      return rc;
    }

    rc_t _network_preset_handle_poly_preset_reference(network_preset_t& network_preset, proc_t* poly_proc, const io_ele_t& proc_id, const char* preset_label )
    {
      rc_t rc = kOkRC;
      
      const class_preset_t* class_pre;

      unsigned min_sfx_id = proc_id.base_sfx_id;
      unsigned sfx_cnt    = proc_id.is_iter_fl ? proc_id.sfx_id_count : (proc_id.has_sfx_fl ? 1 : poly_proc->internal_net->polyN);

      network_t*      poly_net     = poly_proc->internal_net;
      
      // for each specified poly net
      for(; poly_net!=nullptr; poly_net=poly_net->poly_link)
        if( min_sfx_id <= poly_net->poly_idx && poly_net->poly_idx < min_sfx_id + sfx_cnt )
        {
          // if this network is not a het. net then the preset can be directly found in the network preset list
          if( poly_proc->internal_net_cnt == 1 )
            rc = _network_preset_handle_poly_preset_ref(network_preset, poly_net, preset_label);
          else
          {
            // the network is het. net and so the preset must be looked up first in the poly proc instance preset list
            // and then mapped to each of the named networks.
            if((class_pre = proc_preset_find(poly_proc,preset_label)) != nullptr )
            {
              // ... the referenced preset ia a poly preset instance preset ....
              unsigned childN = class_pre->cfg->child_count();
        
              // ... iterate through each of the elements of the preset dictionary
              for(unsigned i=0; i<childN; ++i)
              {
                const object_t* pair             = class_pre->cfg->child_ele(i);
                const char*     net_preset_label = nullptr;
                const char*     net_label        = pair->pair_label();

                // pair = (net-label:net-preset-label)

                // the het. net referenced in the in preset must match the current net  (poly_net) label
                if( textIsEqual(net_label,poly_net->label) )
                {

                  // get the net specific preset label
                  if( pair->pair_value()->value(net_preset_label) != kOkRC )
                  {
                    rc = cwLogError(kSyntaxErrorRC,"The value associated with '%s' is not a string.",pair->pair_label());
                    goto errLabel;
                  }


                  if((rc = _network_preset_handle_poly_preset_ref(network_preset, poly_net, net_preset_label )) != kOkRC )
                  {
                    goto errLabel;
                  }
                }              
          
              }
            }
          }
        }      
    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"The poly processor preset '%s' on '%s:%i' is not valid.",cwStringNullGuard(preset_label),cwStringNullGuard(poly_proc->label),poly_proc->label_sfx_id);
      
      return rc;
    }

    
    rc_t _network_preset_parse_value_list( flow_t* p,
                                           network_t& net,
                                           const object_t* network_preset_dict_cfg,
                                           network_preset_t& network_preset )
    {
      rc_t rc = kOkRC;
      unsigned pairN = 0;
      
      if( network_preset_dict_cfg==nullptr || !network_preset_dict_cfg->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The proc preset dictionary is not a dictionary in network preset:'%s'.",cwStringNullGuard(network_preset.label));
        goto errLabel;
      }

      network_preset.tid = kPresetVListTId;
      
      pairN = network_preset_dict_cfg->child_count();

      // for each pair in the preset value dictionary
      for(unsigned i=0; i<pairN; ++i)
      {
        const object_t* var_dict         = nullptr;
        const object_t* proc_preset_pair = network_preset_dict_cfg->child_ele(i);
        const char*     proc_label       = nullptr;
        io_ele_t        proc_id          = {};
        unsigned        varN             = 0;

        
        // validate the process preset syntax
        if( proc_preset_pair==nullptr || !proc_preset_pair->is_pair() || (proc_label=proc_preset_pair->pair_label())==nullptr || proc_preset_pair->pair_value()==nullptr )
        {
          rc = cwLogError(kSyntaxErrorRC,"A syntax error was encountered on a preset pair on preset '%s'.",cwStringNullGuard(network_preset.label));
          goto errLabel;
        }

        // each pair has the syntax:
        // <proc-label>:<preset_value>
        // where:
        // <proc-label> identifies a proc inside this network where the <preset-value> will be applied.
        // <preset-value> idenfifies a label or a dictionary of <var>:<value> pairs.
        //
        

        // parse the label
        if((rc= _parse_network_proc_label(net, network_preset, proc_label, proc_id )) != kOkRC )
          goto errLabel;

        // if the preset-value is a label ... 
        if( proc_preset_pair->pair_value()->is_string() )
        {
          const char* proc_preset_label = nullptr;
          proc_t* proc = nullptr;


          // ... then the label refers to a proc where the proc may be
          // one of two types:
          //
          // 1) 'poly' proc - in which case we need to a poly_preset_value_t
          // record for each poly channel -
          //
          // 2) a proc instance in this network
          // in which case the label refers to a class or instance preset
          // and we store proc_var_value_t for each variable referenced in the preset cfg.
          
          // get the proc preset label
          if((proc_preset_pair->pair_value()->value(proc_preset_label)) != kOkRC )
          {
            rc = cwLogError(rc,"The proc preset label '%s' could not be parsed on the preset:'%s'",cwStringNullGuard(proc_preset_label),cwStringNullGuard(proc_label));
            goto errLabel;
          }
          
          
          // locate the proc this preset will be applied to
          if((proc = proc_find(net, proc_id.label, net.poly_idx )) == nullptr )
          {
            rc = cwLogError(rc,"The proc '%s:%i' could not be found for the preset:'%s'",cwStringNullGuard(proc_id.label),proc_id.sfx_id,cwStringNullGuard(network_preset.label));
            goto errLabel;
          }

          // if this is a 'poly' proc
          if( proc->internal_net != nullptr  )
          {
            
            if((rc = _network_preset_handle_poly_preset_reference(network_preset,proc,proc_id,proc_preset_label)) != kOkRC )
              goto errLabel;

            // the referenced preset was added to the network_preset.u.vlist - no further processing is required
            continue;
            
          }
          else
          {
            // get the referenced preset preset cfg 
            if((rc = _network_preset_get_preset_cfg_dict( net, network_preset.label, proc_id.label,  proc_id.base_sfx_id,  proc_preset_label, var_dict )) != kOkRC )
              goto errLabel;                           
          }
          
        }
        else // the preset is a dictionary of var/value pairs
        {
          // if preset is not a dictionary 
          if( !proc_preset_pair->pair_value()->is_dict() )
          {
            rc = cwLogError(kSyntaxErrorRC,"The preset value dictionary for '%s:%s' is not valid.",cwStringNullGuard(network_preset.label),cwStringNullGuard(proc_id.label));
            goto errLabel;
          }

          var_dict = proc_preset_pair->pair_value();
        }

        if(var_dict == nullptr )
        {
          rc = cwLogError(kSyntaxErrorRC,"The preset '%s' in '%s' is not valid.",cwStringNullGuard(network_preset.label),cwStringNullGuard(proc_id.label));
          goto errLabel;          
        }
        

        // var_dict now refers to a dictionary of var/value pairs for a single proc

        varN = var_dict->child_count();

        // for each proc/sf_id  (the proc label may refer to multiple proc instances)

        // This loop is designed to handle the case where the preset proc. label uses underscore notation
        // to address specific vs all instances of a given named proc's.
        // If no underscore notation is used then it addresses all instances with the
        // specified name.
        
        for(unsigned j=0; j<proc_id.sfx_id_count && rc==kOkRC; ++j)
        {

          // for each variable label:value pair
          for(unsigned k=0; k<varN; ++k)
          {
            io_ele_t        var_id            = {};
            const object_t* var_pair          = var_dict->child_ele(k);
            unsigned        proc_label_sfx_id = proc_id.base_sfx_id + j;

            // if this net is part of a poly net - but the proc sfx id does not match the poly index
           if( net.polyN>1 && proc_label_sfx_id != net.poly_idx )
              continue;
              
            // parse the preset var label - the var may use underscore notation to address multiple vars
            if((rc = _parse_network_proc_var_label(net, network_preset.label, var_pair, proc_id.label, proc_label_sfx_id, var_id )) == kOkRC )
            {
              // create a preset for each var:sfx_id pair (the var label may refer to multiple var instances) 
              for(unsigned m=0; m<var_id.sfx_id_count; ++m)
                if((rc = _network_preset_create_value( net, network_preset, proc_id.label, proc_label_sfx_id, var_id.label, var_id.base_sfx_id + m, var_pair->pair_value() )) != kOkRC )
                  break;
            }
            
            mem::release(var_id.label);                
          }          
        }

        // BUG BUG BUG: This procid.label is not being cleaned if certain errors are thrown
        
        mem::release(proc_id.label);
      }
      
    errLabel:
      return rc;
    }

    
    rc_t _network_preset_parse_dict( flow_t* p, network_t& net, const object_t* preset_cfg )
    {
      rc_t rc = kOkRC;
      unsigned presetAllocN = 0;
      
      if( preset_cfg == nullptr )
        return rc;
      
      if( !preset_cfg->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The network preset list is not a dictionary.");
        goto errLabel;
      }

      presetAllocN = preset_cfg->child_count();
      net.presetA  = mem::allocZ<network_preset_t>(presetAllocN);
      net.presetN  = 0;

      // parse each preset_label pair
      for(unsigned i=0; i<presetAllocN; ++i)
      {
        const object_t* preset_pair_cfg = preset_cfg->child_ele(i);
        network_preset_t&  network_preset     = net.presetA[i];

        // validate the network preset pair
        if( preset_pair_cfg==nullptr || !preset_pair_cfg->is_pair() || (network_preset.label = preset_pair_cfg->pair_label())==nullptr || preset_pair_cfg->pair_value()==nullptr )
        {
          rc = cwLogError(kSyntaxErrorRC,"Invalid syntax encountered on a network preset.");
          goto errLabel;
        }

        switch( preset_pair_cfg->pair_value()->type_id() )
        {
          case kDictTId: // 'value-list' preset
            if((rc = _network_preset_parse_value_list(p, net, preset_pair_cfg->pair_value(), network_preset)) != kOkRC )
            {
              rc = cwLogError(kSyntaxErrorRC,"Network value-list preset parse failed on preset:'%s'.",cwStringNullGuard(network_preset.label));
              goto errLabel;            
            }
            break;

          case kListTId: // dual preset
            if((rc = _network_preset_parse_dual(p, net, preset_pair_cfg->pair_value(), network_preset)) != kOkRC )
            {
              rc = cwLogError(kSyntaxErrorRC,"Network dual preset parse failed on preset:'%s'.",cwStringNullGuard(network_preset.label));
              goto errLabel;              
            }
            break;

          default:
            rc = cwLogError(kAssertFailRC,"Unknown preset type on network preset: '%s'.",cwStringNullGuard(network_preset.label));
            goto errLabel;
        }

        
        net.presetN += 1;
      }
      
    errLabel:
      if(rc != kOkRC )
      {
        _network_preset_array_destroy(net);
      }

      return rc;
    }

    // Given a network preset label return the dictionary of proc presets that is associated with it.
    /*
    rc_t _get_network_preset_cfg( const object_t* presetsCfg, const char* preset_label, const object_t*& preset_ref )
    {
      rc_t rc = kOkRC;
      preset_ref = nullptr;
      
      if( preset_label == nullptr )
        return rc;

      if( presetsCfg == nullptr )
      {
        rc = cwLogError(kInvalidArgRC,"The network preset '%s' could not be found because there is no network 'presets' dictionary.",cwStringNullGuard(preset_label));
        goto errLabel;
      }

      if((preset_ref = presetsCfg->find_child( preset_label)) == nullptr )
      {
        rc = cwLogError(kEleNotFoundRC,"The network preset '%s' was not found in the 'presets' dictionary.");
        goto errLabel;
      }

      switch( preset_ref->type_id() )
      {
        case kDictTId: // this is a dictionary of proc presets
          break;
          
        case kListTId: // this is a 'dual' preset - skip it
          preset_ref = nullptr;
          break;
          
        default:
          rc = cwLogError(kSyntaxErrorRC,"Network presets must be either dictionaries or lists. Preset '%s' is neither.",cwStringNullGuard(preset_label));
          preset_ref = nullptr;
      }
      
    errLabel:
      return rc;
        
    }
    */
    

    template<typename T0, typename T1 >
    rc_t _preset_set_var_from_dual_interp_1( variable_t* var, T0 v0, T1 v1, double coeff )
    {
      return var_set(var, (T0)(v0 + (v1-v0)*coeff ));
    }
    
    template< typename T >
    rc_t _preset_set_var_from_dual_interp_0( variable_t* var,  T v0, const value_t* v1, double coeff )
    {
      rc_t rc = kOkRC;
      switch( v1->tflag & kTypeMask )
      {
        case kUIntTFl:
          rc = _preset_set_var_from_dual_interp_1(var,v0,v1->u.u,coeff);
          break;
          
        case kIntTFl:
          rc = _preset_set_var_from_dual_interp_1(var,v0,v1->u.i,coeff);
          break;
          
        case kFloatTFl:
          rc = _preset_set_var_from_dual_interp_1(var,v0,v1->u.f,coeff);
          break;
          
        case kDoubleTFl:          
          rc = _preset_set_var_from_dual_interp_1(var,v0,v1->u.d,coeff);
          break;
          
        default:
          rc = cwLogError(kInvalidDataTypeRC,"The second operand of a set by interpolation had a non-numeric data type.");
      }
      return rc;
    }

  

    rc_t _preset_set_var_from_dual( const preset_value_t* preset_val, const value_t* value_1, double coeff )
    {
      rc_t rc = kOkRC;
      unsigned legalTypeMask = kUIntTFl | kIntTFl | kFloatTFl | kDoubleTFl;

      if(preset_val->tid != kDirectPresetValueTId )
      {
        rc = cwLogError(rc,"A poly-preset was encountered while setting a dual value.");
        goto errLabel;
      }
      
      if( value_1 == nullptr || (preset_val->u.pvv.value.tflag & legalTypeMask)==0 )
        rc = var_set( preset_val->u.pvv.var, &preset_val->u.pvv.value );
      else
      {
        if( (value_1->tflag & legalTypeMask) == 0 )
        {
          rc = cwLogError(kInvalidDataTypeRC,"The type of value-1 (0x%x) is not a scalar number.",value_1->tflag);
          goto errLabel;
        }

        switch( preset_val->u.pvv.value.tflag & legalTypeMask )
        {
          case kUIntTFl:
            rc = _preset_set_var_from_dual_interp_0( preset_val->u.pvv.var, preset_val->u.pvv.value.u.u, value_1, coeff );
            break;
            
          case kIntTFl:
            rc = _preset_set_var_from_dual_interp_0( preset_val->u.pvv.var, preset_val->u.pvv.value.u.i, value_1, coeff );
            break;
            
          case kFloatTFl:
            rc = _preset_set_var_from_dual_interp_0( preset_val->u.pvv.var, preset_val->u.pvv.value.u.f, value_1, coeff );
            break;
            
          case kDoubleTFl:
            rc = _preset_set_var_from_dual_interp_0( preset_val->u.pvv.var, preset_val->u.pvv.value.u.d, value_1, coeff );
            break;

        default:
          rc = cwLogError(kInvalidDataTypeRC,"The first operand of a set by interpolation had a non-numeric data type.");
            
        }
      }

    errLabel:
      if(rc != kOkRC )
        if( preset_val->tid == kDirectPresetValueTId )
          rc = cwLogError(rc,"Set variable from dual preset failed on '%s:%i'-'%s:%i' ch:0.",
                          cwStringNullGuard(preset_val->u.pvv.proc->label),preset_val->u.pvv.proc->label_sfx_id,
                          cwStringNullGuard(preset_val->u.pvv.var->label),preset_val->u.pvv.var->label_sfx_id,
                          preset_val->u.pvv.var->chIdx);
      return rc;
    }

    rc_t _network_apply_preset( network_t& net, const network_preset_t* network_preset, unsigned proc_label_sfx_id );
    
    rc_t _network_apply_vlist_preset( network_t& net,  const preset_value_list_t* vlist, unsigned proc_label_sfx_id )
    {
      rc_t                  rc  = kOkRC;
      const preset_value_t* psv = nullptr;

      for(psv=vlist->value_head; psv!=nullptr; psv=psv->link)
      {
        bool apply_to_all_poly_channels_fl = proc_label_sfx_id==kInvalidId;
        bool apply_to_this_poly_channel_fl = psv->tid==kDirectPresetValueTId && psv->u.pvv.proc->label_sfx_id == proc_label_sfx_id;
        bool psv_is_a_poly_preset_fl        = psv->tid!=kDirectPresetValueTId;
        
        // only apply the value if the proc_label_sfx_id is invalid or it matches the proc to which the value will be applied
        if( apply_to_all_poly_channels_fl || apply_to_this_poly_channel_fl  || psv_is_a_poly_preset_fl  )
        {
          // if this preset refers to another network preset
          switch( psv->tid )
          {
            case kNetRefPresetValueTId:
              if((rc = _network_apply_preset(*psv->u.npv.net_preset_net,psv->u.npv.net_preset,proc_label_sfx_id)) != kOkRC )
              {
                rc = cwLogError(rc,"Application of network preset '%s' failed.",cwStringNullGuard(psv->u.npv.net_preset->label));
                goto errLabel;
              }
              break;
              
            case kDirectPresetValueTId:
              if((rc = var_set( psv->u.pvv.var, &psv->u.pvv.value )) != kOkRC )
              {
                rc = cwLogError(rc,"Preset value apply failed on '%s:%i'-'%s:%i'.",
                                cwStringNullGuard(psv->u.pvv.proc->label),psv->u.pvv.proc->label_sfx_id,
                                cwStringNullGuard(psv->u.pvv.var->label),psv->u.pvv.var->label_sfx_id);
                goto errLabel;
              }
              break;
              
            default:
              rc = cwLogError(kInvalidIdRC,"The preset value type id %i is unknown.",psv->tid);
          }
        }
      }
      
    errLabel:      
      return rc;
    }
    
    rc_t _network_apply_dual_preset( network_t& net, const network_preset_t* net_ps0, const network_preset_t* net_ps1, double coeff,  unsigned proc_label_sfx_id )
    {
      rc_t rc = kOkRC;
      
      // clear the value field of the preset-pair array
      for(unsigned i=0; i<net.preset_pairN; ++i)
        net.preset_pairA[i].value = nullptr;

      // set the value pointer in each of the preset-pair records referenced by preset-1
      for(const preset_value_t* pv1=net_ps1->u.vlist.value_head; pv1!=nullptr; pv1=pv1->link)
      {
        if( pv1->tid!=kDirectPresetValueTId )
        {
        }
        
        if( proc_label_sfx_id == kInvalidId || pv1->u.pvv.proc->label_sfx_id == proc_label_sfx_id )
        {
          if( pv1->u.pvv.var->chIdx != kAnyChIdx )
            net.preset_pairA[ pv1->u.pvv.pairTblIdx ].value = &pv1->u.pvv.value;
          else
          {
            for(unsigned i=0; i<net.preset_pairA[ pv1->u.pvv.pairTblIdx ].chN; ++i)
            {
              net.preset_pairA[ pv1->u.pvv.pairTblIdx+i ].value = &pv1->u.pvv.value;
              assert( textIsEqual(net.preset_pairA[ pv1->u.pvv.pairTblIdx+i ].var->label, pv1->u.pvv.var->label) && net.preset_pairA[ pv1->u.pvv.pairTblIdx+i ].var->label_sfx_id == pv1->u.pvv.var->label_sfx_id );        
            }
          }    
        }
      }
      
      // 
      for(const preset_value_t* pv0=net_ps0->u.vlist.value_head; pv0!=nullptr; pv0=pv0->link)
      {
        if( proc_label_sfx_id == kInvalidId || (pv0->tid!=kDirectPresetValueTId) || (pv0->tid==kDirectPresetValueTId && pv0->u.pvv.proc->label_sfx_id == proc_label_sfx_id) )
        {
          if( pv0->tid!=kDirectPresetValueTId )
          {
          }
          
          if( pv0->u.pvv.var->chIdx != kAnyChIdx )
          {
            rc = _preset_set_var_from_dual( pv0, net.preset_pairA[ pv0->u.pvv.pairTblIdx ].value, coeff );
          }
          else
          {
            for(unsigned i=0; i<net.preset_pairA[ pv0->u.pvv.pairTblIdx ].chN; ++i)
            {
              if((rc = _preset_set_var_from_dual( pv0, net.preset_pairA[ pv0->u.pvv.pairTblIdx+i ].value, coeff )) != kOkRC )
                goto errLabel;

              assert( textIsEqual(net.preset_pairA[ pv0->u.pvv.pairTblIdx+i ].var->label,pv0->u.pvv.var->label) && net.preset_pairA[ pv0->u.pvv.pairTblIdx+i ].var->label_sfx_id == pv0->u.pvv.var->label_sfx_id );        
            }
          }    
        }
      }

    errLabel:
      return rc;
    }

    rc_t _network_apply_preset( network_t& net, const network_preset_t* network_preset, unsigned proc_label_sfx_id )
    {
      rc_t rc = kOkRC;
      
      switch( network_preset->tid )
      {
        case kPresetVListTId:
          if((rc = _network_apply_vlist_preset( net, &network_preset->u.vlist, proc_label_sfx_id )) != kOkRC )
            goto errLabel;
          break;
      
        case kPresetDualTId:
          if((rc = _network_apply_dual_preset(net, network_preset->u.dual.pri, network_preset->u.dual.sec, network_preset->u.dual.coeff,  proc_label_sfx_id )) != kOkRC )
            goto errLabel;
          break;
      
        default:
          rc = cwLogError(kAssertFailRC,"Unknown preset type.");
          break;
      }

    errLabel:
      return rc;
    }
    
    //==================================================================================================================
    //
    // Presets - Probabilistic Selection
    //

    unsigned _select_ranked_ele_by_rank_prob( const preset_order_t* presetA, const bool* selV , unsigned presetN )
    {

      // get a count of the candidate presets
      unsigned rankN = selV==nullptr ? presetN : std::count_if(selV,selV+presetN,[](const bool& x){ return x; });

      if( rankN == 0 )
      {
        cwLogWarning("All preset candidates have been eliminated.");
        return kInvalidIdx;
      }

      unsigned rankV[  rankN ];
      unsigned idxMapA[ rankN ];

      // fill rankV[] with candidates 'order' value
      for(unsigned i=0,j=0; i<presetN; ++i)
        if( selV==nullptr || selV[i] )
        {
          assert( j < rankN );
          rankV[j]   = presetA[i].order;
          idxMapA[j] = i;
          ++j;
        }

      // if only one element remains to be selected
      if( rankN == 1 )
        return idxMapA[0];

      assert( rankN > 1 );
      
      unsigned threshV[ rankN ];
      unsigned uniqueRankV[ rankN ];      
      unsigned uniqueRankN = 0;
      unsigned sel_idx = rankN - 1; //

      // for each possible rank value
      for(unsigned i=0; i<rankN; ++i)
      {
        // locate the rank in the uniqueRankV[]
        unsigned j=0;
        for(; j<uniqueRankN; ++j)
          if( uniqueRankV[j]==rankV[i] )
            break;

        // if the rank was not found then include it here
        if( j == uniqueRankN )
          uniqueRankV[uniqueRankN++] = rankV[i];

      }

      // uniqueRankV[] now includes the set of possible rank values
      
      // Take the product of all possible values.
      // (this will be evenly divisible by all values)
      unsigned prod = vop::prod(uniqueRankV,uniqueRankN);

      unsigned thresh = 0;
      for(unsigned i=0; i<rankN; ++i)
        threshV[i] = (thresh += rankV[i] * prod);

      // Thresh is now set to the max possible random value.
      
      // Generate a random number between 0 and thresh
      double   fval = (double)std::rand() * thresh / RAND_MAX;

      unsigned thresh0 = 0;
      for(unsigned i=0; i<rankN; ++i)
      {
        if( thresh0 <= fval && fval < threshV[i] )
        {
          sel_idx = i;
          break;
        }
      }

      assert( sel_idx < rankN );
      
      return idxMapA[sel_idx];
    }

    
    const char* _select_ranked_ele_label_by_rank_prob( const preset_order_t* rankV, const bool* selA, unsigned rankN )
    {
      unsigned sel_idx;

      if((sel_idx = _select_ranked_ele_by_rank_prob( rankV, selA, rankN )) == kInvalidIdx )
      {
        cwLogWarning("The multi-preset select function failed. Selecting preset 0.");
        sel_idx = 0;
      }

      return rankV[sel_idx].preset_label;

    }
    

    double _calc_multi_preset_dual_coeff( const multi_preset_selector_t& mps )
    {
      double result = 0;
      unsigned resultN = 0;
      
      if( mps.coeffN == 0 )
      {
        result = 0.5;
      }
      else
      {  
        for(unsigned i=0; i<mps.coeffN; ++i)
        {
          /*

            Temporarily commented out because coeffV[] values
            have already been normalized.
            
          double norm_factor = (mps.coeffMaxV[i] - mps.coeffMinV[i]);
          
          if( norm_factor <= 0 )
            cwLogWarning("Invalid normalization factor in aggregated distance measurement.");
          else
            norm_factor = 1;
          
          
          result += std::max( mps.coeffMinV[i], std::min( mps.coeffMaxV[i], mps.coeffV[i] ) ) / norm_factor;
          */

          // WOULD DISTANCE BE BETTER THAN AVERAGE????
          
          if( mps.coeffV[i] != 0 )
          {
            result += mps.coeffV[i];
            resultN += 1;
          }
        }

        if( resultN <= 0 )
            cwLogWarning("Invalid normalization factor in aggregated distance measurement.");
        else
          result = std::min(1.0,std::max(0.0,result/mps.coeffN));
      }
      
      
      return result;
    }

    rc_t _network_parse_records_registry( network_t* net, const object_t* records_cfg )
    {
      rc_t rc = kOkRC;
      const object_t* fmt_pair = nullptr;
      unsigned n = 0;
      
      if( records_cfg == nullptr )
        return rc;

      if( !records_cfg->is_dict() )
      {
        rc = cwLogError(kSyntaxErrorRC,"The 'records' registry is not a dictionary.");
        goto errLabel;
      }

      if((n = records_cfg->child_count()) == 0)
        return rc;

      net->recdFmtRegA = mem::allocZ<recd_reg_t>(n);

      while( (fmt_pair = records_cfg->next_child_ele(fmt_pair)) != nullptr )
      {
        recd_fmt_t* recd_fmt = nullptr;
        
        if( !fmt_pair->is_pair() )
        {
          rc = cwLogError(kSyntaxErrorRC,"Illegal syntax on the record registry elment at index %i.",net->recdFmtRegN );
          goto errLabel;
        }

        if( firstMatchChar( fmt_pair->pair_label(), '.') != nullptr )
        {
          rc = cwLogError(kSyntaxErrorRC,"The record format label may not contain a '.' as in '%s'.",cwStringNullGuard(fmt_pair->pair_label()));
          goto errLabel;
        }

        if( net->recdFmtRegN >= n )
        {
          rc = cwLogError(kBufTooSmallRC,"An unexpected number of record registry elements was encountered.");
          goto errLabel;
        }

        net->recdFmtRegA[ net->recdFmtRegN ].label = fmt_pair->pair_label();
        net->recdFmtRegA[ net->recdFmtRegN ].fmt_cfg   = fmt_pair->pair_value();

        net->recdFmtRegN += 1;
        
      }

    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Record registry creation failed on the network '%s'.",cwStringNullGuard(net->label));
      
      return rc;
    }

    //==================================================================================================================
    //
    // Network - Create
    //
    
    rc_t _network_init( flow_t*                p,
                        const object_t*        networkCfg,
                        variable_t*            proxyVarL,
                        network_t*             net )
    {
      rc_t            rc           = kOkRC;
      unsigned        procN        = 0;
      const object_t* records_cfg  = nullptr;
      
      // if the top level network has not been set then set it here.
      // (this is necessary so that proc's later in the exec order
      //  can locate proc's earlier in the exec order)
      if(p->net == nullptr )
        p->net = net;

      if((rc = networkCfg->readv("procs",   kReqFl, net->procsCfg,
                                 "records", kOptFl, records_cfg,
                                 "presets", kOptFl, net->presetsCfg )) != kOkRC )
      {
        rc = cwLogError(rc,"Failed on parsing network cfg.");
        goto errLabel;
      }

      if((rc = _network_parse_records_registry(net, records_cfg )) != kOkRC )
      {
        goto errLabel;
      }

      procN            = net->procsCfg->child_count();
      net->procA  = mem::allocZ<proc_t*>(procN);

      // for each proc in the network
      for(unsigned j=0; j<procN; ++j)
      {
        const object_t* proc_cfg = net->procsCfg->child_ele(j);

        // create the proc inst instance
        if( (rc= _proc_create( p, proc_cfg, *net, proxyVarL, net->procA[j] ) ) != kOkRC )
        {
          rc = cwLogError(rc,"The processor instantiation at proc index %i failed.",j);
          goto errLabel;
        }

        net->procN += 1;
      }


      if((rc = _network_preset_pair_create_table(*net)) != kOkRC )
        goto errLabel;

      // parse the network presets but do not apply them
      if((rc = _network_preset_parse_dict(p, *net, net->presetsCfg )) != kOkRC )
        goto errLabel;

    errLabel:

      return rc;
    }

    rc_t _form_net_ui_desc( const flow_t* p, network_t& net, ui_net_t*& ui_net_ref );

    
    rc_t  _fill_net_ui_proc_and_preset_arrays( const flow_t* p, network_t& net, ui_net_t*& ui_net_ref )
    {
      rc_t rc = kOkRC;
      
      ui_net_ref->procA = mem::allocZ<ui_proc_t>(net.procN);
      ui_net_ref->procN = net.procN;
      
      for(unsigned i=0; i<ui_net_ref->procN; ++i)
      {
        ui_proc_t* ui_proc = ui_net_ref->procA + i;
        ui_proc->ui_net       = ui_net_ref;
        ui_proc->label        = net.procA[i]->label;
        ui_proc->label_sfx_id = net.procA[i]->label_sfx_id;
        ui_proc->desc         = net.procA[i]->class_desc->ui;
        ui_proc->cfg          = net.procA[i]->proc_cfg;
        ui_proc->varN         = 0;
        ui_proc->varA         = mem::allocZ<ui_var_t>(net.procA[i]->varMapN);

        proc_t* proc_ptr = proc_find(net,ui_proc->label,ui_proc->label_sfx_id );
        assert(proc_ptr != nullptr );
        ui_proc->proc = proc_ptr;
        
        
        for(unsigned j=0; j<net.procA[i]->varMapN; ++j)
        {
          variable_t* var = net.procA[i]->varMapA[j];
          
          // all slots in the varMapA[] are not used
          if( var != nullptr )
          {
            ui_var_t* ui_var = ui_proc->varA + ui_proc->varN++;

            ui_var->ui_proc           = ui_proc;
            ui_var->label             = var->label;
            ui_var->label_sfx_id      = var->label_sfx_id;
            ui_var->has_source_fl     = is_connected_to_source( var );
            ui_var->title             = var->ui_title == nullptr ? var->label : var->ui_title;
            ui_var->disable_fl        = var->ui_disable_fl || ui_var->has_source_fl || cwIsFlag(var->varDesc->flags,flow::kInitVarDescFl);
            ui_var->new_disable_fl    = ui_var->disable_fl;
            ui_var->hide_fl           = var->ui_hide_fl;
            ui_var->new_hide_fl       = ui_var->hide_fl;
            ui_var->vid               = var->vid;
            ui_var->ch_cnt            = var_channel_count( net.procA[i], var->label, var->label_sfx_id );
            ui_var->ch_idx            = var->chIdx;
            ui_var->list              = var->value_list;
            ui_var->value_tid         = (var->varDesc->type & flow::kTypeMask) == kAllTFl ? kAllTFl : var->type;
            ui_var->desc_flags        = var->varDesc->flags;
            ui_var->ui_cfg            = var->varDesc->ui_cfg;
            ui_var->user_arg          = nullptr;

            var->ui_var = ui_var;
          }
        }

        if( net.procA[i]->internal_net != nullptr )
        {
          if((rc = _form_net_ui_desc(p, *net.procA[i]->internal_net, ui_proc->internal_net )) != kOkRC )
            goto errLabel;
        }
        
      }

      ui_net_ref->presetA = mem::allocZ<ui_preset_t>(net.presetN);
      ui_net_ref->presetN = net.presetN;

      for(unsigned i=0; i<ui_net_ref->presetN; ++i)
      {
        ui_net_ref->presetA[i].label = net.presetA[i].label;
        ui_net_ref->presetA[i].preset_idx = i;
      }

    errLabel:
      return rc;
    }

    rc_t _form_net_ui_desc( const flow_t* p, network_t& net, ui_net_t*& ui_net_ref )
    {
      rc_t rc = kOkRC;
      ui_net_ref = mem::allocZ<ui_net_t>();
      
      if((rc = _fill_net_ui_proc_and_preset_arrays(p,net,ui_net_ref)) != kOkRC )
        goto errLabel;

      ui_net_ref->poly_idx = net.poly_idx;
      ui_net_ref->ui_create_fl = p->ui_create_fl;

      if( net.poly_link != nullptr )
        _form_net_ui_desc(p,*net.poly_link,ui_net_ref->poly_link);

    errLabel:
      return rc;
    }

    void _network_profile_proc_total( const network_t& net, time::spec_t& acc )
    {
      time::setZero(acc);
      for(unsigned i=0; i<net.procN; ++i)
        time::accumulate(acc,net.procA[i]->prof_dur);
    }
    
    void _network_profile_report(const network_t& net, unsigned level)
    {
      time::spec_t acc;
      double acc_sec;
      _network_profile_proc_total(net,acc);
      acc_sec = time::seconds(acc);

      printf("%s : net:%8.5fs accum:%8.5fs\n",net.label==nullptr ? "<none>" : net.label,time::seconds(net.prof_dur),acc_sec);

      for(unsigned i=0; i<net.procN; ++i)
      {
        double dur_sec = time::seconds(net.procA[i]->prof_dur);
        
        printf("%2i %6.2f  %8.5fs %s::%i\n",level,dur_sec/acc_sec,dur_sec,net.procA[i]->label,net.procA[i]->label_sfx_id);

        for(const network_t* n = net.procA[i]->internal_net; n!=nullptr; n=n->poly_link)
          _network_profile_report(*n,level+1);        
      }
    }
    
  }
}



cw::rc_t cw::flow::network_create( flow_t*                 p,
                                   const char* const *            netLabelA,  // netLabel[ netCfgN ]
                                   const object_t* const * netCfgA,    // netCfgA[ netCfgA ]
                                   unsigned                netCfgN,
                                   variable_t*             proxyVarL,
                                   //const proc_t*           owner_proc,
                                   unsigned                polyCnt,
                                   network_t*&             net_ref )
{
  rc_t       rc  = kOkRC;
  network_t* n0  = nullptr;
  
  net_ref = nullptr;

  /*
  if( !(netCfgN==1 || netCfgN==polyCnt ))
  {
    cwLogError(kInvalidArgRC,"The count of network cfg's must be one, or must match the 'poly count'.");
    goto errLabel;
  }
  */
  
  // for each network configuration
  for(unsigned cfg_idx=0; cfg_idx<netCfgN; ++cfg_idx)
  {
    unsigned poly_cnt = polyCnt;

    const object_t* netCfg = netCfgA[cfg_idx];
    const char* netLabel = netLabelA[cfg_idx];

    // overrride poly_cnt with the optional 'count' field passed from the caller
    if((rc = netCfg->getv_opt("count",poly_cnt)) != kOkRC )
    {
      rc = cwLogError(rc,"Poly network 'count' parse failed on the network '%s'.",cwStringNullGuard(netLabel));
      goto errLabel;
    }

    // for each poly network
    for(unsigned i=0; i<poly_cnt; ++i)
    {
      // allocate the network
      network_t*      net = mem::allocZ<network_t>();

      net->flow       = p;
      net->label      = netLabel;
      net->polyN      = poly_cnt;
      net->poly_idx   = i;

      if( net_ref == nullptr )
        net_ref = net;
    
      if( n0 != nullptr )
        n0->poly_link = net;
    
      n0 = net;
    
      // create the network
      if((rc = _network_init(p, netCfg, proxyVarL, net)) != kOkRC )
      {
        rc = cwLogError(rc,"Network create failed on poly index %i.",i);
        goto errLabel;
      }
    
    }
  }
  
errLabel:
  if( rc != kOkRC && net_ref != nullptr )
    _network_destroy(net_ref);
  
  return rc;
}

cw::rc_t cw::flow::network_destroy( network_t*& net )
{
  return _network_destroy(net);
}

const cw::object_t* cw::flow::find_network_preset( const network_t& net, const char* presetLabel )
{
  const object_t* preset_value = nullptr;
      
  if( net.presetsCfg != nullptr )
  {
    rc_t rc;
        
    if((rc = net.presetsCfg->getv_opt( presetLabel, preset_value )) != kOkRC )
      cwLogError(rc,"Search for network preset named '%s' failed.", cwStringNullGuard(presetLabel));
  }

  return preset_value;     
}


cw::rc_t cw::flow::create_net_ui_desc( flow_t* p )
{
  rc_t rc = kOkRC;

  if( p->net == nullptr )
  {
    rc = cwLogError(kInvalidStateRC,"The UI description could not be formed because the network is not valid.");
    goto errLabel;
  }
  
  if((rc = _form_net_ui_desc(p, *p->net, p->net->ui_net )) != kOkRC )
  {
    rc = cwLogError(rc,"The UI description creation failed.");
    goto errLabel;
  }

errLabel:
  
  return rc;  
}

cw::rc_t cw::flow::exec_cycle( network_t& net )
{
  rc_t rc = kOkRC;
  bool halt_fl = false;
  time::spec_t net_t0;
  
  if( net.flow->prof_fl )
    time::get(net_t0);

  for(unsigned i=0; i<net.procN && rc==kOkRC; ++i)
  {
    time::spec_t t0;
    proc_t* proc = net.procA[i];
    
    if( net.flow->prof_fl)
      time::get(t0);

    TRACE_TIME( proc->trace_id, tracer::kBegEvtId, net.flow->cycleIndex,0 );

    // execute the proc instance
    if((rc = proc_exec(proc)) != kOkRC )
    {
      // kEofRC indicates that that the network should shutdow at the end of this cycle.
      if( rc == kEofRC )
      {
        halt_fl = true;
        rc = kOkRC;
      }      
    }
    
    TRACE_TIME( proc->trace_id, tracer::kEndEvtId, net.flow->cycleIndex,0 );

    if( net.flow->prof_fl )
    {
      time::accumulate_elapsed_current(proc->prof_dur,t0);
      proc->prof_cnt += 1;
    }
  }

  if( net.flow->prof_fl )
  {
    time::accumulate_elapsed_current(net.prof_dur,net_t0);
    net.prof_cnt += 1;
  }

  return halt_fl ? ((unsigned)kEofRC) : rc;
}

cw::rc_t cw::flow::network_profile_report( const network_t& net )
{
   _network_profile_report(net,0);
   return kOkRC;
}


cw::rc_t cw::flow::get_variable( network_t& net, const char* proc_label, const char* var_label, unsigned chIdx, proc_t*& procPtrRef, variable_t*& varPtrRef )
{
  rc_t        rc   = kOkRC;
  proc_t* proc = nullptr;
  variable_t* var  = nullptr;

  varPtrRef = nullptr;
  procPtrRef = nullptr;

  // locate the proc inst instance
  if((proc = proc_find(net,proc_label,kBaseSfxId)) == nullptr )
  {
    rc = cwLogError(kInvalidIdRC,"Unknown proc inst instance label '%s'.", cwStringNullGuard(proc_label));
    goto errLabel;
  }

  // locate the variable
  if((rc = var_find( proc, var_label, kBaseSfxId, chIdx, var)) != kOkRC )
  {
    rc = cwLogError(kInvalidArgRC,"The variable '%s' could not be found on the proc inst instance '%s'.",cwStringNullGuard(var_label),cwStringNullGuard(proc_label));
    goto errLabel;
  }

  procPtrRef = proc;
  varPtrRef = var;
      
errLabel:
  return rc;
}


cw::rc_t cw::flow::set_variable_user_arg( network_t&net, const ui_var_t* ui_var, void* arg )
{
  rc_t rc = kOkRC;
  variable_t* var = nullptr;

  if((rc = var_find( ui_var->ui_proc->proc, ui_var->vid,  ui_var->ch_idx, var )) != kOkRC )
  {
    rc = cwLogError(rc,"User-Id assigned failed on '%s:%i' because the variable was not found.",cwStringNullGuard(ui_var->label),ui_var->label_sfx_id);
    goto errLabel;
  }

  var->ui_var->user_arg = arg;

errLabel:
  return rc;
}

cw::rc_t cw::flow::network_apply_preset( network_t& net, const char* preset_label, unsigned proc_label_sfx_id )
{
  rc_t                    rc             = kOkRC;
  const network_preset_t* network_preset = nullptr;

  if((network_preset = network_preset_from_label(net, preset_label )) == nullptr )
  {
    rc = cwLogError(kInvalidIdRC,"The network preset '%s' could not be found.", cwStringNullGuard(preset_label) );
    goto errLabel;
  }

  if((rc = _network_apply_preset(net,network_preset,proc_label_sfx_id)) != kOkRC )
    goto errLabel;
  
  cwLogInfo("Activated preset:%s",preset_label);

errLabel:
  if(rc != kOkRC )
    rc = cwLogError(rc,"The network application '%s' with sfx_id '%i' failed.", cwStringNullGuard(preset_label), proc_label_sfx_id );
     
  return rc;
 
}


cw::rc_t cw::flow::network_apply_dual_preset( network_t& net, const char* preset_label_0, const char* preset_label_1, double coeff, unsigned proc_label_sfx_id )
{
  rc_t                    rc      = kOkRC;
  const network_preset_t* net_ps0 = nullptr;
  const network_preset_t* net_ps1 = nullptr;
  
  if((net_ps0 = network_preset_from_label(net, preset_label_0 )) == nullptr )
  {
    rc = cwLogError(kInvalidIdRC,"The network preset '%s' could not be found.", preset_label_0 );
    goto errLabel;
  }

  if((net_ps1 = network_preset_from_label(net, preset_label_1 )) == nullptr )
  {
    rc = cwLogError(kInvalidIdRC,"The network preset '%s' could not be found.", preset_label_1 );
    goto errLabel;
  }

  if((rc = _network_apply_dual_preset(net, net_ps0, net_ps1, coeff,  proc_label_sfx_id )) != kOkRC )
    goto errLabel;

errLabel:
  if( rc != kOkRC )
    rc = cwLogError(rc,"Apply dual-preset failed.");  
  
  return rc;
}


cw::rc_t cw::flow::network_apply_preset( network_t& net, const multi_preset_selector_t& mps, unsigned proc_label_sfx_id )
{
  rc_t        rc        = kOkRC;
  const char* label0    = nullptr;
  const char* label1    = nullptr;
  bool        priProbFl = cwIsFlag(mps.flags, kPriPresetProbFl );
  bool        secProbFl = cwIsFlag(mps.flags, kSecPresetProbFl );
  bool        interpFl  = cwIsFlag(mps.flags, kInterpPresetFl );

  //printf("preset flags: pri:%i sec:%i interp:%i\n",priProbFl,secProbFl,interpFl);
  
  // verify that the set of candidate presets is not empty
  if( mps.presetN == 0 )
  {
    cwLogError(kInvalidArgRC,"A multi-preset application was requested but no presets were provided.");
    goto errLabel;    
  }

  // if only a single candidate preset exists or needs to be selected
  if( interpFl==false || mps.presetN==1 )
  {
    // if only a single candidate preset is available or pri. probablity is not enabled 
    if( mps.presetN == 1 || priProbFl==false )
      label0 = mps.presetA[0].preset_label;
    else
    {
      if( priProbFl )
        label0 = _select_ranked_ele_label_by_rank_prob( mps.presetA, nullptr, mps.presetN );
      else
        label0 = mps.presetA[0].preset_label;
    }
  }
  else  // interpolation has been selected and at least 2 presets exist
  {    
    unsigned pri_sel_idx = 0;
        
    // select the primary preset
    if( priProbFl )
      pri_sel_idx = _select_ranked_ele_by_rank_prob( mps.presetA, nullptr, mps.presetN );
    else
    {
      // select all presets assigned to order == 1
      bool selA[ mps.presetN ];
      for(unsigned i=0; i<mps.presetN; ++i)
        selA[i]= mps.presetA[i].order==1;

      // select the preset among all presets marked as 1
      pri_sel_idx = _select_ranked_ele_by_rank_prob( mps.presetA, selA, mps.presetN );
    }

    if( pri_sel_idx == kInvalidIdx )
      pri_sel_idx    = 0;
    
    // the primary preset has now been selected

    // if there is only one candidate secondary preset
    if( mps.presetN == 2)
    {
      assert( pri_sel_idx <= 1 );
      label1  = mps.presetA[ pri_sel_idx == 0 ? 1 : 0 ].preset_label;
    }
    else  // at least two remaining presets exist to select between
    {
      // mark the selected primary preset as not-available
      bool selA[ mps.presetN ];
      vop::fill(selA,mps.presetN,true);
      selA[pri_sel_idx] = false;

      // if the second preset should be selected probabilistically
      if( secProbFl )
        label1 = _select_ranked_ele_label_by_rank_prob( mps.presetA, selA, mps.presetN );
      else 
      {
        // select the best preset that is not the primary preset
        for(unsigned i=0; i<mps.presetN; ++i)
          if( i != pri_sel_idx )
          {
            label1 = mps.presetA[i].preset_label;
            break;
          }        
      }
    }
    
    assert( pri_sel_idx != kInvalidIdx );
    label0               = mps.presetA[ pri_sel_idx ].preset_label;
  }
    
  assert(label0 != nullptr );
  
  if( label1 == nullptr )
  {
    rc = network_apply_preset( net, label0, proc_label_sfx_id );
  }
  else
  {
    double coeff = _calc_multi_preset_dual_coeff(mps);
    rc = network_apply_dual_preset( net, label0, label1, coeff, proc_label_sfx_id );
  }
  

errLabel:
  return rc;
}
