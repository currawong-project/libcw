namespace cw
{
  namespace flow
  {

    typedef dsp::coeff_t     coeff_t;
    typedef dsp::sample_t    sample_t;
    typedef dsp::fd_sample_t fd_sample_t;
    typedef dsp::srate_t     srate_t;
    typedef dsp::ftime_t     ftime_t;
    typedef unsigned         uint_t;
    typedef int              int_t;
    
    
    typedef unsigned vid_t;

    enum {
      kBaseSfxId = 0,
      kFbufVectN = 3,  // count of signal vectors in fbuf (mag,phs,hz)
      kAnyChIdx = kInvalidIdx,
      kLocalValueN = 2,
      kDefaultFramesPerCycle=64,
      kDefaultSampleRate=48000
    };
        
    typedef struct abuf_str
    {
      srate_t            srate;        // Signal sample rate
      unsigned           chN;          // Count of channels
      unsigned           frameN;       // Count of sample frames per channel
      unsigned           bufAllocSmpN; // Size of allocated buf[] in samples.
      sample_t*          buf;          // buf[ chN ][ frameN ]
    } abuf_t;


    typedef struct fbuf_str
    {
      unsigned          memByteN;  // Count of bytes in mem[].
      void*             mem;       // mem[ memByteN ] All dynamically allocated memory used by this fbuf.
      
      srate_t           srate;     // signal sample rate
      unsigned          flags;     // See kXXXFbufFl
      unsigned          chN;       // count of channels
      unsigned*         maxBinN_V; // maxBinN_V[chN] max value that binN_V[i] is allowed to take
      unsigned*         binN_V;    // binN_V[ chN ] count of sample frames per channel
      unsigned*         hopSmpN_V; // hopSmpN_V[ chN ] hop sample count 
      fd_sample_t**     magV;      // magV[ chN ][ binN ]
      fd_sample_t**     phsV;      // phsV[ chN ][ binN ]
      fd_sample_t**     hzV;       // hzV[ chN ][ binN ]
      bool*             readyFlV;  // readyFlV[chN] true if this channel is ready to be processed (used to sync. fbuf rate to abuf rate)
    } fbuf_t;

    typedef struct mbuf_str
    {
      const midi::ch_msg_t* msgA;
      unsigned              msgN;
    } mbuf_t;

    enum
    {
      kInvalidTFl  = 0x00000000,
      kBoolTFl     = 0x00000001,
      kUIntTFl     = 0x00000002,
      kIntTFl      = 0x00000004,
      kFloatTFl    = 0x00000008,
      kDoubleTFl   = 0x00000010,
      
      kBoolMtxTFl  = 0x00000020,
      kUIntMtxTFl  = 0x00000040,
      kIntMtxTFl   = 0x00000080,
      kFloatMtxTFl = 0x00000100,
      kDoubleMtxTFl= 0x00000200,
      
      kABufTFl     = 0x00000400,
      kFBufTFl     = 0x00000800,
      kMBufTFl     = 0x00001000,
      kStringTFl   = 0x00002000,
      kTimeTFl     = 0x00004000,
      kCfgTFl      = 0x00008000,

      kTypeMask    = 0x0000ffff,

      kRuntimeTFl  = 0x80000000,

      kNumericTFl = kBoolTFl | kUIntTFl | kIntTFl | kFloatTFl | kDoubleTFl,
      kMtxTFl     = kBoolMtxTFl | kUIntMtxTFl | kIntMtxTFl | kFloatMtxTFl | kDoubleMtxTFl,
      kAllTFl     = kTypeMask
    };

    typedef struct mtx_str
    {
      union {
        struct mtx::mtx_str< unsigned >* u;
        struct mtx::mtx_str< int >*      i;
        struct mtx::mtx_str< float >*    f;
        struct mtx::mtx_str< double >*   d;
      } u;
    } mtx_t;
    
    typedef struct value_str
    {
      unsigned tflag;
      
      union {
        bool            b;
        uint_t          u;
        int_t           i;
        float           f;
        double          d;
        
        mtx_t*          mtx;        
        abuf_t*         abuf;
        fbuf_t*         fbuf;
        mbuf_t*         mbuf;
        
        char*           s;
        
        const object_t* cfg;
        void*           p;

      } u;
      
      struct value_str* link;
      
    } value_t;

        
    struct proc_str;
    struct variable_str;
        
    typedef rc_t (*member_func_t)( struct proc_str* ctx );
    typedef rc_t (*member_value_func_t)( struct proc_str* ctx, struct variable_str* var );

    // var_desc_t attribute flags
    enum
    {
      kInvalidVarDescFl   = 0x00,
      kSrcVarDescFl       = 0x01,
      kSrcOptVarDescFl    = 0x02,
      kNoSrcVarDescFl     = 0x04,
      kInitVarDescFl      = 0x08,
      kMultVarDescFl      = 0x10,
      kSubnetOutVarDescFl = 0x20
    };
    
    typedef struct class_members_str
    {
      member_func_t       create;
      member_func_t       destroy;  
      member_value_func_t value;
      member_func_t       exec;
      member_func_t       report;
    } class_members_t;
    
    typedef struct var_desc_str
    {
      const object_t*      cfg;     // The cfg object that describes this variable from 'flow_class'.
      const object_t*      val_cfg; // An object containing the default value for this variable.
      const char*          label;   // Name of this var. 
      unsigned             type;    // Value type id (e.g. kBoolTFl, kIntTFl, ...)
      unsigned             flags;   // Attributes for this var. (e.g. kSrcVarFl )
      const char*          docText; // User help string for this var.
      
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
      class_preset_t*   presetL;    // presetA[ presetN ]
      class_members_t*  members;    // member functions for this class
      unsigned          polyLimitN; // max. poly copies of this class per network_t or 0 if no limit
    } class_desc_t;

    enum {
      kInvalidVarFl    = 0x00,
      kLogVarFl        = 0x01,
      kProxiedVarFl    = 0x02,
      kProxiedOutVarFl = 0x04
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
      unsigned             flags;        // kLogVarFl
      unsigned             type;         // This is the value type as established when the var is initialized - it never changes for the life of the var.
            
      var_desc_t*          classVarDesc; // pointer to this variables class var desc
      var_desc_t*          localVarDesc; // pointer to this variables local var desc - if it doesn't match classVarDesc.
      var_desc_t*          varDesc;      // the effective variable description for this variable (set to classVarDesc or localVarDesc)
      
      value_t              local_value[ kLocalValueN ]; // the local value instance (actual value if this is not a 'src' variable)
      unsigned             local_value_idx;             // local_value[] is double buffered to allow the cur value of the buf[] to be held while the next value is validated (see _var_set_template())
      struct variable_str* src_var;                     // pointer to this input variables source link (or null if it uses the local_value)
      value_t*             value;                       // pointer to the value associated with this variable
      
      struct variable_str* var_link;     // instance.varL list link
      struct variable_str* ch_link;      // list of channels that share this variable (rooted on 'any' channel - in order by channel number)

      struct variable_str* dst_head;     // Pointer to list of out-going connections (null on var's that do not have out-going connections)
      struct variable_str* dst_tail;     // 
      struct variable_str* dst_link;     // Link used by dst_head list.

    } variable_t;


    struct network_str;
    
    typedef struct proc_str
    {
      struct flow_str*    ctx;  // global system context
      struct network_str* net;  // network which owns this proc

      class_desc_t*   class_desc;    //
      
      char*           label;         // instance label
      unsigned        label_sfx_id;  // label suffix id (set to kBaseSfxId (0) unless poly is non-null)
      
      const object_t* proc_cfg;      // instance configuration
            
      const char*     arg_label;     // optional args label
      const object_t* arg_cfg;       // optional args configuration

      void*           userPtr;       // instance state

      variable_t*     varL;          // linked list of all variables on this instance

      unsigned        varMapChN;     // max count of channels (max 'chIdx' + 2) among all variables on this instance, (2=kAnyChIdx+index to count)
      unsigned        varMapIdN;     // max 'vid' among all variables on this instance 
      unsigned        varMapN;       // varMapN = varMapIdN * varMapChN 
      variable_t**    varMapA;       // varMapA[ varMapN ] = allows fast lookup from ('vid','chIdx) to variable

      struct network_str*  internal_net;
      
    } proc_t;


    // preset_value_t holds a preset value and the proc/var to which it will be applied.
    typedef struct preset_value_str
    {
      proc_t*                  proc;       // proc target for this preset value
      variable_t*              var;        // var target for this preset value
      value_t                  value;      // Preset value.
      unsigned                 pairTblIdx; // Index into the preset pair table for this preset value
      struct preset_value_str* link;
    } preset_value_t;

    typedef struct preset_value_list_str
    {
      preset_value_t* value_head;  // List of preset_value_t for this preset. 
      preset_value_t* value_tail;  // Last preset value in the list.
    } preset_value_list_t;

    struct network_preset_str;

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

    typedef struct net_global_var_str
    {
      const char* class_label;
      char*       var_label;
      void*       blob;
      unsigned    blobByteN;
      
      struct net_global_var_str* link;
      
    } net_global_var_t;

    typedef struct network_str
    {
      const object_t*   procsCfg;   // network proc list
      const object_t*   presetsCfg; // presets designed for this network

      unsigned*         poly_proc_idxA;  // poly_proc_idxA[ poly_cnt ]. Index into proc_array[] of first proc in each network
      unsigned          poly_cnt; // count of duplicated networks in the list
      
      struct proc_str** proc_array;
      
      unsigned          proc_arrayAllocN;
      unsigned          proc_arrayN;

      network_preset_t* presetA;
      unsigned          presetN;

      // Preset pair table used by network_apply_dual_preset()
      network_preset_pair_t* preset_pairA;
      unsigned               preset_pairN;

      net_global_var_t* globalVarL;
    } network_t;
    
    
    typedef struct flow_str
    {
      const object_t*      flowCfg;     // complete cfg used to create this flow
      const object_t*      networkCfg;  // 'network' cfg from flowCfg

      bool                 printNetworkFl;
      bool                 non_real_time_fl;     // set if this is a non-real-time program
      unsigned             framesPerCycle;       // sample frames per cycle (64)
      srate_t              sample_rate;          // default sample rate (48000.0)
      unsigned             maxCycleCount;        // count of cycles to run on flow::exec() or 0 if there is no limit.
      const char*          init_net_preset_label;// network initialization preset label or nullptr if there is no net. init. preset
      
      bool                 isInRuntimeFl;        // Set when compile-time is complete
      
      unsigned             cycleIndex;           // Incremented with each processing cycle

      bool                 printLogHdrFl;
      
      bool                 multiPriPresetProbFl; // If set then probability is used to choose presets on multi-preset application
      bool                 multiSecPresetProbFl; // 
      bool                 multiPresetInterpFl;  // If set then interpolation is applied between two selectedd presets on multi-preset application
      
      class_desc_t*        classDescA;           // 
      unsigned             classDescN;           //

      class_desc_t*        subnetDescA;          // 
      unsigned             subnetDescN;          //
      
      external_device_t*   deviceA;              // deviceA[ deviceN ] external device description array
      unsigned             deviceN;              //

      const char*          proj_dir;             // default input/output directory
      
      network_t net;

    } flow_t;

    //------------------------------------------------------------------------------------------------------------------------
    //
    // Value Only
    //

    inline void set_null( value_t& v, unsigned tflag ) { v.tflag=tflag; v.u.p=nullptr; }
    inline bool is_numeric( const value_t* v ) { return cwIsFlag(v->tflag,kNumericTFl); }
    inline bool is_matrix(  const value_t* v ) { return cwIsFlag(v->tflag,kMtxTFl); }

    // if all of the src flags are set in the dst flags then the two types are convertable.
    inline bool can_convert( unsigned src_tflag, unsigned dst_tflag ) { return (src_tflag&dst_tflag)==src_tflag; }

    
    abuf_t*         abuf_create( srate_t srate, unsigned chN, unsigned frameN );
    void            abuf_destroy( abuf_t*& buf );
    
    // If 'dst' is null then a new abuf is allocated, filled with the contents of 'src'.
    // If 'dst' is non-null and there is enough space for the contents of 'src' then only a copy is executed.
    // If there is not enough space then dst is reallocated.
    abuf_t*         abuf_duplicate( abuf_t* dst, const abuf_t* src );
    rc_t            abuf_set_channel( abuf_t* buf, unsigned chIdx, const sample_t* v, unsigned vN );
    const sample_t* abuf_get_channel( abuf_t* buf, unsigned chIdx );

    fbuf_t*        fbuf_create( srate_t srate, unsigned chN, const unsigned* maxBinN_V, const unsigned* binN_V, const unsigned* hopSmpN_V, const fd_sample_t** magV=nullptr, const fd_sample_t** phsV=nullptr, const fd_sample_t** hzV=nullptr );
    fbuf_t*        fbuf_create( srate_t srate, unsigned chN, unsigned maxBinN, unsigned binN, unsigned hopSmpN, const fd_sample_t** magV=nullptr, const fd_sample_t** phsV=nullptr, const fd_sample_t** hzV=nullptr );
    void           fbuf_destroy( fbuf_t*& buf );

    // Memory allocation will only occur if dst is null, or the size of dst's internal buffer are too small.
    fbuf_t*        fbuf_duplicate( fbuf_t* dst, const fbuf_t* src );

    mbuf_t*        mbuf_create( const midi::ch_msg_t* msgA=nullptr, unsigned msgN=0 );
    void           mbuf_destroy( mbuf_t*& buf );
    mbuf_t*        mbuf_duplicate( const mbuf_t* src );
    
    inline bool    value_is_abuf( const value_t* v ) { return v->tflag & kABufTFl; }
    inline bool    value_is_fbuf( const value_t* v ) { return v->tflag & kFBufTFl; }

    unsigned       value_type_label_to_flag( const char* type_desc );
    const char*    value_type_flag_to_label( unsigned flag );

    void           value_duplicate( value_t& dst, const value_t& src );

    void           value_print( const value_t* value, bool info_fl=false);
    
    //------------------------------------------------------------------------------------------------------------------------
    //
    // Class and Variable Description
    //

    var_desc_t*       var_desc_create( const char* label, const object_t* value_cfg );
    void              var_desc_destroy( var_desc_t* var_desc );
    
    unsigned             var_desc_attr_label_to_flag( const char* attr_label );
    const char*          var_desc_flag_to_attribute( unsigned flag );
    const idLabelPair_t* var_desc_flag_array( unsigned& array_cnt_ref );

    void              class_desc_destroy( class_desc_t* class_desc);
    class_desc_t*     class_desc_find(  flow_t* p, const char* class_desc_label );
    
    var_desc_t*       var_desc_find(       class_desc_t* cd, const char* var_label );
    const var_desc_t* var_desc_find( const class_desc_t* cd, const char* var_label );
    rc_t              var_desc_find(       class_desc_t* cd, const char* var_label, var_desc_t*& vdRef );

    const class_preset_t*   class_preset_find( const class_desc_t* cd, const char* preset_label );
    
    void              class_dict_print( flow_t* p );

    
    //------------------------------------------------------------------------------------------------------------------------
    //
    // Network
    //

    // Access a blob stored via network_global_var()
    void*    network_global_var(       proc_t* proc, const char* var_label );
    
    // Copy a named blob into the network global variable space.
    rc_t     network_global_var_alloc( proc_t* proc, const char* var_label, const void* blob, unsigned blobByteN );

    
    void     network_print(const network_t& net );

    const network_preset_t* network_preset_from_label( const network_t& net, const char* preset_label );
    
    unsigned proc_mult_count( const network_t& net, const char* proc_label );
    
    rc_t     proc_mult_sfx_id_array( const network_t& net, const char* proc_label, unsigned* idA, unsigned idAllocN, unsigned& idN_ref );
        
    
    //------------------------------------------------------------------------------------------------------------------------
    //
    // Proc
    //

    void               proc_destroy( proc_t* proc );
    rc_t               proc_validate( proc_t* proc );
    
    proc_t*            proc_find( network_t& net, const char* proc_label, unsigned sfx_id );
    rc_t               proc_find( network_t& net, const char* proc_label, unsigned sfx_id, proc_t*& procPtrRef );

    external_device_t* external_device_find( flow_t* p, const char* device_label, unsigned typeId, unsigned inOrOutFl, const char* midiPortLabel=nullptr );

    void               proc_print( proc_t* proc );

    // Count of all var instances on this proc.  This is a count of the length of proc->varL.
    unsigned           proc_var_count( proc_t* proc );

    // If fname has a '$' prefix then the system project directory is prepended to it.
    // If fname has a '~' then the users home directory is prepended to it.
    // The returned string must be release with a call to mem::free().
    char*              proc_expand_filename( const proc_t* proc, const char* fname );

    
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

    // Wrapper around call to var->proc->members->value()
    rc_t           var_call_custom_value_func( variable_t* var );

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

    // Connect in_var to src_var. 
    void           var_connect( variable_t* src_var, variable_t* in_var );

    // Disconnect an in_var from it's source
    void           var_disconnect( variable_t* in_var );


    // Get the count of 'mult' vars associated with this var label.
    unsigned       var_mult_count( proc_t* proc, const char* var_label );
    
    // Get all the label-sfx-id's associated with a give var label
    rc_t           var_mult_sfx_id_array( proc_t* proc, const char* var_label, unsigned* idA, unsigned idAllocN, unsigned& idN_ref );
    
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

    rc_t           cfg_to_value( const object_t* cfg, value_t& value_ref );


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

    template< typename T >
    T val_get( proc_t* proc, unsigned vid, unsigned chIdx )
    {
      T value;
      var_get(proc,vid,chIdx,value);
      return value;
    }

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
    rc_t var_set( proc_t* proc, unsigned vid, unsigned chIdx, const object_t* val );

    
  }
}
