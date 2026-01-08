//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
namespace cw
{
  namespace flow
  {

    struct proc_str;
    struct variable_str;
        
    typedef rc_t (*member_func_t)( struct proc_str* ctx );
    typedef rc_t (*member_notify_func_t)( struct proc_str* ctx, struct variable_str* var );

    // var_desc_t attribute flags
    enum
    {
      kInvalidVarDescFl   = 0x000,
      kSrcVarDescFl       = 0x001,
      kSrcOptVarDescFl    = 0x002,
      kNoSrcVarDescFl     = 0x004,
      kInitVarDescFl      = 0x008,
      kMultVarDescFl      = 0x010,
      kUdpOutVarDescFl    = 0x020,
      kUiCreateVarDescFl  = 0x040,
      kUiDisableVarDescFl = 0x080,
      kUiHideVarDescFl    = 0x100,
      kNotifyVarDescFl    = 0x200,
    };
    
    typedef struct class_members_str
    {
      member_func_t        create;
      member_func_t        destroy;  
      member_notify_func_t notify;
      member_func_t        exec;
      member_func_t        report;
    } class_members_t;

    typedef struct var_desc_str
    {
      const object_t*      cfg;     // The cfg object that describes this variable from 'flow_class'.
      const object_t*      val_cfg; // An object containing the default value for this variable.
      const object_t*      fmt_cfg; // An object containg the format (e.g. record fields) information
      const object_t*      ui_cfg;  // The UI cfg for this var.
      const char*          label;   // Name of this var.
      const char*          mult_ref_var_label; // Label of a variable which can supply it's mult. count.
      unsigned             type;    // Value type id (e.g. kBoolTFl, kIntTFl, ...)
      unsigned             flags;   // Attributes for this var. (e.g. kSrcVarFl )
      const char*          docText; // User help string for this var.

      union
      {
        recd_fmt_t* recd_fmt;   // the 'recd_type.base' is never set in 'recd_type' because it is only valid once the var is instantiated
      } fmt;
      
      char*                proxyProcLabel;
      char*                proxyVarLabel;
      
      struct var_desc_str* link;    // class_desc->varDescL list link
    } var_desc_t;

    typedef struct class_preset_str
    {
      const char*              label;
      const object_t*          cfg;
      struct class_preset_str* link;
    } class_preset_t;
    
    typedef struct class_desc_str
    {
      const object_t*   cfg;        // class cfg 
      const char*       label;      // class label;      
      var_desc_t*       varDescL;   // varDescL variable description linked on var_desc_t.link
      class_preset_t*   presetL;    // preset linked list
      class_members_t*  members;    // member functions for this class
      unsigned          polyLimitN; // max. poly copies of this class per network_t or 0 if no limit
      ui_proc_desc_t*   ui;
    } class_desc_t;

    enum {
      kInvalidVarFl    = 0x00,
      kLogRtVarFl      = 0x01,  // Log the value of this variable on cycle 0 or greater.
      kLogInitVarFl    = 0x02,  // Always log the value of this variable before cycle 0
      kProxiedVarFl    = 0x04,
      kProxiedOutVarFl = 0x08,
    };

    // Note: The concatenation of 'vid' and 'chIdx' should form a unique identifier among all variables
    // on a given 'instance'.
    typedef struct variable_str
    {
      struct proc_str*     proc;         // pointer to this variables instance
      
      char*                label;        // this variables label
      unsigned             label_sfx_id; // the label suffix id of this variable or kBaseSfxId if this has no suffix
      
      unsigned             vid;          // this variables numeric id ( cat(vid,chIdx) forms a unique variable identifier on this 'proc'      
      unsigned             chIdx;        // channel index
      unsigned             flags;        // See kLogVarFl, kProxiedVarFl, etc
      unsigned             type;         // This is the value type as established when the var is initialized - it never changes for the life of the var.
            
      var_desc_t*          classVarDesc; // pointer to this variables class var desc
      var_desc_t*          localVarDesc; // pointer to this variables local var desc - if it doesn't match classVarDesc.
      var_desc_t*          varDesc;      // the effective variable description for this variable (set to classVarDesc or localVarDesc)
      
      value_t              local_value[ kLocalValueN ]; // the local value instance (actual value if this is not a 'src' variable)
      unsigned             local_value_idx;             // local_value[] is double buffered to allow the cur value of the buf[] to be held while the next value is validated (see _var_set_template())
      struct variable_str* src_var;                     // pointer to this input variables source link (or null if it uses the local_value)
      value_t*             value;                       // pointer to the value associated with this variable

      const list_t*        value_list;   // list of valid values for this variable or nullptr if not applicable

      struct variable_str* var_link;     // instance.varL list link
      struct variable_str* ch_link;      // list of channels that share this variable (rooted on 'any' channel - in order by channel number)

      struct variable_str* dst_head;     // Pointer to list of out-going connections (null on var's that do not have out-going connections)
      struct variable_str* dst_tail;     // 
      struct variable_str* dst_link;     // Link used by dst_head list.

      std::atomic<unsigned> mod_cycle_idx;  // The last cycle index this variable was modified on.
      unsigned             log_verbosity;   // Logging verbosity for this variable (See k???ValPrintVerb)
      struct variable_str* log_link;        // Link used by proc_t.logVarL;
      
      char*                ui_title;     // class description UI overrides from the proc. inst 'ui' cfg. for this varaible
      bool                 ui_hide_fl;
      bool                 ui_disable_fl;
      
      ui_var_t*            ui_var;       // this variables UI description
      std::atomic<struct variable_str*> ui_var_link; // UI update var link based on flow_t ui_var_head;

      std::atomic<unsigned> modN; // count of modifications made to this variable during this cycle
      
    } variable_t;


    struct network_str;

    enum {
      kUiCreateProcFl = 0x01
    };

    typedef struct manual_notify_str
    {
      variable_t*       var;
      bool              check_ele_cnt_fl;
    } manual_notify_t;
    
    typedef struct proc_str
    {
      struct flow_str*    ctx;  // global system context
      struct network_str* net;  // network which owns this proc

      unsigned        flags;         // See k???ProcFl

      class_desc_t*   class_desc;    //
      
      char*           label;         // instance label
      unsigned        label_sfx_id;  // label suffix id (set to kBaseSfxId (0) unless poly is non-null)
      
      const object_t* proc_cfg;      // instance configuration
      class_preset_t* presetL;       // instance presets
            
      void*           userPtr;       // instance state

      variable_t*     varL;          // linked list of all variables on this instance

      unsigned        varMapChN;     // max count of channels (max 'chIdx' + 2) among all variables on this instance, (2=kAnyChIdx+index to count)
      unsigned        varMapIdN;     // max 'vid' among all variables on this instance 
      unsigned        varMapN;       // varMapN = varMapIdN * varMapChN 
      variable_t**    varMapA;       // varMapA[ varMapN ] = allows fast lookup from ('vid','chIdx) to variable

      bool                  modVarRecurseFl;  // flag used to prevent call to set_var() from inside _notify() from calling var_schedule_notification()
      variable_t**          modVarMapA;       // modVarMapA[ modVarMapN ]
      unsigned              modVarMapN;       // modVarMapN == varMapN
      unsigned              modVarMapTailIdx; // index of next full slot in varMapA[]
      std::atomic<unsigned> modVarMapFullCnt; // count of elements in modVarMapA[]
      std::atomic<unsigned> modVarMapHeadIdx; // index of next empty slot in varMapA[]

      manual_notify_t*   manualNotifyVarA;  // manualNotifyVarA[ manualNotifyVarN ] Array of variables which do not support automatic notification ...
      unsigned           manualNotifyVarN;  //                                     ... but which act as a source to variables that require notification.

      
      log::logLevelId_t logLevel;  // Log level, set by `log:{}` and enforced by proc_log_msg(). Default: kInvalid_LogLevel which tracks global log level.
      variable_t*       logVarL;  // Link list of variables that are flagged to log after each exec. Linked via variable_t.log_link;

      // For 'poly' proc's 'internal_net' is a list linked by network_t.poly_link.
      struct network_str*  internal_net;
      unsigned             internal_net_cnt; // count of hetergenous networks contained in the internal_net linked list.

      time::spec_t prof_dur; // total time spent in this proc
      unsigned     prof_cnt; // total count of calls to this proc
      unsigned     trace_id;
      
    } proc_t;

    struct network_preset_str;
    typedef struct proc_var_value_str
    {
      proc_t*                  proc;       // proc target for this preset value
      variable_t*              var;        // var target for this preset value
      value_t                  value;      // Preset value.
      unsigned                 pairTblIdx; // Index into the preset pair table for this preset value
    } proc_var_value_t;

    typedef struct net_preset_ref_str
    {
      struct network_str*       net_preset_net;  // poly net this preset will be applied to
      const network_preset_str* net_preset;      // network_preset_t of presets 
    } net_preset_ref_t;
    
    typedef enum {
      kDirectPresetValueTId,
      kNetRefPresetValueTId,
    } preset_val_tid_t;
    
    // preset_value_t holds a preset value and the proc/var to which it will be applied.
    typedef struct preset_value_str
    {
      preset_val_tid_t tid;
      
      union
      {
        proc_var_value_t  pvv;  // Direct proc/var/value tuples.
        net_preset_ref_t  npv;  // Refers to a network_preset_t and a list of preset_values.
      } u;
      
      struct preset_value_str* link;
    } preset_value_t;

    typedef struct preset_value_list_str
    {
      preset_value_t* value_head;  // List of preset_value_t for this preset. 
      preset_value_t* value_tail;  // Last preset value in the list.
    } preset_value_list_t;

    typedef struct dual_preset_str
    {
      const struct network_preset_str* pri;
      const struct network_preset_str* sec;
      double      coeff;
    } dual_preset_t;

    typedef enum {
      kPresetVListTId,
      kPresetDualTId
    } preset_type_id_t;
    
    typedef struct network_preset_str
    {
      const char*     label;       // Preset label
      preset_type_id_t tid;
      
      union {
        preset_value_list_t vlist;
        dual_preset_t       dual;
      } u;
    } network_preset_t;

    // Preset-pair record used to apply dual presets.
    typedef struct network_preset_pair_str
    {
      const proc_t*     proc;   //
      const variable_t* var;    //
      unsigned          chIdx;  // 
      unsigned          chN;    //
      const value_t*    value;  //
    } network_preset_pair_t;

    typedef struct global_var_str
    {
      const char* class_label;
      char*       var_label;
      void*       blob;
      unsigned    blobByteN;
      
      struct global_var_str* link;      
    } global_var_t;

    typedef struct recd_reg_str
    {
      const char*     label;
      const object_t* fmt_cfg;
    } recd_reg_t;

    typedef struct network_str
    {
      struct flow_str*  flow;
      const char*       label;
      
      const object_t*   procsCfg;   // network proc list
      const object_t*   presetsCfg; // presets designed for this network

      struct proc_str** procA;      
      unsigned          procN;

      network_preset_t* presetA;
      unsigned          presetN;

      recd_reg_t*       recdFmtRegA;
      unsigned          recdFmtRegN;

      // Preset pair table used by network_apply_dual_preset()
      network_preset_pair_t* preset_pairA;
      unsigned               preset_pairN;

      unsigned            polyN;       // Count of networks in poly net or 1 if not part of a poly net
                                       // (for het. poly net's this counts the number of net's for each het. array)
      unsigned            poly_idx;    // Index in poly net.
      struct network_str* poly_link;   // Link to next net in poly.

      ui_net_t* ui_net;

      time::spec_t prof_dur; // total time spent executing this network
      unsigned     prof_cnt; // total count of executions of this network
            
    } network_t;
    
    
    typedef struct flow_str
    {
      const object_t*      pgmCfg;      // complete program cfg
      const object_t*      networkCfg;  // 'network' cfg from pgmCfg

      bool                 printNetworkFl;
      bool                 non_real_time_fl;     // set if this is a non-real-time program
      unsigned             framesPerCycle;       // sample frames per cycle (64)
      srate_t              sample_rate;          // default sample rate (48000.0)
      unsigned             maxCycleCount;        // count of cycles to run on flow::exec() or 0 if there is no limit.
      unsigned             uiUpdateCycleCount;   // count of cycles between UI updates (1)
      const char*          init_net_preset_label;// network initialization preset label or nullptr if there is no net. init. preset
      
      bool                 isInRuntimeFl;        // Set when compile-time is complete
      
      unsigned             cycleIndex;           // Incremented with each processing cycle
      unsigned             uiUpdateCycleIndex;   // Count cycles between UI updates

      bool                 printLogHdrFl;
      
      bool                 multiPriPresetProbFl; // If set then probability is used to choose presets on multi-preset application
      bool                 multiSecPresetProbFl; // 
      bool                 multiPresetInterpFl;  // If set then interpolation is applied between two selectedd presets on multi-preset application
      
      class_desc_t*        classDescA;           // 
      unsigned             classDescN;           //

      class_desc_t*        udpDescA;             // 
      unsigned             udpDescN;             //
      
      external_device_t*   deviceA;              // deviceA[ deviceN ] external device description array
      unsigned             deviceN;              //

      const char*          proj_dir;             // default input/output directory

      // Top-level preset list.
      network_preset_t* presetA;  // presetA[presetN] partial (label and tid only) parsing of the network presets 
      unsigned          presetN;  // 
      
      network_t*        net;      // The root of the network instance

      bool                     ui_create_fl; // dflt: false Set by network flag: ui_create_fl, override with proc inst ui:{ create_fl }
      ui_callback_t            ui_callback;
      void*                    ui_callback_arg;
      
      std::atomic<variable_t*> ui_var_head; // Linked lists of var's to send to the UI
      variable_t               ui_var_stub;
      variable_t*              ui_var_tail;

      global_var_t* globalVarL;

      bool         prof_fl;     // set to turn profiling on
      time::spec_t prof_dur;    // total execution time
      unsigned     prof_cnt;    // total count of execution cycles
      time::spec_t prof_ui_dur; // total time spent updating UI
      unsigned     trace_id;
      
    } flow_t;

    

    
    
    //------------------------------------------------------------------------------------------------------------------------
    //
    // Class and Variable Description
    //

    rc_t              var_desc_create( const char* label, const object_t* cfg, var_desc_t*& var_desc_ref );
    void              var_desc_destroy( var_desc_t* var_desc );
    
    unsigned             var_desc_attr_label_to_flag( const char* attr_label );
    const char*          var_desc_flag_to_attribute( unsigned flag );
    const idLabelPair_t* var_desc_flag_array( unsigned& array_cnt_ref );

    void                class_desc_destroy( class_desc_t* class_desc);
    class_desc_t*       class_desc_find(        flow_t* p, const char* class_desc_label );
    const class_desc_t* class_desc_find(  const flow_t* p, const char* class_desc_label );
    
    var_desc_t*       var_desc_find(       class_desc_t* cd, const char* var_label );
    const var_desc_t* var_desc_find( const class_desc_t* cd, const char* var_label );
    rc_t              var_desc_find(       class_desc_t* cd, const char* var_label, var_desc_t*& vdRef );

    bool              var_desc_has_recd_format( var_desc_t* vd );
    
    const class_preset_t* class_preset_find( const class_desc_t* cd, const char* preset_label );

    template<class T>
    rc_t class_preset_value( const class_preset_t* class_preset, const char* var_label, unsigned ch_idx, T& val )
    {
      rc_t rc = kOkRC;
      const object_t* preset_val = nullptr;

      if((rc = class_preset->cfg->get(var_label, preset_val )) != kOkRC )
      {
        rc = cwLogError(rc,"The preset variable '%s' on the preset '%s' could not be parsed.",cwStringNullGuard(var_label),cwStringNullGuard(class_preset->label));
        goto errLabel;
      }

      if( preset_val->is_list() )
      {
        if( ch_idx >= preset_val->child_count() || (preset_val = preset_val->child_ele( ch_idx )) == nullptr )
        {
          rc = cwLogError(rc,"The channel index '%i' is invalid for the preset '%s:%s'.",ch_idx,cwStringNullGuard(class_preset->label),cwStringNullGuard(var_label));
          goto errLabel;
        }
      }

      // 
      if((rc = preset_val->value( val )) != kOkRC )
      {
        rc = cwLogError(rc,"The preset value '%s:%s ch:%i' could not be parsed.",cwStringNullGuard(class_preset->label),cwStringNullGuard(var_label),ch_idx);
        goto errLabel;
      }

    errLabel:
      return rc;
    }

    template<class T>
    rc_t class_preset_value( const class_desc_t* class_desc, const char* class_preset_label, const char* var_label, unsigned ch_idx, T& val )
    {
      rc_t rc = kOkRC;
      const class_preset_t* class_preset = nullptr;

      if((class_preset = class_preset_find(class_desc, class_preset_label)) == nullptr )
      {
        rc = cwLogError(kEleNotFoundRC,"The preset '%s' could not be found on the class description: '%s'.",cwStringNullGuard(class_preset_label),cwStringNullGuard(class_desc->label));
        goto errLabel;
      }

      rc = class_preset_value(class_preset, var_label, ch_idx, val );

    errLabel:
      return rc;
    }

    template<class T>
    rc_t class_preset_value( const flow_t* p, const char* class_desc_label, const char* class_preset_label, const char* var_label, unsigned ch_idx, T& val )
    {
      rc_t rc = kOkRC;
      const class_desc_t* class_desc;
      if((class_desc = class_desc_find(p,class_desc_label)) == nullptr )
      {
        rc = cwLogError(kEleNotFoundRC,"The class description '%s' could not be found.",cwStringNullGuard(class_desc_label));
        goto errLabel;
      }

      rc = class_preset_value( class_desc, class_preset_label, var_label, ch_idx, val );

    errLabel:
      return rc;      
    }
    
    
    rc_t class_preset_value_channel_count( const class_preset_t* class_preset, const char* var_label, unsigned& ch_cnt_ref );
    rc_t class_preset_value_channel_count( const class_desc_t* class_desc, const char* class_preset_label, const char* var_label, unsigned& ch_cnt_ref );
    rc_t class_preset_value_channel_count( const flow_t* p, const char* class_desc_label, const char* class_preset_label, const char* var_label, unsigned& ch_cnt_ref );


    rc_t class_preset_has_var( const class_preset_t* class_preset, const char* var_label, bool& fl_ref );
    rc_t class_preset_has_var( const class_desc_t* class_desc, const char* class_preset_label, const char* var_label, bool& fl_ref );
    rc_t class_preset_has_var( const flow_t* p, const char* class_desc_label, const char* class_preset_label, const char* var_label, bool& fl_ref );
    


    
    
    //------------------------------------------------------------------------------------------------------------------------
    //
    // Flow
    //
    
    void    class_dict_print( flow_t* p );

    external_device_t* external_device_find( flow_t* p, const char* device_label, unsigned typeId, unsigned inOrOutFl, const char* midiPortLabel=nullptr );


    
    //------------------------------------------------------------------------------------------------------------------------
    //
    // Network
    //

    
    void     network_print(const network_t& net );

    const network_preset_t* network_preset_from_label( const network_t& net, const char* preset_label );
    
    unsigned proc_mult_count( const network_t& net, const char* proc_label );
    
    rc_t     proc_mult_sfx_id_array( const network_t& net, const char* proc_label, unsigned* idA, unsigned idAllocN, unsigned& idN_ref );

    const object_t* network_find_record_format( const network_t& net, const char* format_name );

    
    //------------------------------------------------------------------------------------------------------------------------
    //
    // Proc
    //

    void               proc_destroy( proc_t* proc );
    rc_t               proc_validate( proc_t* proc );
    
    proc_t*            proc_find( network_t& net, const char* proc_label, unsigned sfx_id );
    rc_t               proc_find( network_t& net, const char* proc_label, unsigned sfx_id, proc_t*& procPtrRef );

    const class_preset_t*   proc_preset_find( const proc_t* cd, const char* preset_label );

    // A) If 'recd_cfg_specifier' is a list then it must contain exactly one string element.
    //    and that element must conform to rules in B).
    // B) If 'recd_cfg_specifier' is a string then it must:
    //    1. Match a label in the the local net record registry.
    //    2. Match a proc/var desc identifer ("<proc_desc_label>.<var_desc_label>") of a var description containing a record format cfg.
    // C) If 'recd_cfg_specifier' is a dictionary then it must match one of two syntaxes:
    //    1. The syntax expected by recd_format_create().
    //    2. { alloc_cnt:<>, fields:<string> } where 'alloc_cnt' is optional and <string> conforms to the rules in B).
    //
    // Notes:
    // 1. No records are actually allocated by this call. alloc_recdN is simply assigned to recd_fmt_ref.alloc_cnt.
    // 2. The actual value of of recd_fmt_t.alloc_cnt is the max. of allocRecdN, alloc_cnt from the C) 2. syntax,
    //    and any 'alloc_cnt' field that happens to be in the located record fmt cfg.
    // 3. It is the responsiblity of the caller to release the recd_fmt_t create by this call.
    rc_t               proc_recd_format_create( proc_t* proc, const object_t* recd_cfg_specifier, unsigned alloc_recdN, recd_fmt_t*& recd_fmt_ref );

    // Access a blob stored via global_var()
    void*              global_var(       proc_t* proc, const char* var_label );
    
    // Copy a named blob into the network global variable space.
    rc_t               global_var_alloc( proc_t* proc, const char* var_label, const void* blob, unsigned blobByteN );
    
    void               proc_print( proc_t* proc );

    rc_t               proc_log_msg( proc_t* proc, variable_t* var, log::handle_t logH, log::logLevelId_t log_level, const char* function, const char* file, unsigned line, rc_t rc, const char* fmt, va_list vl );
    rc_t               proc_log_msg( proc_t* proc, variable_t* var, log::handle_t logH, log::logLevelId_t log_level, const char* function, const char* file, unsigned line, rc_t rc, const char* fmt, ... );


    // Count of all var instances on this proc.  This is a count of the length of proc->varL.
    unsigned           proc_var_count( proc_t* proc );

    // If fname has a '$' prefix then the system project directory is prepended to it.
    // If fname has a '~' then the users home directory is prepended to it.
    // The returned string must be release with a call to mem::free().
    char*              proc_expand_filename( const proc_t* proc, const char* fname );

    // Call this function from inside the proc instance exec() routine, with flags=kCallbackPnFl,
    // to get callbacks on variables marked for notification on change. Note that variables must have
    // their var. description 'kNotifyVarDescFl' (var. desc flag: 'notify') set in order
    // to generate callbacks.
    // Set kQuietPnFl to avoid warning messages when this function is called on proc's
    // that do not have any variables marked for notification.
    enum { kCallbackPnFl=0x01, kQuietPnFl=0x02 };
    rc_t               proc_notify( proc_t* proc, unsigned flags = kCallbackPnFl );

    // Execute a proc instance by calling it's custom 'exec' function.
    // Returns kEofRC to indicate that the network that this proc belongs to
    // show shutdown at the end of this execution cycle.
    rc_t               proc_exec( proc_t* proc );
    
    //------------------------------------------------------------------------------------------------------------------------
    //
    // Variable
    //

    // Create a variable but do not assign it a value.  Return a pointer to the new variable.
    // Notes:
    // 1) `value_cfg` is optional. Set it to NULL to ignore
    // 2) If `altTypeFl` is not set to kInvalidTFl then the var is assigned this type.
    rc_t           var_create( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, const object_t* value_cfg, unsigned altTypeFlag, variable_t*& varRef );
    void           var_destroy( variable_t* var );

    // Channelizing creates a new var record with an explicit channel index to replace the
    // automatically generated variable whose channel index is set to  'kAnyChIdx'.
    rc_t           var_channelize( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned chIdx, const object_t* value_cfg, unsigned vid, variable_t*& varRef );

    // Get the count of channels attached to var_label:sfx_id:kAnyChIdx.
    // Returns 0 if only kAnyChIdx exists,
    // Returns kInvalidCnt if var_label:sfx_id does not exist.
    // Otherwise returns count of channels no including kAnyChIdx. (e.g. mono=1, stereo=2, quad=4 ...)
    unsigned       var_channel_count( proc_t* proc, const char* var_label, unsigned sfx_id );

    // Calls the _mod_var_map_update() on connected vars and implements the var 'log' functionality
    rc_t           var_schedule_notification( variable_t* var );

    // Sets and get the var->flags field
    unsigned       var_flags(     proc_t* proc, unsigned chIdx, const char* var_label, unsigned sfx_id, unsigned& flags_ref );
    rc_t           var_set_flags( proc_t* proc, unsigned chIdx, const char* var_label, unsigned sfx_id, unsigned flags );
    rc_t           var_clr_flags( proc_t* proc, unsigned chIdx, const char* var_label, unsigned sfx_id, unsigned flags );

    // `value_cfg` is optional. Set it to NULL to ignore
    rc_t           var_register( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, const object_t* value_cfg, variable_t*& varRef );

    // Returns true if this var is connected to a source proc variable
    bool           is_connected_to_source( const variable_t* var );

    // Return true if this var is acting as a source for another var.
    bool           is_a_source_var( const variable_t* var );

    // Returns true if var->varDesc->fmt.recd_fmt is valid.
    bool           var_has_recd_format( const variable_t* var );

    // Connect in_var to src_var. 
    void           var_connect( variable_t* src_var, variable_t* in_var );

    // Disconnect an in_var from it's source
    void           var_disconnect( variable_t* in_var );

    // Get the count of 'mult' vars associated with this var label.
    unsigned       var_mult_count( proc_t* proc, const char* var_label );
    
    // Get all the label-sfx-id's associated with a give var label
    rc_t           var_mult_sfx_id_array( proc_t* proc, const char* var_label, unsigned* idA, unsigned idAllocN, unsigned& idN_ref );

    // Send a variable value to the UI
    rc_t           var_send_to_ui( variable_t* var );
    rc_t           var_send_to_ui( proc_t* proc, unsigned vid,  unsigned chIdx );
    rc_t           var_send_to_ui_enable( proc_t* proc, unsigned vid,  unsigned chIdx, bool enable_fl );
    rc_t           var_send_to_ui_show(   proc_t* proc, unsigned vid,  unsigned chIdx, bool show_fl );


    //-----------------
    //
    // var_register
    //
    
    inline rc_t _var_reg(cw::flow::proc_t*, unsigned int ) { return kOkRC; }
    
    template< typename T0, typename T1,  typename... ARGS >
    rc_t _var_reg( proc_t* proc, unsigned chIdx, T0 vid, T1 var_label, unsigned sfx_id, ARGS&&... args )
    {
      rc_t rc;
      variable_t* dummy = nullptr;
      if((rc = var_register( proc, var_label, sfx_id, vid, chIdx, nullptr, dummy )) == kOkRC )        
        if((rc = _var_reg( proc, chIdx, std::forward<ARGS>(args)...)) != kOkRC )
          return rc;
      return rc;
    }

    // Call var_register() on a list of variables.
    template< typename... ARGS >
    rc_t           var_register( proc_t* proc, unsigned chIdx, unsigned vid, const char* var_label, unsigned sfx_id, ARGS&&... args )
    {  return _var_reg( proc, chIdx, vid, var_label, sfx_id, std::forward<ARGS>(args)...); }



    //---------------------
    //
    // var_register_and_get
    //

    inline rc_t _var_register_and_get(cw::flow::proc_t*, unsigned int ) { return kOkRC; }

    template< typename T>
    rc_t var_register_and_get( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, T& valRef )
    {
      rc_t rc;
      variable_t* var;
      if((rc = var_register(proc,var_label,sfx_id,vid,chIdx,nullptr,var)) == kOkRC )
        rc = var_get(var,valRef);
      return rc;
    }
      
    
    inline rc_t _var_reg_and_get(cw::flow::proc_t*, unsigned int ) { return kOkRC; }

    template< typename T0, typename T1, typename T2, typename... ARGS >
    rc_t _var_reg_and_get( proc_t* proc, unsigned chIdx, T0 vid, T1 var_label, unsigned sfx_id, T2& valRef, ARGS&&... args )
    {
      rc_t rc;

      if((rc = var_register_and_get( proc, var_label, sfx_id, vid, chIdx, valRef )) == kOkRC )        
        if((rc = _var_reg_and_get( proc, chIdx, std::forward<ARGS>(args)...)) != kOkRC )
          return rc;
      
      return rc;
    }

    // Call var_register_and_get() on a list of variables.
    template< typename... ARGS >
    rc_t           var_register_and_get( proc_t* proc, unsigned chIdx, unsigned vid, const char* var_label, unsigned sfx_id,  ARGS&&... args )
    {  return _var_reg_and_get( proc, chIdx, vid, var_label, sfx_id, std::forward<ARGS>(args)...); }


    
    //---------------------
    //
    // var_register_and_set
    //
    
    // var_register_and_set().  If the variable has not yet been created then it is created and assigned a value.
    // If the variable has already been created then 'vid' and the value are updated.
    // (Note that abuf and fbuf values are not changed by this function only the 'vid' is updated.)
    rc_t           var_register_and_set( proc_t* proc, const char* label,  unsigned sfx_id,   unsigned vid, unsigned chIdx, variable_t*& varRef );
    
    rc_t           var_register_and_set( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, unsigned frameN );
    rc_t           var_register_and_set( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, midi::ch_msg_t* midiA, unsigned midiN );
    rc_t           var_register_and_set( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, const unsigned* maxBinN_V, const unsigned* binN_V, const unsigned* hopSmpN_V, const fd_sample_t** magV=nullptr, const fd_sample_t** phsV=nullptr, const fd_sample_t** hzV=nullptr );
    rc_t           var_register_and_set( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, unsigned maxBinN, unsigned binN, unsigned hopSmpN, const fd_sample_t** magV=nullptr, const fd_sample_t** phsV=nullptr, const fd_sample_t** hzV=nullptr );
    rc_t           var_register_and_set( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, const recd_type_t* recd_type, recd_t* recdA, unsigned recdN, unsigned maxRecdN );

    // If the var description has a 'recd_format_t' compatible cfg. in `var_desc_t.fmt.recd_fmt` then
    // use this function to allocate a backing recd_array_t buffer for it and then register a variable
    // which will point into the buffer.  Note that it is the callers responsibility to eventually
    // call recd_array_destroy() on the recd_array_t object returned in recd_array_ref.
    // Note that the rbuf_t served by the variable in this call will be initialy empty (recdA=nullptr,recdN=0)
    // Alloc a recd_array, using an internal call to var_alloc_recd_array(), and assign all records to the specified variable.
    // The caller is responsible for destroying (recd_array_destroy()) the returned recd_array.    
    rc_t           var_alloc_register_and_set( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, const recd_type_t* base, recd_array_t*& recd_arrray_ref, unsigned allocRecdN=0 );


    // Alloc the recd_array_t based on the recd_format_t included in the variable description. See var_desc_t.fmt.recd_fmt.
    // If arg. `allocRecdN` is non-zero then the allocated size of the recd_array_t will be  max(allocRecdN,recd_format_t.allocRecdN)
    // where recd_format_t.allocRecdN is taken from the variables recd_format_t.
    // The caller is responsible for destroying (recd_array_destroy()) the returned recd_array.
    rc_t           var_alloc_record_array( proc_t* proc, const char* var_label, unsigned sfx_id, unsigned chIdx, const recd_type_t* base, recd_array_t*& recd_array_ref, unsigned allocRecdN=0 );
    
    
    inline rc_t _var_register_and_set(cw::flow::proc_t*, unsigned int ) { return kOkRC; }

    template< typename T0, typename T1, typename T2, typename... ARGS >
    rc_t _var_register_and_set( proc_t* proc, unsigned chIdx, T0 vid, T1 var_label, unsigned sfx_id, T2 val, ARGS&&... args )
    {
      rc_t rc;

      variable_t* var = nullptr;
      if((rc = var_register_and_set( proc, var_label, sfx_id, vid, chIdx, var)) == kOkRC )
      {
        if((rc = var_set( proc, vid, chIdx, val )) != kOkRC )
          return rc;
        
        if((rc = _var_register_and_set( proc, chIdx, std::forward<ARGS>(args)...)) != kOkRC )
          return rc;
      }
      
      return rc;
    }

    // Call var_register_and_set() on a list of variables.
    template< typename... ARGS >
    rc_t           var_register_and_set( proc_t* proc, unsigned chIdx, unsigned vid, const char* var_label, unsigned sfx_id, ARGS&&... args )
    {  return _var_register_and_set( proc, chIdx, vid, var_label, sfx_id, std::forward<ARGS>(args)...); }



    void           _var_destroy( variable_t* var );

    bool           var_exists(      proc_t* proc, const char* label, unsigned sfx_id, unsigned chIdx );
    bool           var_has_value(   proc_t* proc, const char* label, unsigned sfx_id, unsigned chIdx );
    bool           var_is_a_source( proc_t* proc, const char* label, unsigned sfx_id, unsigned chIdx );
    bool           var_is_a_source( proc_t* proc, unsigned vid, unsigned chIdx );

    rc_t           var_find(   proc_t* proc, const char* var_label, unsigned sfx_id, unsigned chIdx, const variable_t*& varRef );
    rc_t           var_find(   proc_t* proc, const char* var_label, unsigned sfx_id, unsigned chIdx,       variable_t*& varRef );
    rc_t           var_find(   proc_t* proc, unsigned vid,                           unsigned chIdx,       variable_t*& varRef );

    
    // Count of numbered channels - does not count the kAnyChIdx variable instance.
    rc_t           var_channel_count( proc_t* proc, const char* label, unsigned sfx_idx, unsigned& chCntRef );
    rc_t           var_channel_count( const variable_t* var, unsigned& chCntRef );



    //
    // var_get() coerces the value of the variable to the type of the returned value.
    //
    
    rc_t var_get( const variable_t* var, bool&            valRef );
    rc_t var_get( const variable_t* var, uint_t&          valRef );
    rc_t var_get( const variable_t* var, int_t&           valRef );
    rc_t var_get( const variable_t* var, float&           valRef );
    rc_t var_get( const variable_t* var, double&          valRef );
    rc_t var_get( const variable_t* var, const char*&     valRef );    
    rc_t var_get( const variable_t* var, const abuf_t*&   valRef );    
    rc_t var_get(       variable_t* var, abuf_t*&         valRef );    
    rc_t var_get( const variable_t* var, const fbuf_t*&   valRef );
    rc_t var_get(       variable_t* var, fbuf_t*&         valRef );
    rc_t var_get( const variable_t* var, const mbuf_t*&   valRef );
    rc_t var_get(       variable_t* var, mbuf_t*&         valRef );
    rc_t var_get( const variable_t* var, const rbuf_t*&   valRef );
    rc_t var_get(       variable_t* var, rbuf_t*&         valRef );
    rc_t var_get( const variable_t* var, const object_t*& valRef );

    template< typename T>
    rc_t var_get( proc_t* proc, unsigned vid, unsigned chIdx, T& valRef)
    {
      rc_t        rc  = kOkRC;
      variable_t* var = nullptr;
      
      if((rc = var_find(proc, vid, chIdx, var )) == kOkRC )
        rc = var_get(var,valRef);
      
      return rc;  
    }

    template< typename T>
    rc_t var_get( proc_t* proc, unsigned vid, T& valRef)
    { return var_get<T>(proc,vid,kAnyChIdx,valRef); }
    
    template< typename T >
    T val_get( proc_t* proc, unsigned vid, unsigned chIdx )
    {
      T value;
      var_get(proc,vid,chIdx,value);
      return value;
    }

    template< typename T >
    T val_get( proc_t* proc, unsigned vid )
    { return var_get<T>(proc,vid,kAnyChIdx); }
    
    //
    //  var_set() coerces the incoming value to the type of the variable (var->type)
    //

    rc_t var_set_from_cfg( variable_t* var, const object_t* cfg_value );

    rc_t var_set( variable_t* var, const value_t* val );
    rc_t var_set( variable_t* var, bool val );
    rc_t var_set( variable_t* var, uint_t val );
    rc_t var_set( variable_t* var, int_t val );
    rc_t var_set( variable_t* var, float val );
    rc_t var_set( variable_t* var, double val );
    rc_t var_set( variable_t* var, const char* val );
    rc_t var_set( variable_t* var, abuf_t* val );
    rc_t var_set( variable_t* var, fbuf_t* val );
    rc_t var_set( variable_t* var, mbuf_t* val );
    rc_t var_set( variable_t* var, rbuf_t* val );
    rc_t var_set( variable_t* var, const object_t* val );
    
    rc_t var_set( proc_t* proc, unsigned vid, unsigned chIdx, const value_t* val );
    rc_t var_set( proc_t* proc, unsigned vid, unsigned chIdx, bool val );
    rc_t var_set( proc_t* proc, unsigned vid, unsigned chIdx, uint_t val );
    rc_t var_set( proc_t* proc, unsigned vid, unsigned chIdx, int_t val );
    rc_t var_set( proc_t* proc, unsigned vid, unsigned chIdx, float val );
    rc_t var_set( proc_t* proc, unsigned vid, unsigned chIdx, double val );
    rc_t var_set( proc_t* proc, unsigned vid, unsigned chIdx, const char* val );
    rc_t var_set( proc_t* proc, unsigned vid, unsigned chIdx, abuf_t* val );
    rc_t var_set( proc_t* proc, unsigned vid, unsigned chIdx, fbuf_t* val );
    rc_t var_set( proc_t* proc, unsigned vid, unsigned chIdx, mbuf_t* val );
    rc_t var_set( proc_t* proc, unsigned vid, unsigned chIdx, rbuf_t* val );
    rc_t var_set( proc_t* proc, unsigned vid, unsigned chIdx, const object_t* val );


    inline rc_t var_set( proc_t* proc, unsigned vid, const value_t* val )  { return var_set(proc,vid,kAnyChIdx,val); }
    inline rc_t var_set( proc_t* proc, unsigned vid, bool val )            { return var_set(proc,vid,kAnyChIdx,val); }
    inline rc_t var_set( proc_t* proc, unsigned vid, uint_t val )          { return var_set(proc,vid,kAnyChIdx,val); }
    inline rc_t var_set( proc_t* proc, unsigned vid, int_t val )           { return var_set(proc,vid,kAnyChIdx,val); }
    inline rc_t var_set( proc_t* proc, unsigned vid, float val )           { return var_set(proc,vid,kAnyChIdx,val); }
    inline rc_t var_set( proc_t* proc, unsigned vid, double val )          { return var_set(proc,vid,kAnyChIdx,val); }
    inline rc_t var_set( proc_t* proc, unsigned vid, const char* val )     { return var_set(proc,vid,kAnyChIdx,val); }
    inline rc_t var_set( proc_t* proc, unsigned vid, abuf_t* val )         { return var_set(proc,vid,kAnyChIdx,val); }
    inline rc_t var_set( proc_t* proc, unsigned vid, fbuf_t* val )         { return var_set(proc,vid,kAnyChIdx,val); }
    inline rc_t var_set( proc_t* proc, unsigned vid, mbuf_t* val )         { return var_set(proc,vid,kAnyChIdx,val); }
    inline rc_t var_set( proc_t* proc, unsigned vid, rbuf_t* val )         { return var_set(proc,vid,kAnyChIdx,val); }
    inline rc_t var_set( proc_t* proc, unsigned vid, const object_t* val ) { return var_set(proc,vid,kAnyChIdx,val); }

    
  }
}

#define proc_printf(proc,   fmt,...) cw::flow::proc_log_msg( proc, nullptr, cw::log::globalHandle(), cw::log::kPrint_LogLevel,   __FUNCTION__, __FILE__, __LINE__, kOkRC, fmt, ##__VA_ARGS__ )
#define proc_debug( proc,   fmt,...) cw::flow::proc_log_msg( proc, nullptr, cw::log::globalHandle(), cw::log::kDebug_LogLevel,   __FUNCTION__, __FILE__, __LINE__, kOkRC, fmt, ##__VA_ARGS__ )
#define proc_info(  proc,   fmt,...) cw::flow::proc_log_msg( proc, nullptr, cw::log::globalHandle(), cw::log::kInfo_LogLevel,    __FUNCTION__, __FILE__, __LINE__, kOkRC, fmt, ##__VA_ARGS__ )
#define proc_warn(  proc,   fmt,...) cw::flow::proc_log_msg( proc, nullptr, cw::log::globalHandle(), cw::log::kWarning_LogLevel, __FUNCTION__, __FILE__, __LINE__, kOkRC, fmt, ##__VA_ARGS__ )
#define proc_error( proc,rc,fmt,...) cw::flow::proc_log_msg( proc, nullptr, cw::log::globalHandle(), cw::log::kError_LogLevel,   __FUNCTION__, __FILE__, __LINE__, rc,    fmt, ##__VA_ARGS__ )

#define var_printf(proc,  fmt,...) cw::flow::proc_log_msg( nullptr, var, cw::log::globalHandle(), cw::log::kPrint_LogLevel,   __FUNCTION__, __FILE__, __LINE__, kOkRC, fmt, ##__VA_ARGS__ )
#define var_debug( proc,  fmt,...) cw::flow::proc_log_msg( nullptr, var, cw::log::globalHandle(), cw::log::kDebug_LogLevel,   __FUNCTION__, __FILE__, __LINE__, kOkRC, fmt, ##__VA_ARGS__ )
#define var_info(  proc,  fmt,...) cw::flow::proc_log_msg( nullptr, var, cw::log::globalHandle(), cw::log::kInfo_LogLevel,    __FUNCTION__, __FILE__, __LINE__, kOkRC, fmt, ##__VA_ARGS__ )
#define var_warn(  var,   fmt,...) cw::flow::proc_log_msg( nullptr, var, cw::log::globalHandle(), cw::log::kWarning_LogLevel, __FUNCTION__, __FILE__, __LINE__, kOkRC, fmt, ##__VA_ARGS__ )
#define var_error( var,rc,fmt,...) cw::flow::proc_log_msg( nullptr, var, cw::log::globalHandle(), cw::log::kError_LogLevel,   __FUNCTION__, __FILE__, __LINE__, rc, fmt, ##__VA_ARGS__ )

