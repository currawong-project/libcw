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
      kLocalValueN = 2
    };
        
    typedef struct abuf_str
    {
      struct value_str*  base;
      srate_t            srate;   // signal sample rate
      unsigned           chN;     // count of channels
      unsigned           frameN;  // count of sample frames per channel
      sample_t*          buf;     // buf[ chN ][ frameN ]
    } abuf_t;


    typedef struct fbuf_str
    {      
      struct value_str* base;
      srate_t           srate;     // signal sample rate
      unsigned          flags;     // See kXXXFbufFl
      unsigned          chN;       // count of channels
      unsigned*         maxBinN_V; // max value that binN_V[i] is allowed to take
      unsigned*         binN_V;    // binN_V[ chN ] count of sample frames per channel
      unsigned*         hopSmpN_V; // hopSmpN_V[ chN ] hop sample count 
      fd_sample_t**       magV;      // magV[ chN ][ binN ]
      fd_sample_t**       phsV;      // phsV[ chN ][ binN ]
      fd_sample_t**       hzV;       // hzV[ chN ][ binN ]
      bool*             readyFlV;  // readyFlV[chN] true if this channel is ready to be processed (used to sync. fbuf rate to abuf rate)
      fd_sample_t*        buf;       // memory used by this buffer (or NULL if magV,phsV,hzV point are proxied to another buffer)      
    } fbuf_t;

    typedef struct mbuf_str
    {
      struct value_str*     base;
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
      unsigned flags;
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

      } u;
      
      struct value_str* link;
      
    } value_t;


    inline bool is_numeric( const value_t* v ) { return cwIsFlag(v->flags,kBoolTFl|kUIntTFl|kIntTFl|kFloatTFl|kDoubleTFl); }
    inline bool is_matrix(  const value_t* v ) { return cwIsFlag(v->flags,kBoolMtxTFl|kUIntMtxTFl|kIntMtxTFl|kFloatMtxTFl|kDoubleMtxTFl); }
        
    struct instance_str;
    struct variable_str;
        
    typedef rc_t (*member_func_t)( struct instance_str* ctx );
    typedef rc_t (*member_value_func_t)( struct instance_str* ctx, struct variable_str* var );
    
    enum
    {
      kNoVarFl     = 0x00,
      kSrcVarFl    = 0x01,
      kSrcOptVarFl = 0x02,
      kMultVarFl   = 0x04
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
      struct var_desc_str* link;    // class_desc->varDescL list link
    } var_desc_t;

    typedef struct preset_str
    {
      const char*        label;
      const object_t*    cfg;
      struct preset_str* link;
    } preset_t;
    
    typedef struct class_desc_str
    {
      const object_t*   cfg;       // 
      const char*       label;     // class label;      
      var_desc_t*       varDescL;  // varDescA[varDescN] value description list
      preset_t*         presetL;   // presetA[ presetN ]
      class_members_t*  members;   // member functions for this class
      unsigned          polyLimitN; // max. poly copies of this class per network_t or 0 if no limit
    } class_desc_t;


    // Note: The concatenation of 'vid' and 'chIdx' should form a unique identifier among all variables
    // on a given 'instance'.
    typedef struct variable_str
    {
      struct instance_str* inst;         // pointer to this variables instance
      char*                label;        // this variables label
      unsigned             label_sfx_id; // the label suffix id of this variable or kInvalidIdx if this has no suffix
      unsigned             vid;          // this variables numeric id ( cat(vid,chIdx) forms a unique variable identifier on this 'inst'
      var_desc_t*          varDesc;      // the variable description for this variable
      value_t              local_value[ kLocalValueN ];  // the local value instance (actual value if this is not a 'src' variable)
      unsigned             local_value_idx; // local_value[] is double buffered to allow the cur value of the buf[] to be held while the next value is validated (see _var_set_template())
      value_t*             value;        // pointer to the value associated with this variable   
      unsigned             chIdx;        // channel index
      struct variable_str* src_var;      // pointer to this input variables source link (or null if it uses the local_value)
      struct variable_str* var_link;     // instance.varL link list
      struct variable_str* connect_link; // list of outgoing connections
      struct variable_str* ch_link;      // list of channels that share this variable (rooted on 'any' channel - in order by channel number)
    } variable_t;

    
    struct instance_str;

    typedef struct network_str
    {
      const object_t*      procsCfg;       // network proc list
      const object_t*      presetsCfg;     // presets designed for this network

      unsigned poly_cnt; // count of duplicated networks in the list

      struct instance_str** proc_array;
      unsigned              proc_arrayAllocN;
      unsigned              proc_arrayN;
      
    } network_t;
    
    typedef struct instance_str
    {
      struct flow_str* ctx;          // global system context
      network_t*       net;          // network which owns this proc

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

      network_t*  internal_net;
      
    } instance_t;


    typedef struct flow_str
    {
      const object_t*      flowCfg;     // complete cfg used to create this flow 

      
      unsigned             framesPerCycle;  // sample frames per cycle (64)
      bool                 multiPriPresetProbFl; // If set then probability is used to choose presets on multi-preset application
      bool                 multiSecPresetProbFl; // 
      bool                 multiPresetInterpFl; // If set then interpolation is applied between two selectedd presets on multi-preset application
      unsigned             cycleIndex;     // Incremented with each processing cycle      
      unsigned             maxCycleCount;  // count of cycles to run on flow::exec() or 0 if there is no limit.
      
      class_desc_t*        classDescA;     // 
      unsigned             classDescN;     //

      external_device_t*   deviceA;        // deviceA[ deviceN ] external device description array
      unsigned             deviceN;        //
      
      network_t net;

    } flow_t;

    //------------------------------------------------------------------------------------------------------------------------
    //
    // Value Only
    //
    
    abuf_t*         abuf_create( srate_t srate, unsigned chN, unsigned frameN );
    void            abuf_destroy( abuf_t*& buf );
    abuf_t*         abuf_duplicate( const abuf_t* src );
    rc_t            abuf_set_channel( abuf_t* buf, unsigned chIdx, const sample_t* v, unsigned vN );
    const sample_t* abuf_get_channel( abuf_t* buf, unsigned chIdx );

    fbuf_t*        fbuf_create( srate_t srate, unsigned chN, const unsigned* maxBinN_V, const unsigned* binN_V, const unsigned* hopSmpN_V, const fd_sample_t** magV=nullptr, const fd_sample_t** phsV=nullptr, const fd_sample_t** hzV=nullptr );
    fbuf_t*        fbuf_create( srate_t srate, unsigned chN, unsigned maxBinN, unsigned binN, unsigned hopSmpN, const fd_sample_t** magV=nullptr, const fd_sample_t** phsV=nullptr, const fd_sample_t** hzV=nullptr );
    void           fbuf_destroy( fbuf_t*& buf );
    fbuf_t*        fbuf_duplicate( const fbuf_t* src );

    mbuf_t*        mbuf_create( const midi::ch_msg_t* msgA=nullptr, unsigned msgN=0 );
    void           mbuf_destroy( mbuf_t*& buf );
    mbuf_t*        mbuf_duplicate( const mbuf_t* src );
    
    inline bool    value_is_abuf( const value_t* v ) { return v->flags & kABufTFl; }
    inline bool    value_is_fbuf( const value_t* v ) { return v->flags & kFBufTFl; }

    unsigned       value_type_label_to_flag( const char* type_desc );

    //------------------------------------------------------------------------------------------------------------------------
    //
    // Class and Variable Description
    //
    
    var_desc_t*    var_desc_find( class_desc_t* cd, const char* var_label );
    rc_t           var_desc_find( class_desc_t* cd, const char* label, var_desc_t*& vdRef );

    class_desc_t*  class_desc_find(  flow_t* p, const char* class_desc_label );
    
    void           class_dict_print( flow_t* p );

    
    //------------------------------------------------------------------------------------------------------------------------
    //
    // Network
    //
    void           network_print(const network_t& net );

    //------------------------------------------------------------------------------------------------------------------------
    //
    // Instance
    //
    
    instance_t*        instance_find( network_t& net, const char* inst_label, unsigned sfx_id );
    rc_t               instance_find( network_t& net, const char* inst_label, unsigned sfx_id, instance_t*& instPtrRef );
    
    external_device_t* external_device_find( flow_t* p, const char* device_label, unsigned typeId, unsigned inOrOutFl, const char* midiPortLabel=nullptr );

    void               instance_print( instance_t* inst );


    
    //------------------------------------------------------------------------------------------------------------------------
    //
    // Variable
    //

    // Create a variable but do not assign it a value.  Return a pointer to the new variable.
    // Note: `value_cfg` is optional. Set it to NULL to ignore
    rc_t           var_create( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, const object_t* value_cfg, variable_t*& varRef );

    // Channelizing creates a new var record with an explicit channel index to replace the
    // automatically generated variable whose channel index is set to  'all'.
    rc_t           var_channelize( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned chIdx, const object_t* value_cfg, unsigned vid, variable_t*& varRef );

    // `value_cfg` is optional. Set it to NULL to ignore
    rc_t           var_register( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, const object_t* value_cfg, variable_t*& varRef );

    // Returns true if this var is connected to an source proc variable
    bool           is_connected_to_source_proc( const variable_t* var );

    // Return true if this var is acting as a source for another var.
    bool           is_a_source_var( const variable_t* var );


    //-----------------
    //
    // var_register
    //
    
    inline rc_t _var_reg(cw::flow::instance_t*, unsigned int ) { return kOkRC; }
    
    template< typename T0, typename T1,  typename... ARGS >
    rc_t _var_reg( instance_t* inst, unsigned chIdx, T0 vid, T1 var_label, unsigned sfx_id, ARGS&&... args )
    {
      rc_t rc;
      variable_t* dummy = nullptr;
      if((rc = var_register( inst, var_label, sfx_id, vid, chIdx, nullptr, dummy )) == kOkRC )        
        if((rc = _var_reg( inst, chIdx, std::forward<ARGS>(args)...)) != kOkRC )
          return rc;
      return rc;
    }

    // Call var_register() on a list of variables.
    template< typename... ARGS >
    rc_t           var_register( instance_t* inst, unsigned chIdx, unsigned vid, const char* var_label, unsigned sfx_id, ARGS&&... args )
    {  return _var_reg( inst, chIdx, vid, var_label, sfx_id, std::forward<ARGS>(args)...); }



    //---------------------
    //
    // var_register_and_get
    //

    inline rc_t _var_register_and_get(cw::flow::instance_t*, unsigned int ) { return kOkRC; }

    template< typename T>
    rc_t var_register_and_get( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, T& valRef )
    {
      rc_t rc;
      variable_t* var;
      if((rc = var_register(inst,var_label,sfx_id,vid,chIdx,nullptr,var)) == kOkRC )
        rc = var_get(var,valRef);
      return rc;
    }
      
    
    inline rc_t _var_reg_and_get(cw::flow::instance_t*, unsigned int ) { return kOkRC; }

    template< typename T0, typename T1, typename T2, typename... ARGS >
    rc_t _var_reg_and_get( instance_t* inst, unsigned chIdx, T0 vid, T1 var_label, unsigned sfx_id, T2& valRef, ARGS&&... args )
    {
      rc_t rc;

      if((rc = var_register_and_get( inst, var_label, sfx_id, vid, chIdx, valRef )) == kOkRC )        
        if((rc = _var_reg_and_get( inst, chIdx, std::forward<ARGS>(args)...)) != kOkRC )
          return rc;
      
      return rc;
    }

    // Call var_register_and_get() on a list of variables.
    template< typename... ARGS >
    rc_t           var_register_and_get( instance_t* inst, unsigned chIdx, unsigned vid, const char* var_label, unsigned sfx_id,  ARGS&&... args )
    {  return _var_reg_and_get( inst, chIdx, vid, var_label, sfx_id, std::forward<ARGS>(args)...); }


    
    //---------------------
    //
    // var_register_and_set
    //
    
    // var_register_and_set().  If the variable has not yet been created then it is created and assigned a value.
    // If the variable has already been created then 'vid' and the value are updated.
    // (Note that abuf and fbuf values are not changed by this function only the 'vid' is updated.)
    rc_t           var_register_and_set( instance_t* inst, const char* label,  unsigned sfx_id,   unsigned vid, unsigned chIdx, variable_t*& varRef );
    
    rc_t           var_register_and_set( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, unsigned frameN );
    rc_t           var_register_and_set( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, midi::ch_msg_t* midiA, unsigned midiN );
    rc_t           var_register_and_set( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, const unsigned* maxBinN_V, const unsigned* binN_V, const unsigned* hopSmpN_V, const fd_sample_t** magV=nullptr, const fd_sample_t** phsV=nullptr, const fd_sample_t** hzV=nullptr );
    rc_t           var_register_and_set( instance_t* inst, const char* var_label, unsigned sfx_id, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, unsigned maxBinN, unsigned binN, unsigned hopSmpN, const fd_sample_t** magV=nullptr, const fd_sample_t** phsV=nullptr, const fd_sample_t** hzV=nullptr );

    inline rc_t _var_register_and_set(cw::flow::instance_t*, unsigned int ) { return kOkRC; }

    template< typename T0, typename T1, typename T2, typename... ARGS >
    rc_t _var_register_and_set( instance_t* inst, unsigned chIdx, T0 vid, T1 var_label, unsigned sfx_id, T2 val, ARGS&&... args )
    {
      rc_t rc;

      variable_t* var = nullptr;
      if((rc = var_register_and_set( inst, var_label, sfx_id, vid, chIdx, var)) == kOkRC )
      {
        if((rc = var_set( inst, vid, chIdx, val )) != kOkRC )
          return rc;
        
        if((rc = _var_register_and_set( inst, chIdx, std::forward<ARGS>(args)...)) != kOkRC )
          return rc;
      }
      
      return rc;
    }

    // Call var_register_and_set() on a list of variables.
    template< typename... ARGS >
    rc_t           var_register_and_set( instance_t* inst, unsigned chIdx, unsigned vid, const char* var_label, unsigned sfx_id, ARGS&&... args )
    {  return _var_register_and_set( inst, chIdx, vid, var_label, sfx_id, std::forward<ARGS>(args)...); }



    void           _var_destroy( variable_t* var );

    bool           var_exists(    instance_t* inst, const char* label, unsigned sfx_id, unsigned chIdx );
    bool           var_has_value( instance_t* inst, const char* label, unsigned sfx_id, unsigned chIdx );
    bool           var_is_a_source( instance_t* inst, const char* label, unsigned sfx_id, unsigned chIdx );
    bool           var_is_a_source( instance_t* inst, unsigned vid, unsigned chIdx );

    rc_t           var_find(   instance_t* inst, const char* var_label, unsigned sfx_id, unsigned chIdx, const variable_t*& varRef );
    rc_t           var_find(   instance_t* inst, const char* var_label, unsigned sfx_id, unsigned chIdx,       variable_t*& varRef );
    rc_t           var_find(   instance_t* inst, unsigned vid,                           unsigned chIdx,       variable_t*& varRef );

    
    // Count of numbered channels - does not count the kAnyChIdx variable instance.
    rc_t           var_channel_count( instance_t* inst, const char* label, unsigned sfx_idx, unsigned& chCntRef );
    rc_t           var_channel_count( const variable_t* var, unsigned& chCntRef );
    
    
    rc_t           var_get( const variable_t* var, bool&          valRef );
    rc_t           var_get( const variable_t* var, uint_t&        valRef );
    rc_t           var_get( const variable_t* var, int_t&         valRef );
    rc_t           var_get( const variable_t* var, float&         valRef );
    rc_t           var_get( const variable_t* var, double&        valRef );
    rc_t           var_get( const variable_t* var, const char*&   valRef );    
    rc_t           var_get( const variable_t* var, const abuf_t*& valRef );    
    rc_t           var_get(       variable_t* var, abuf_t*&       valRef );    
    rc_t           var_get( const variable_t* var, const fbuf_t*& valRef );
    rc_t           var_get(       variable_t* var, fbuf_t*&       valRef );
    rc_t           var_get( const variable_t* var, const mbuf_t*& valRef );
    rc_t           var_get(       variable_t* var, mbuf_t*&       valRef );

    template< typename T>
    rc_t var_get( instance_t* inst, unsigned vid, unsigned chIdx, T& valRef)
    {
      rc_t        rc  = kOkRC;
      variable_t* var = nullptr;
      
      if((rc = var_find(inst, vid, chIdx, var )) == kOkRC )
        rc = var_get(var,valRef);
      
      return rc;  
    }

    template< typename T >
    T val_get( instance_t* inst, unsigned vid, unsigned chIdx )
    {
      T value;
      var_get(inst,vid,chIdx,value);
      return value;
    }

    rc_t           var_set( instance_t* inst, unsigned vid, unsigned chIdx, bool val );
    rc_t           var_set( instance_t* inst, unsigned vid, unsigned chIdx, uint_t val );
    rc_t           var_set( instance_t* inst, unsigned vid, unsigned chIdx, int_t val );
    rc_t           var_set( instance_t* inst, unsigned vid, unsigned chIdx, float val );
    rc_t           var_set( instance_t* inst, unsigned vid, unsigned chIdx, double val );
    rc_t           var_set( instance_t* inst, unsigned vid, unsigned chIdx, const char* val );
    rc_t           var_set( instance_t* inst, unsigned vid, unsigned chIdx, abuf_t* val );
    rc_t           var_set( instance_t* inst, unsigned vid, unsigned chIdx, fbuf_t* val );

    const preset_t* class_preset_find( class_desc_t* cd, const char* preset_label );
    
  }
}
