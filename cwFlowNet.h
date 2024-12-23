//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef cwFlowNet_h
#define cwFlowNet_h

namespace cw
{
  namespace flow
  {
    // Instantiate a network.
    // The root network always is instantiated with a single cfg. record - because it is never a poly network.
    // The only time netCfgN will be greater than 1 is when a heterogenous poly network is being
    // instantiated.
    rc_t network_create( flow_t*         p,
                         const object_t* const * netCfgA, // netCfgA[netCfgN] 
                         unsigned        netCfgN,         // count of cfg. records in netCfgN
                         variable_t*     proxyVarL,       //
                         const proc_t*   owner_proc,      // Pointer to the proc that owns this network (or null if creating the top level network)
                         unsigned        polyCnt,         // Count of poly subnets to create or 1 if the network is not poly
                         network_t*&     net_ref          // Returned network handle.
      );
    
    rc_t network_destroy( network_t*& net );

    const object_t* find_network_preset( const network_t& net, const char* presetLabel );

    // Instantiates net_t.ui_net.
    rc_t create_net_ui_desc( flow_t* p );
    
    rc_t exec_cycle( network_t& net );


    rc_t get_variable( network_t& net, const char* inst_label, const char* var_label, unsigned chIdx, proc_t*& instPtrRef, variable_t*& varPtrRef );

    rc_t set_variable_user_id( network_t&net, const ui_var_t* ui_var, unsigned user_id );


    template< typename T >
    rc_t set_variable_value( network_t& net, const char* inst_label, const char* var_label, unsigned chIdx, T value )
    {
      rc_t rc = kOkRC;
      proc_t* inst = nullptr;
      variable_t* var = nullptr;

      // get the variable
      if((rc = get_variable(net,inst_label,var_label,chIdx,inst,var)) != kOkRC )
        goto errLabel;
      
      // set the variable value
      if((rc = var_set( inst, var->vid, chIdx, value )) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"The variable set failed on instance:'%s' variable:'%s'.",cwStringNullGuard(inst_label),cwStringNullGuard(var_label));
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    template< typename T >
    rc_t get_variable_value( network_t& net, const char* inst_label, const char* var_label, unsigned chIdx, T& valueRef )
    {
      rc_t rc = kOkRC;
      proc_t* inst = nullptr;
      variable_t* var = nullptr;

      // get the variable 
      if((rc = get_variable(net,inst_label,var_label,chIdx,inst,var)) != kOkRC )
        goto errLabel;
      
      // get the variable value
      if((rc = var_get( inst, var->vid, chIdx, valueRef )) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"The variable get failed on instance:'%s' variable:'%s'.",cwStringNullGuard(inst_label),cwStringNullGuard(var_label));
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    template< typename T >
    rc_t set_variable_value( network_t& net, const ui_var_t* ui_var, T value )
    {
      rc_t rc = kOkRC;
      
      // set the variable value
      if((rc = var_set( ui_var->ui_proc->proc, ui_var->vid, ui_var->ch_idx, value )) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"The variable set failed on instance:'%s:%i' variable:'%s:%i'.",cwStringNullGuard(ui_var->ui_proc->proc->label),ui_var->ui_proc->proc->label_sfx_id,cwStringNullGuard(ui_var->label),ui_var->label_sfx_id);
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    template< typename T >
    rc_t get_variable_value( network_t& net, const ui_var_t* ui_var, T& valueRef )
    {
      rc_t rc = kOkRC;
      
      // get the variable value
      if((rc = var_get( ui_var->ui_proc->proc, ui_var->vid, ui_var->ch_idx, valueRef )) != kOkRC )
      {
        rc = cwLogError(kOpFailRC,"The variable get failed on instance:'%s:%i' variable:'%s:%i'.",cwStringNullGuard(ui_var->ui_proc->proc->label),ui_var->ui_proc->proc->label_sfx_id,cwStringNullGuard(ui_var->label),ui_var->label_sfx_id);
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    // 'proc_label_sfx_id' is the proc label_sfx_id to be used to identify all proc's which will
    // be updated by the preset application. This is used to identify the set of procs to be updated
    // for 'poly' networks.
    // If 'proc_label_sfx_id' is set to 'kInvalidId' then the preset will be applied to all proc's.
    rc_t network_apply_preset( network_t& net, const char* presetLabel, unsigned proc_label_sfx_id=kInvalidId );
    rc_t network_apply_dual_preset( network_t& net, const char* presetLabel_0, const char* presetLabel_1, double coeff, unsigned proc_label_sfx_id=kInvalidId );      
    rc_t network_apply_preset( network_t& net, const multi_preset_selector_t& mps, unsigned proc_label_sfx_id=kInvalidId );
    
    
  }
}


#endif
