namespace cw
{
  namespace flow
  {

    typedef float    real_t;
    typedef real_t   sample_t;
    typedef real_t   srate_t;
    typedef unsigned uint_t;
    typedef int      int_t;

    typedef unsigned vid_t;

    typedef struct abuf_str
    {
      struct value_str*  base;
      srate_t            srate;   // signal sample rate
      unsigned           chN;     // count of channels
      unsigned           frameN;  // count of sample frames per channel
      sample_t*          buf;     // buf[ chN ][ frameN ]
    } abuf_t;


    enum {
      kFbufVectN = 3,
      kAnyChIdx = kInvalidIdx
    };
    
    typedef struct fbuf_str
    {      
      struct value_str* base;
      srate_t           srate;   // signal sample rate
      unsigned          flags;   // See kXXXFbufFl
      unsigned          chN;     // count of channels
      unsigned          binN;    // count of sample frames per channel
      unsigned          hopSmpN; // hop sample count 
      sample_t**        magV;    // magV[ chN ][ binN ]
      sample_t**        phsV;    // phsV[ chN ][ binN ]
      sample_t**        hzV;     // hzV[ chN ][ binN ]
      sample_t*         buf;     // memory used by this buffer (or NULL if magV,phsV,hzV point are proxied to another buffer)      
    } fbuf_t;

    enum
    {
      kInvalidTFl = 0x00000000,
      kBoolTFl    = 0x00000001,
      kUIntTFl    = 0x00000002,
      kIntTFl     = 0x00000004,
      kRealTFl    = 0x00000008,
      kF32TFl     = 0x00000010,
      kF64TFl     = 0x00000020,
      
      kBoolMtxTFl = 0x00000040,
      kUIntMtxTFl = 0x00000080,
      kIntMtxTFl  = 0x00000100,
      kRealMtxTFl = 0x00000200,
      kF32MtxTFl  = 0x00000400,
      kF64MtxTFl  = 0x00000800,
      
      kABufTFl    = 0x00001000,
      kFBufTFl    = 0x00002000,
      kStringTFl  = 0x00004000,
      kFNameTFl   = 0x00008000,
      kTimeTFl    = 0x00010000,

      kTypeMask   = 0x0001ffff,

    };

    typedef struct mtx_str
    {
      union {
        struct mtx::mtx_str< unsigned >* u;
        struct mtx::mtx_str< int >*      i;
        struct mtx::mtx_str< real_t >*   r;
        struct mtx::mtx_str< float >*    f;
        struct mtx::mtx_str< double >*   d;
      } u;
    } mtx_t;
    
    typedef struct value_str
    {
      unsigned flags;
      union {
        bool      b;
        uint_t    u;
        int_t     i;
        real_t    r;
        float     f;
        double    d;

        mtx_t*    mtx;
        
        abuf_t*   abuf;
        fbuf_t*   fbuf;
        
        char*     s;
        char*     fname;

        struct value_str* proxy;
      } u;
      
      struct value_str* link;
      
    } value_t;

    struct instance_str;
    struct variable_str;
    
    typedef struct ctx_str
    {
    } ctx_t;
    
    typedef rc_t (*member_func_t)( struct instance_str* ctx );
    typedef rc_t (*member_value_func_t)( struct instance_str* ctx, struct variable_str* var );
    enum
    {
      kSrcVarFl = 0x01
    };
    
    typedef struct class_members_str
    {
      member_func_t       create;
      member_func_t       destroy;  
      member_value_func_t value;
      member_func_t       exec;
    } class_members_t;
    
    typedef struct var_desc_str
    {
      const object_t*      cfg;     //
      const char*          label;   //
      unsigned             type;    // value type id
      unsigned             flags;   // 
      const char*          docText; //
      struct var_desc_str* link;    //
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
    } class_desc_t;

    
    typedef struct variable_str
    {
      struct instance_str* inst;         // pointer to this variables instance
      char*                label;        // this variables label
      unsigned             vid;          // this variables numeric id ( cat(vid,chIdx) forms a unique variable identifier on this 'inst'
      var_desc_t*          varDesc;      // the variable description for this variable
      value_t              local_value;  // the local value instance (actual value if this is not a 'src' variable)
      value_t*             value;        // pointer to the value associated with this variable   
      unsigned             chIdx;        // channel index
      struct variable_str* link;         // link to other var's on 'inst' 
      struct variable_str* connect_link; // list of outgoing connections
    } variable_t;

    
    typedef struct instance_str
    {
      struct flow_str* ctx;          // global system context

      class_desc_t*   class_desc;    //
      
      const char*     label;         // instance label
      const object_t* inst_cfg;      // instance configuration
      
      const char*     arg_label;     // optional args label
      const object_t* arg_cfg;       // optional args configuration
      const char*     preset_label;  // optional preset label

      void*           userPtr;       // instance state

      variable_t*     varL;          // list of instance  value

      unsigned        varMapChN;     // max count of channels among all variables
      unsigned        varMapIdN;
      unsigned        varMapN;       // varMapN
      variable_t**    varMapA;       // varMapA[ varMapN ]
      
      struct instance_str* link;
    } instance_t;    

    typedef struct flow_str
    {
      const object_t*      cfg;
      
      unsigned             framesPerCycle; // sample frames per cycle (64)
      unsigned             cycleIndex;     // Incremented with each processing cycle      
      unsigned             maxCycleCount;  // count of cycles to run on flow::exec() or 0 if there is no limit.
      
      class_desc_t*        classDescA;     // 
      unsigned             classDescN;     //
      
      struct instance_str* network_head; // first instance
      struct instance_str* network_tail; // last insance      
    } flow_t;

    //------------------------------------------------------------------------------------------------------------------------
    //
    // Value Only
    //
    
    abuf_t*         abuf_create( srate_t srate, unsigned chN, unsigned frameN );
    void            abuf_destroy( abuf_t* buf );
    rc_t            abuf_set_channel( abuf_t* buf, unsigned chIdx, const sample_t* v, unsigned vN );
    const sample_t* abuf_get_channel( abuf_t* buf, unsigned chIdx );

    fbuf_t*        fbuf_create( srate_t srate, unsigned chN, unsigned binN, unsigned hopSmpN, const sample_t** magV=nullptr, const sample_t** phsV=nullptr, const sample_t** hzV=nullptr );
    void           fbuf_destroy( fbuf_t* buf );
    
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
    void           class_desc_print( flow_t* p );
    void           network_print(    flow_t* p );

    //------------------------------------------------------------------------------------------------------------------------
    //
    // Instance
    //
    
    instance_t*    instance_find( flow_t* p, const char* inst_label );
    rc_t           instance_find( flow_t* p, const char* inst_label, instance_t*& instPtrRef );
    void           instance_print( instance_t* inst );

    //------------------------------------------------------------------------------------------------------------------------
    //
    // Variable
    //

    // Create a variable but do not assign it a value.  Return a pointer to the new variable.
    rc_t           var_create( instance_t* inst, const char* label, unsigned vid, unsigned chIdx, variable_t*& varRef );

    // var_init().  If the variable has not yet been created then it is created and assigned a value.
    // If the variable has already been created then 'vid' and the value are updated.
    // (Note that abuf and fbuf values are not changed by this function only the 'vid' is updated.)
    rc_t           var_init( instance_t* inst, const char* label,     unsigned vid, unsigned chIdx, variable_t*& varRef );
    rc_t           var_init( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, unsigned      value );
    rc_t           var_init( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, int           value );
    rc_t           var_init( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, real_t        value );
    rc_t           var_init( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, const abuf_t* abuf );
    rc_t           var_init( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, const fbuf_t* fbuf );

    inline rc_t _var_init(cw::flow::instance_t* inst, unsigned int ) { return kOkRC; }

    template< typename T0, typename T1, typename T2, typename... ARGS >
    rc_t _var_init( instance_t* inst, unsigned chIdx, T0 vid, T1 var_label, T2 val, ARGS&&... args )
    {
      rc_t rc;

      if((rc = var_init( inst, var_label, vid, chIdx, val )) == kOkRC )        
        rc = _var_init( inst, chIdx, std::forward<ARGS>(args)...);
      return rc;
    }

    // Call var_init() on a list of variables.
    template< typename... ARGS >
    rc_t           var_init( instance_t* inst, unsigned chIdx, unsigned vid, const char* var_label, ARGS&&... args )
    {  return _var_init( inst, chIdx, vid, var_label, std::forward<ARGS>(args)...); }
    
    void           _var_destroy( variable_t* var );

    bool           var_exists( instance_t* inst, const char* label, unsigned chIdx );
    
    rc_t           var_get(      instance_t* inst, const char* var_label, unsigned chIdx,       variable_t*& vRef );
    rc_t           var_get(      instance_t* inst, const char* var_label, unsigned chIdx, const variable_t*& vRef );
    
    rc_t           value_get(    instance_t* inst, const char* label, unsigned chIdx,       value_t*& vRef );
    rc_t           value_get(    instance_t* inst, const char* label, unsigned chIdx, const value_t*& vRef );

    rc_t           var_abuf_create( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, unsigned frameN );
    rc_t           var_fbuf_create( instance_t* inst, const char* var_label, unsigned vid, unsigned chIdx, srate_t srate, unsigned chN, unsigned binN, unsigned hopSmpN, const sample_t** magV=nullptr, const sample_t** phsV=nullptr, const sample_t** hzV=nullptr );
    
    rc_t           var_abuf_get( instance_t* inst, const char* var_label, unsigned chIdx,       abuf_t*& abufRef );
    rc_t           var_abuf_get( instance_t* inst, const char* var_label, unsigned chIdx, const abuf_t*& abufRef );
    rc_t           var_fbuf_get( instance_t* inst, const char* var_label, unsigned chIdx,       fbuf_t*& fbufRef );
    rc_t           var_fbuf_get( instance_t* inst, const char* var_label, unsigned chIdx, const fbuf_t*& fbufRef );

   
    rc_t           var_map_id_to_index(     instance_t* inst, unsigned vid, unsigned chIdx, unsigned& idxRef );
    rc_t           var_map_label_to_index(  instance_t* inst, const char* var_label, unsigned chIdx, unsigned& idxRef );

    rc_t           var_get( instance_t* inst, unsigned vid, unsigned chIdx, uint_t& valRef );
    rc_t           var_get( instance_t* inst, unsigned vid, unsigned chIdx, int_t& valRef );
    rc_t           var_get( instance_t* inst, unsigned vid, unsigned chIdx, real_t& valRef );
    rc_t           var_get( instance_t* inst, unsigned vid, unsigned chIdx, abuf_t*& valRef );    
    rc_t           var_get( instance_t* inst, unsigned vid, unsigned chIdx, fbuf_t*& valRef );

    rc_t           var_set( instance_t* inst, unsigned vid, unsigned chIdx, uint_t val );
    rc_t           var_set( instance_t* inst, unsigned vid, unsigned chIdx, int_t val );
    rc_t           var_set( instance_t* inst, unsigned vid, unsigned chIdx, real_t val );

    rc_t           var_set( instance_t* inst, const char* var_label, unsigned chIdx, uint_t val );
    rc_t           var_set( instance_t* inst, const char* var_label, unsigned chIdx, int_t val );
    rc_t           var_set( instance_t* inst, const char* var_label, unsigned chIdx, real_t val );
    
    rc_t  apply_preset( instance_t* inst, const char* preset_label );
    
  }
}
