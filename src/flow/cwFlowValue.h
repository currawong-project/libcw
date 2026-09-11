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
        //void*           p;
        

      } u;
      
      struct value_str* link;
      
    } value_t;

    

    //------------------------------------------------------------------------------------------------------------------------
    //
    // Value Only
    //

    
    //inline void set_null( value_t& v, unsigned tflag ) { v.tflag=tflag; v.u.p=nullptr; }
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

    rc_t value_get( const value_t* val, value_t& valRef );
    rc_t value_get(       value_t* val, value_t& valRef );
    rc_t value_set(       value_t* val, const value_t& valRef );


    //------------------------------------------------------------------------------------------------------------------------
    //
    // Record
    //
    
    typedef struct recd_field_str
    {
      char*       label;    // field label
      unsigned    uid;      // unique id from the global id table
      value_t     value;    // default value for this field
      char*       doc;      // documentation field for this field
      unsigned    val_idx;  // index into recd_t.valA of the value associated with this field
      unsigned    src_uid;  // 
        
      struct recd_field_str* link;  // link for recd_type_t.fieldL linked list
    } recd_field_t;


    typedef struct field_map_str
    {
      const recd_field_t* field_desc; // Field desc this map represents.
      unsigned            level_idx;  // Count of levels in the recd_t which must be traversed to get to the value base array
    } recd_field_map_t;
    
    typedef struct recd_type_str
    {
      recd_field_t*               fieldL;      // linked list of field spec's for this type (excluding the base type)
      unsigned                    fieldN;      // length of fieldL list   
      const struct recd_type_str* base;        // base recd type that this field inherits from
      recd_field_map_t*           fieldMapA;   // Maps from field indexes to field value for all fields including the base fields.
      unsigned                    fieldMapN;   // Count of records in fieldN and all fields in all bases.
      bool                        read_only_fl;// This record is read only
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
      const recd_type_t* type;       // The type of all records in the array are logically equivalent to this type but may have different physical layouts.
                                     // This type may therefore be used to determine the label to field index mapping for all records in the array.
      recd_type_t*       _type;      // Internally allocated record type. Always has the same value as 'type' or is null.
      value_t*           valA;       // valA[ allocRecdN * type->fieldN ] value memory for all fields defined in type.
      struct recd_str*   recdA;      // recdA[ allocRecdN ]
      unsigned           allocRecdN; // Allocated size of recdA[]
      unsigned           recdN;      // Current count of records in use within recdA[].
    } recd_array_t;

    typedef struct recd_str
    {
      const recd_type_t*      type;   // Type of this record
      struct value_str*       valA;   // varA[ recd_type_t.fieldN ] array of field values
      const struct recd_str*  base;   // Pointer to the records inherited fields.
    } recd_t;


    // Create/destroy a recd_format_t object.
    // Cfg Syntax:
    // { alloc_cnt:<>, required:[ 'fieldname' ], fields:{ <field_label>:{ "type":<>, "value":<>, "doc":<> } } }
    // Note: dflt_alloc_cnt  is overridden by the 'alloc_cnt' field in 'cfg' if it exists.
    rc_t recd_format_create( recd_fmt_t*& recd_fmt_ref, const object_t* cfg, unsigned dflt_alloc_cnt=32, const recd_type_t* base_type=nullptr );
    void recd_format_destroy( recd_fmt_t*& recd_fmt_ref );

    // Create a recd_type_t instance from a cfg. description.
    // Note that if 'cfg' is null then this type will have only fields specified by 'base_type'
    // The format of the cfg is the same as that used by recd_format_create() however only the
    // 'fields' list is used (e.g. { fields:{ ... }} ).
    // A default field map is instantiated that includes all fields including the base fields.
    rc_t recd_type_create(  recd_type_t*& recd_type_ref, const recd_type_t* base_type, const object_t* cfg );

    // 'field_map_cfg' is a dictionary of input:output field labels.
    // The input fields must exist in 'base_type'.
    // If the output fields do not exist in the base type then it is assumed that the field is being renamed.
    // In this case the input field aliased with the new name.
    // The order of the output fields defines the logical layout of the output record.
    rc_t recd_type_create_from_map(  recd_type_t*& recd_type_ref, const recd_type_t* base_type, const object_t* field_map_cfg );
    void recd_type_destroy( recd_type_t*& recd_type_ref );

    rc_t recd_type_apply_map( recd_type_t* recd_type, const char* const * labelA, unsigned labelN );
    rc_t recd_type_apply_map( recd_type_t* recd_type, const object_t* map_list_cfg );

    recd_field_map_t* recd_type_field_label_to_map( const recd_type_t* recd_type, const char* field_label );

    // Count of fields combined local and base record types. 
    rc_t recd_type_max_field_count( const recd_type_t* recd_type );

    // Get the field index associated with a named field.
    // Use '.' notation to separate groups from fields.
    // Note if this is a 'local' field then the high bit in the returned index will be set.    
    unsigned recd_type_field_index( const recd_type_t* recd_type, const char* field_label );
    rc_t     recd_type_field_index( const recd_type_t* recd_type, const char* field_label, unsigned& field_idx_ref );

    // Same as recd_type_field_index() except does not report an error if the field is not found.
    unsigned recd_type_field_index_silent( const recd_type_t* recd_type, const char* field_label );

    inline rc_t recd_type_to_field_index( const recd_type_t* ) { return kOkRC; }
    
    template < typename... ARGS >
    rc_t recd_type_to_field_index(const recd_type_t* recd_type, const char* field_label, unsigned& index_ref, ARGS&&... args)
    {
      rc_t rc = kOkRC;
      
      if((rc = recd_type_field_index(recd_type,field_label,index_ref)) != kOkRC )
        return rc;

      return recd_type_to_field_index(recd_type,std::forward<ARGS>(args)...);            
    }
    
    // Given a field index return the field label.
    const char* recd_type_field_index_to_label( const recd_type_t* recd_type, unsigned field_idx );

    // Returns true if both fields have same data type.
    bool recd_fields_data_types_are_equivalent( const recd_field_t* fd0, const recd_field_t* fd1 );
    
    // Compare two record types for 'logical' equivalence.
    // Returns true if these two record types match on field name, default value type, and group.
    // Record types that are logically equivalent can safely exchange records without having to
    // reformat or rearrange the data in recd_t.valA[].
    // Note that logical equivalence is not 'physical' equivalence a record CANNOT be decoded
    // with a logically equivalent type record.  Logical equivalence only guaraentees
    // that the same field index will return a value from a same named/typed field.
    // The recd_type_t which describes the record however must be used to set or decode
    // the contents of the record.
    bool recd_types_are_equivalent( const recd_type_t* rt0, const recd_type_t* rt1 );

    // Print the recd_type info. to the console.
    void recd_type_print( const recd_type_t* recd_type );
    void recd_type_print( const recd_t* recd );
    
    // Set the record base pointer and the value of all fields with default values.
    rc_t recd_init( const recd_type_t* recd_type, const recd_t* base, recd_t* r );



    template< typename T >
    rc_t recd_get_from_uid( const recd_t* r,  unsigned uid, T& val_ref )
    {
      if( r == nullptr )
        return cwLogError(kEleNotFoundRC,"The field associated with uid:%i '%s' was not found.",uid,cwStringNullGuard(id_table::get_label(uid)));

      // for each field in this data record
      for(const recd_field_t* f = r->type->fieldL; f!=nullptr; f=f->link)
      {
        // if this is the target 'uid' 
        if( f->uid == uid )
        {
          // if this field does not contain a link to another field ...
          if( f->src_uid == kInvalidIdx )
          {
            // verify that the record is not corrupted
            if( r->valA == nullptr )
              return cwLogError(kInvalidStateRC,"No data is available for the field:'%s'.",cwStringNullGuard(f->label));

            // return the field value
            return value_get( r->valA + f->val_idx, val_ref );
          }

          // ... this record is a link to another field - the other field must be in the base.
          // (since the only way to make a link is via the 'reformat' option)
          return recd_get_from_uid(r->base,f->src_uid,val_ref);
            
        }
      }
      
      return recd_get_from_uid( r->base, uid, val_ref);
    }
                         
    template< typename T >
    rc_t recd_get( const recd_t* recd, unsigned field_idx, T& val_ref )
    {
      rc_t          rc = kOkRC;
      
      assert( field_idx < recd->type->fieldMapN );
      
      const recd_field_t* rf = recd->type->fieldMapA[ field_idx ].field_desc;
      
      return recd_get_from_uid(recd,rf->uid,val_ref);
    }
    
    inline rc_t _recd_get( const recd_t* r ) { return kOkRC; }
    
    template< typename T1, typename... ARGS >
    rc_t _recd_get( const recd_t* recd, unsigned field_idx, T1& val, ARGS&&... args )
    {
      rc_t rc = kOkRC;
      
      if((rc = recd_get(recd,field_idx,val)) != kOkRC )
        return rc;

      return _recd_get(recd,std::forward<ARGS>(args)...);      
    }

    // Read the value of multiple record fields.
    template< typename T1, typename... ARGS >
    rc_t recd_get( const recd_t* recd, unsigned field_idx, T1& val, ARGS&&... args )
    {
      return _recd_get(recd,field_idx,val,args...);
    }

    // Set the base record pointer for a record with an inherited base
    inline rc_t recd_set_base( recd_t* recd, const recd_t* base )
    {
      // if we are setting base then the type must have a base type
      assert( (recd->type->base == nullptr && base==nullptr) || (recd->type->base!=nullptr && base!=nullptr) );
      
      recd->base = base;
      return kOkRC;
    }
   
    rc_t recd_set_value( const recd_t* base, recd_t* recd, unsigned field_idx, const value_t& val );

    inline rc_t _recd_set( const recd_type_t* type, recd_t* recd ) { return kOkRC; }

    template< typename T1, typename... ARGS >
    rc_t _recd_set( const recd_type_t* type, recd_t* recd, unsigned field_idx, const T1& val, ARGS&&... args )
    {
      rc_t rc = kOkRC;

      if( field_idx >= recd->type->fieldN )
        return cwLogError(kInvalidArgRC,"Fields in the inherited record may not be set.");

      //printf("set: %p %i %s\n",recd->valA, field_idx, recd_type_field_index_to_label( recd->type, field_idx ) );

      
      if((rc = value_set( recd->valA + field_idx, val)) != kOkRC )
      {
        rc = cwLogError(rc,"Field set failed on '%s'.", cwStringNullGuard(recd_type_field_index_to_label( recd->type, field_idx )));
        goto errLabel;
      }
      
      return _recd_set(type, recd,std::forward<ARGS>(args)...);

    errLabel:
      return rc;
    }

    // Set multiple fields of a record.
    template< typename T1, typename... ARGS >
    rc_t recd_set( const recd_type_t* type, const recd_t* base, recd_t* recd, unsigned field_idx, const T1& val, ARGS&&... args )
    {
      rc_t rc = kOkRC;
      
      if( type->read_only_fl )
      {
        rc = cwLogError(kInvalidStateRC,"Attempt to write to a read-only record.");
        goto errLabel;
      }
          
      if((rc = recd_init( type, base, recd )) != kOkRC )
        goto errLabel;
      
      if((rc = _recd_set(type,recd,field_idx,val,args...)) != kOkRC )
        goto errLabel;

    errLabel:
      return rc;

    }

    // Print a record to the console.
    void recd_print( const recd_t* r );

    // Create/destroy a buffer of records.
    // Count of acutal records allocated is max(data_cfg->alloc_cnt,allocRecdN);
    // The recd_type is not cloned and is expected to exist for the life of the recd_array.
    rc_t recd_array_create( recd_array_t*&     recd_array_ref,
                            const recd_type_t* recd_type,
                            unsigned           allocRecdN,
                            const object_t*    data_cfg = nullptr );

    // This version of recd_array_create() creates the record type internally from 'fmt_cfg' and 'base'.
    rc_t recd_array_create( recd_array_t*&     recd_array_ref,
                            const recd_type_t* base,
                            const object_t*    fmt_cfg,
                            unsigned           allocRecdN,
                            const object_t*    data_cfg = nullptr );
    
    rc_t recd_array_destroy( recd_array_t*& recd_array_ref );

    // Data must be a list of dictionaries of the form:
    // [ { <field_name>:<value>, <field_name>:<value> } ]
    // where each dictionary represents a record and each pair in the dictionary is a field labe/value pair.
    //
    // Ex: [ { x:3, c:"blue"},{ x:4, c:"red"}, { x:7, c:"green"} ] 
    rc_t recd_array_append_from_cfg( recd_array_t* recd_array, const object_t* cfg );

    // True if the internal type of this array is logically equivalent to 'type'.
    bool recd_array_is_type_equivalent( recd_array_t* recd_array, const recd_type_t* type );

    // The type of the records in recdA[recdN] must be logically equivalent to the type of dst_array.
    // This equivalance is asserted in the debug build, but not the release build.
    // The top level of dst_array->type must always be empty. (e.g. dst_array.type->fieldN == 0) because
    // this function works by setting the 'base' pointer of every output record to the incoming
    // record. This implies that the output record has no top level record.
    rc_t recd_array_concat( recd_array_t* dst_array, const recd_t* recdA, unsigned recdN );

    // Split the records in srcA[] into one of the destination arrays in dst_arrayAA[][] based
    // on the value of the the source field identified by 'src_fld_idx' or if src_fld_idx is invalid
    // then 'dflt_dst_idx;.  If 'src_fld_idx' is valid then the value of stored in the associated
    // field must be between 0 and dst_array_cnt-1.  Likewise if 'dflt_dst_idx' is valid then
    // it must be a value between 0 and dst_array_cnt-1.
    rc_t recd_array_split( const recd_t* srcA, unsigned srcRecdN, unsigned src_fld_idx, recd_array_t** dst_arrayAA, unsigned dst_array_cnt, unsigned dflt_dst_idx=kInvalidIdx );

    rc_t recd_array_remap( const recd_t* srcA, unsigned srcRecdN, recd_array_t* dst_recd_array );

    

    // Print the contents of the array.
    void recd_array_print( const recd_array_t* recd_array );

    


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

    rc_t list_clear( list_t* list );    
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
