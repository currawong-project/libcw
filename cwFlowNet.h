#ifndef cwFlowNet_h
#define cwFlowNet_h

namespace cw
{
  namespace flow
  {
    typedef enum {
      kNetFirstPolyOrderId,
      kProcFirstPolyOrderId
    } network_order_id_t;
    
    rc_t network_create( flow_t* p,
                         const object_t*    networkCfg,
                         network_t&         net,                              // Network object to be filled with new proc instances
                         variable_t*        proxyVarL,                        // 
                         unsigned           polyCnt   = 1,                    // Count of networks to create
                         network_order_id_t orderId   = kNetFirstPolyOrderId  // Set the network exec order.
      );
    
    rc_t network_destroy( network_t& net );

    const object_t* find_network_preset( const network_t& net, const char* presetLabel );
    
    rc_t exec_cycle( network_t& net );


    rc_t get_variable( network_t& net, const char* inst_label, const char* var_label, unsigned chIdx, proc_t*& instPtrRef, variable_t*& varPtrRef );

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
