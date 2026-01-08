#ifndef cwFlowValue_h
#define cwFlowValue_h

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
      sample_t*          buf;          // buf[ chN * frameN ] ch0: 0:frameN, ch1: frameN:2*frame, ...
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
      kRBufTFl     = 0x00002000,
      kStringTFl   = 0x00004000,
      kCfgTFl      = 0x00010000,
      kMidiTFl     = 0x00020000,

      kTypeMask    = 0x0003ffff,

      kRuntimeTFl  = 0x80000000,  // The type of the value associated with this variable will be set by the proc instances during instantiation

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

    struct recd_type_str;
    struct recd_str;
    typedef struct rbuf_str
    {
      const struct recd_type_str* type;     // all msgs are formed from this type      
      const struct recd_str*      recdA;    // recdA[ recdN ] 
      unsigned                    recdN;    //
      unsigned                    maxRecdN; // largest possible value of recdN for the life of the network.
    } rbuf_t;

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
        rbuf_t*         rbuf;
        
        char*           s;
        
        const object_t* cfg;
        midi::ch_msg_t* midi;
        void*           p;
        

      } u;
      
      struct value_str* link;
      
    } value_t;

    

    //------------------------------------------------------------------------------------------------------------------------
    //
    // Value Only
    //

    
    inline void set_null( value_t& v, unsigned tflag ) { v.tflag=tflag; v.u.p=nullptr; }
    inline bool is_numeric( const value_t* v ) { return cwIsFlag(v->tflag,kNumericTFl); }
    inline bool is_matrix(  const value_t* v ) { return cwIsFlag(v->tflag,kMtxTFl); }    

    // if all of the src flags are set in the dst flags then the two types are convertable.
    inline bool can_convert( unsigned src_tflag, unsigned dst_tflag ) { return (src_tflag&dst_tflag)==src_tflag; }

    enum { kSilentValPrintVerb,
           kMinimalValPrintVerb,
           kSummaryValPrintVerb,
           kAllValPrintVerb,
           kMaxValPrintVerb=kAllValPrintVerb,
           kInvalidValPrintVerb
    };
    
    unsigned       value_print_verbosity_from_string( const char* s );
    const char*    value_print_verbosity_to_string( unsigned verbosity );

    
    abuf_t*         abuf_create( srate_t srate, unsigned chN, unsigned frameN );
    void            abuf_destroy( abuf_t*& buf );
    void            abuf_print( const abuf_t* abuf, unsigned verbosity );
    
    // If 'dst' is null then a new abuf is allocated, filled with the contents of 'src'.
    // If 'dst' is non-null and there is enough space for the contents of 'src' then only a copy is executed.
    // If there is not enough space then dst is reallocated.
    abuf_t*         abuf_duplicate( abuf_t* dst, const abuf_t* src );
    void            abuf_zero(        abuf_t* buf );
    rc_t            abuf_set_channel( abuf_t* buf, unsigned chIdx, const sample_t* v, unsigned vN );
    const sample_t* abuf_get_channel( abuf_t* buf, unsigned chIdx );

    fbuf_t*        fbuf_create( srate_t srate, unsigned chN, const unsigned* maxBinN_V, const unsigned* binN_V, const unsigned* hopSmpN_V, const fd_sample_t** magV=nullptr, const fd_sample_t** phsV=nullptr, const fd_sample_t** hzV=nullptr );
    fbuf_t*        fbuf_create( srate_t srate, unsigned chN, unsigned maxBinN, unsigned binN, unsigned hopSmpN, const fd_sample_t** magV=nullptr, const fd_sample_t** phsV=nullptr, const fd_sample_t** hzV=nullptr );
    void           fbuf_zero( fbuf_t* fbuf );
    void           fbuf_destroy( fbuf_t*& buf );
    void           fbuf_print( const fbuf_t* fbuf, unsigned verbosity );

    // Memory allocation will only occur if dst is null, or the size of dst's internal buffer are too small.
    fbuf_t*        fbuf_duplicate( fbuf_t* dst, const fbuf_t* src );

    mbuf_t*        mbuf_create( const midi::ch_msg_t* msgA=nullptr, unsigned msgN=0 );
    void           mbuf_destroy( mbuf_t*& buf );
    mbuf_t*        mbuf_duplicate( const mbuf_t* src );
    void           mbuf_print( const mbuf_t* mbuf, unsigned verbosity );

    rbuf_t*        rbuf_create( const struct recd_type_str* type=nullptr, const struct recd_str* recdA=nullptr, unsigned recdN=0, unsigned maxRecdN=0 );
    void           rbuf_destroy( rbuf_t*& buf );
    rbuf_t*        rbuf_duplicate( const rbuf_t* src );
    void           rbuf_setup( rbuf_t* rbuf, struct recd_type_str* type, struct recd_str* recdA, unsigned recdN, unsigned maxRecdN );
    void           rbuf_print( const rbuf_t* rbuf, unsigned verbosity );

    
    inline bool    value_is_abuf( const value_t* v ) { return v->tflag & kABufTFl; }
    inline bool    value_is_fbuf( const value_t* v ) { return v->tflag & kFBufTFl; }

    unsigned       value_type_label_to_flag( const char* type_desc );
    const char*    value_type_flag_to_label( unsigned flag );
    inline const char*    value_to_type_label( const value_t* v ) { return value_type_flag_to_label(v->tflag); }

    void           value_release( value_t* v );
    void           value_duplicate( value_t& dst, const value_t& src );

    // For all numeric types this function set's the type of 'value_ref' to the type of cfg.
    // For all other types sets the type of 'value_ref' to kCfgTFl and stores 'cfg' to value_ref.u.cfg;
    rc_t           value_from_cfg( const object_t* cfg, value_t& value_ref );

    // Assigns src to dst. If dst has a value type then src is converted to this type.
    // If the conversion is not possible then the function fail.s
    rc_t           value_from_value( const value_t& src, value_t& dst );

    // Print the value to the log.
    void           value_print( const value_t* value, bool print_type_label_fl=false, unsigned verbosity=kMinimalValPrintVerb );

    // Buffer values (rbuf,mbuf,abuf,rbuf,cfg) support the notion of containing 0 or more elements.
    // Other types do not support this  (int,uint,float,string).
    bool           value_supports_an_ele_count( const value_t* value );

    // Returns true if the value supports the concept of containing elements and currently has a non-zero element count.
    bool           value_has_elements_now(  const value_t* value );

    // Returns true if the this value type is can 'notify()' the owning 'proc' when it changes.
    bool           value_can_auto_notify( const value_t* value );

    rc_t value_get( const value_t* val, bool& valRef );
    rc_t value_set(       value_t* val, bool v );
    
    rc_t value_get( const value_t* val, uint_t& valRef );
    rc_t value_set(       value_t* val, uint_t v );
    
    rc_t value_get( const value_t* val, int_t& valRef );
    rc_t value_set(       value_t* val, int_t v );
    
    rc_t value_get( const value_t* val, float& valRef );
    rc_t value_set(       value_t* val, float v );
    
    rc_t value_get( const value_t* val, double& valRef );
    rc_t value_set(       value_t* val, double v );
    
    rc_t value_get( const value_t* val, const char*& valRef );
    rc_t value_set(       value_t* val, const char* v );
    
    rc_t value_get(       value_t* val, abuf_t*& valRef );
    rc_t value_get(       value_t* val, const abuf_t*& valRef );
    rc_t value_set(       value_t* val, abuf_t* v );
    
    rc_t value_get(       value_t* val, fbuf_t*& valRef );
    rc_t value_get(       value_t* val, const fbuf_t*& valRef );
    rc_t value_set(       value_t* val, fbuf_t* v );
    
    rc_t value_get(       value_t* val, mbuf_t*& valRef );
    rc_t value_get(       value_t* val, const mbuf_t*& valRef );
    rc_t value_set(       value_t* val, mbuf_t* v );
    
    rc_t value_get(       value_t* val, rbuf_t*& valRef );
    rc_t value_get(       value_t* val, const rbuf_t*& valRef );
    rc_t value_set(       value_t* val, rbuf_t* v );
    
    rc_t value_get(       value_t* val, const object_t*& valRef );
    rc_t value_set(       value_t* val, const object_t* v );

    rc_t value_get( const value_t* val, midi::ch_msg_t*& valRef );
    rc_t value_get( const value_t* val, const midi::ch_msg_t*& valRef );
    rc_t value_set(       value_t* val, midi::ch_msg_t* v );



    //------------------------------------------------------------------------------------------------------------------------
    //
    // Record
    //
    
    typedef struct recd_field_str
    {
      bool        group_fl; // set if this field record is a group
      char*       label;    // field or group label
      value_t     value;    // default value for this field
      char*       doc;      // documentation field for this field
      union
      {
        unsigned               index; // index into recd_t.valA of the value associated with this field
        struct recd_field_str* group_fieldL; 
      } u;
        
      struct recd_field_str* link;
    } recd_field_t;

    typedef struct recd_type_str
    {
      recd_field_t*               fieldL;  // linked list of field spec's
      unsigned                    fieldN;  // length of fieldL list   (fieldN + base->fieldN) is total field count
      const struct recd_type_str* base;    // base recd type that this field inherits from
    } recd_type_t;

    // Record format  represents the 'cfg' data structure commonly
    // used to specify record types.  
    typedef struct recd_fmt_str
    {
      unsigned        alloc_cnt;  // count of records to pre-allocate
      const object_t* req_fieldL; // label of required fields
      recd_type_t*    recd_type;  // record type for this variable
    } recd_fmt_t;

    typedef struct recd_array_str
    {
      recd_type_t*     type;       // recd_type_t of this record array
      value_t*         valA;       // valA[ allocRecdN * type->fieldN ]
      struct recd_str* recdA;      // recdA[ allocRecdN ]
      unsigned         allocRecdN; //
      unsigned         recdN; 
    } recd_array_t;

    typedef struct recd_str
    {
      struct value_str*       valA;   // varA[ recd_type_t.fieldN ] array of field values
      const struct recd_str*  base;   // Pointer to the records inherited fields.
    } recd_t;


    // Create/destroy a recd_format_t object.
    // Cfg Syntax:
    // { alloc_cnt:<>, required:[ 'fieldname' ], fields:{ <field_label>:{ "type":<>, "value":<>, "doc":<> } } }
    // Note: dflt_alloc_cnt  is overridden by the 'alloc_cnt' field in 'cfg' if it exists.
    rc_t recd_format_create( recd_fmt_t*& recd_fmt_ref, const object_t* cfg, unsigned dflt_alloc_cnt=32 );
    void recd_format_destroy( recd_fmt_t*& recd_fmt_ref );

    // Create a recd_type_t instance from a cfg. description.
    // Note that if 'cfg' is null then this type will have only fields specified by 'base_type'
    // The format of the cfg is the same as that used by recd_format_create() however only the
    // 'fields' list is used (e.g. { fields:{ ... }} ).
    rc_t recd_type_create( recd_type_t*& recd_type_ref, const recd_type_t* base_type, const object_t* cfg );
    void recd_type_destroy( recd_type_t*& recd_type );

    // Count of fields combined local and base record types. 
    rc_t recd_type_max_field_count( const recd_type_t* recd_type );

    // Get the field index associated with a named field.
    // Use '.' notation to separate groups from fields.
    // Note if this is a 'local' field then the high bit in the returned index will be set.    
    unsigned recd_type_field_index( const recd_type_t* recd_type, const char* field_label);

    // Given a field index return the field label.
    const char* recd_type_field_index_to_label( const recd_type_t* recd_type, unsigned field_idx );

    // Returns true if these two record types match on field name, default value type, and group.
    // Record types that are equivalent can safely exchange records without having to
    // reformat or rearrange the data in recd_t.valA[].
    bool recd_types_are_equivalent( const recd_type_t* rt0, const recd_type_t* rt1 );

    // Print the recd_type info. to the console.
    void recd_type_print( const recd_type_t* recd_type );

    
    // Set the record base pointer and the value of all fields with default values.
    rc_t recd_init( const recd_type_t* recd_type, const recd_t* base, recd_t* r );

    rc_t recd_get_value( const recd_type_t* type, const recd_t* recd, unsigned field_idx, value_t& val_ref );

    // Read the value from a single record field
    template< typename T >
    rc_t recd_get( const recd_type_t* type, const recd_t* recd, unsigned field_idx, T& val_ref )
    {
      if( field_idx < type->fieldN )
        return value_get( recd->valA + field_idx, val_ref );

      return recd_get( type->base, recd->base, field_idx - type->fieldN, val_ref );
    }

    inline rc_t _recd_get(const recd_type_t* recd_type, recd_t* r ) { return kOkRC; }
    
    template< typename T1, typename... ARGS >
    rc_t _recd_get( const recd_type_t* recd_type, const recd_t* recd, unsigned field_idx, T1& val, ARGS&&... args )
    {
      rc_t rc = kOkRC;
      
      if((rc = recd_get(recd_type,recd,field_idx,val)) != kOkRC )
        return rc;

      return _recd_get(recd_type,recd,std::forward<ARGS>(args)...);      
    }

    // Read the value of multiple record fields.
    template< typename T1, typename... ARGS >
    rc_t recd_get( const recd_type_t* recd_type, const recd_t* recd, unsigned field_idx, T1& val, ARGS&&... args )
    {
      return _recd_get(recd_type,recd,field_idx,val,args...);
    }

    // Set the base record pointer for a record with an inherited base
    inline rc_t recd_set_base( const recd_type_t* type, recd_t* recd, const recd_t* base )
    {
      // if we are setting base then the type must have a base type
      assert( (type->base == nullptr && base==nullptr) || (type->base!=nullptr && base!=nullptr) );
      
      recd->base = base;
      return kOkRC;
    }

    rc_t recd_set_value( const recd_type_t* type, const recd_t* base, recd_t* recd, unsigned field_idx, const value_t& val );

    template< typename T >
    rc_t recd_set( const recd_type_t* type, const recd_t* base, recd_t* recd, unsigned field_idx, const T& val )
    {
      if( field_idx >= type->fieldN )
        return cwLogError(kInvalidArgRC,"Only 'local' record value may be set.");
      
      // set the base of this record
      recd_set_base(type,recd,base);
      
      return value_set( recd->valA + field_idx, val );
    }

    inline rc_t _recd_set( const recd_type_t* recd_type, recd_t* recd ) { return kOkRC; }

    template< typename T1, typename... ARGS >
    rc_t _recd_set( const recd_type_t* recd_type, recd_t* recd, unsigned field_idx, const T1& val, ARGS&&... args )
    {
      rc_t rc = kOkRC;

      if( field_idx >= recd_type->fieldN )
        return cwLogError(kInvalidArgRC,"Fields in the inherited record may not be set.");
              
      if((rc = value_set( recd->valA + field_idx, val)) != kOkRC )
      {
        rc = cwLogError(rc,"Field set failed on '%s'.", cwStringNullGuard(recd_type_field_index_to_label( recd_type, field_idx )));
        goto errLabel;
      }
      
      return _recd_set(recd_type,recd,std::forward<ARGS>(args)...);

    errLabel:
      return rc;
    }

    // Set multiple fields of a record.
    template< typename T1, typename... ARGS >
    rc_t recd_set( const recd_type_t* recd_type, const recd_t* base, recd_t* recd, unsigned field_idx, const T1& val, ARGS&&... args )
    {
      rc_t rc = kOkRC;
      if((rc = recd_init( recd_type, base, recd )) != kOkRC )
        goto errLabel;
      

      if((rc = _recd_set(recd_type,recd,field_idx,val,args...)) != kOkRC )
        goto errLabel;

    errLabel:
      return rc;

    }

    // Print a record to the console.
    rc_t recd_print( const recd_type_t* recd_type, const recd_t* r );

    // Create/destroy a buffer of records.
    rc_t recd_array_create( recd_array_t*& recd_array_ref, const recd_type_t* recd_type, const recd_type_t* base,  unsigned allocRecdN, const object_t* data_cfg=nullptr );
    rc_t recd_array_destroy( recd_array_t*& recd_array_ref );

    // Data must be a list of dictionaries of the form:
    // [ { <field_name>:<value>, <field_name>:<value> } ]
    // where each dictionary represents a record and each pair in the dictionary is a field labe/value pair.
    //
    // Ex: [ { x:3, c:"blue"},{ x:4, c:"red"}, { x:7, c:"green"} ] 
    rc_t recd_array_append_from_cfg( recd_array_t* recd_array, const object_t* cfg );

    // Copy records into a recd_array.  This function fails if there are less than
    // 'src_recdN' records already allocated in 'dest_recd_array'.
    // The source and destination record types should be the same, but this
    // function does very little to verify that they actually are.
    //rc_t recd_copy( const recd_type_t* src_recd_type, const recd_t* src_recdA, unsigned src_recdN, recd_array_t* dst_recd_array, unsigned dst_recd_idx = 0 );


    //------------------------------------------------------------------------------------------------------------------------
    //
    // List
    //

    typedef struct list_ele_str
    {
      char*    label;
      value_t  value;
    } list_ele_t;
    
    typedef struct list_str
    {
      unsigned    tflag;  // all elements of the list share the same  value type.
      list_ele_t* eleA;
      unsigned    eleAllocN;
      unsigned    eleN;
      
    } list_t;

    
    // Cfg: [ <label0>, <label1> ... <labelN> ]    (value is the same as the element index)
    //      or
    //      { (<label0>:<value0> ... (<labelN>:<valueN>) }
    rc_t list_create( list_t*& list_ref, const object_t* cfg );
    rc_t list_create( list_t*& list_ref, unsigned count );

    rc_t list_destroy( list_t*& list_ref );

    rc_t list_append( list_t* list, const char* label, const value_t& value );

    
    
    template< typename T >
    rc_t list_append( list_t* list, const char* label, const T& v )
    {
      rc_t rc;      
      value_t value;
      value.tflag = kInvalidTFl;
      if((rc = value_set(&value,v)) != kOkRC )
        goto errLabel;

      if((rc = list_append(list,label,value)) != kOkRC )
        goto errLabel;

    errLabel:
      return rc;
    }

    const char* list_ele_label( const list_t* list, unsigned index );
    unsigned    list_ele_index( const list_t* list, const char* label );

    template< typename T >
    rc_t list_ele_value( const list_t* list, unsigned index, T& v )
    {
      rc_t rc = kOkRC;
      if( index >= list->eleN )
      {
        rc = cwLogError(rc,"The list element index '%i' is invalid on a list of length '%i'.",index,list->eleN);
        goto errLabel;
      }

      if((rc = value_get(&list->eleA[index].value,v)) != kOkRC )
      {
        rc = cwLogError(rc,"Read of list element value at index '%i' failed.",index,list->eleN);
        goto errLabel;        
      }

    errLabel:
      return rc;
    }
    
    
    //------------------------------------------------------------------------------------------------------------------------
    rc_t value_test( const test::test_args_t& args );
 
    
  }
}


#endif
