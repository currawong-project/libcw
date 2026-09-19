
namespace cw
{
  namespace flow
  {
    enum {
      kDefaultAllocRecdCnt=1024
    };

    // Create/Destroy the global recd_type_t registry.
    void recd_registry_create();
    void recd_registry_destroy();
    
    typedef struct recd_field_desc_str
    {
      char*                             label;       // field label
      unsigned                          label_id;    // automatically generated unique id based on the label
      value_t                           _dflt_value; // _alias ? tflag=kInvalidTFl : default value for this field
      char*                             _doc;        // _alias ? nullptr           : documentation field for this field
      unsigned                          _val_idx;    // _alias ? kInvalidIdx       : index into recd_t.valA of the value associated with this field
      const struct recd_field_desc_str* _alias;      // Set if this is an alias field desc otherwise null.
      unsigned                          _alias_level_offset;  // Offset to alias level or 0 if _alias == nullptr;
    } recd_field_desc_t;

    void recd_field_desc_print( const recd_field_desc_t* f );
    
    typedef struct recd_type_str
    {
      unsigned                    class_id;   // Equivalent recd_types have the same class_id.
      recd_field_desc_t*          fieldDescA; // Array of field spec's for this level (excludes base_type) sorted on recd_desc_t.label_id 
      unsigned                    fieldDescN; // length of fieldL list   
      const struct recd_type_str* base_type;  // base recd type that this field inherits from
    } recd_type_t;


    // Create a recd_type_t instance from a cfg. description.
    // Note that if 'fieldD_cfg' is null then this type will have only fields specified by 'base_type'
    // The syntax of the 'fieldD_cfg' is {'fields':{ <field_label>:{ "type":<>, "value":<>, "doc":<> } } }
    // or for aliasing base fields:      {'fields':{ <base_target_field_label>:<alias_field_label> } }
    // Note that the the recd_type is either used to represent actual data fields or or used to rename (alias) base type fields
    // it cannot do both.
    rc_t recd_type_create(  recd_type_t*& recd_type_ref, const recd_type_t* base_type, const object_t* fieldD_cfg=nullptr );
    void recd_type_destroy( recd_type_t*& recd_type_ref );
    
    // Returns true if this is an aliasing type.
    bool recd_type_is_alias( const recd_type_t* rt );
    void recd_type_print( const recd_type_t* rt );
    
    const char* recd_type_field_index_to_label( const recd_type_t* rt, unsigned field_idx );


    // Record format  represents the 'cfg' data structure commonly
    // used to specify record types.  
    typedef struct recd_fmt_str
    {
      unsigned        alloc_cnt;  // count of records to pre-allocate
      const object_t* req_fieldL; // label of required fields
      const object_t* fieldD_cfg; // optional list of field s
    } recd_fmt_t;

    // Create/destroy a recd_format_t object.
    // Cfg Syntax:
    // { alloc_cnt:<>, required:[ 'fieldname' ], fields:{ <field_label>:{ "type":<>, "value":<>, "doc":<> } } }
    rc_t recd_format_create( recd_fmt_t*& fmt, const object_t* fmt_cfg, unsigned alloc_recdN=kDefaultAllocRecdCnt );
    void recd_format_destroy( recd_fmt_t*& fmt );
    
    typedef struct field_map_str
    {      
      const char*            field_label;       
      unsigned               label_id;
      unsigned               val_type_tflag;
    } recd_common_field_t;

    typedef struct recd_field_loc_str
    {
      const recd_common_field_t* com_field; // The name and type of the field this locator represents (informational only)
      unsigned                   level_cnt; // The recd_t base recursion depth to arrive at the data for this field.
      unsigned                   value_idx; // the value offset into recd_t.valA[] of this fields value data.
    } recd_field_loc_t;

    // Holds the field locations for each field in 'recd_type'.
    typedef struct recd_type_link_str
    {
      const recd_type_t*  recd_type;  // Type for all fields in fieldSpecA[]
      recd_field_loc_t*   fieldLocA;  // Locator spec's for each available field in this type. (fieldLocA[i].field == recd_array_t.comFieldA[i])
      unsigned            fieldLocN;  // Same as comFieldN for the owning recd_array_t
    } recd_type_link_t;
    
    typedef struct recd_array_str
    {
      recd_type_t**        typeA;      // typeA[typeN] All types used by records in this recd_array_t instance.
      unsigned             typeN;      //

      recd_type_link_t*    typeLinkA;  // linkA[typeN] 

      recd_common_field_t* comFieldA;  // comFieldA[comFieldN] Common fields across all types
      unsigned             comFieldN;

      unsigned*            baseMapA;   // baseMapA[baseMapN] translate base class_id to index into typeLinkA[]
      unsigned             baseMapN;

      value_t*             valA;       // valA[ allocRecdN * topLevelFieldN ] value memory for all fields defined in type.
      struct recd_str*     recdA;      // recdA[ allocRecdN ]
      unsigned             allocRecdN; // Allocated size of recdA[]
      unsigned             recdN;      // Current count of records in use within recdA[].
    } recd_array_t;


    typedef struct recd_str
    {
      const recd_type_link_t*  link; // Link to be used for decoding the data in this record
      struct value_str*        valA; // varA[ recd_type_t.fieldN ] array of field values
      const struct recd_str*   base; // Pointer to the records inherited fields.
    } recd_t;

    rc_t recd_print( const recd_t* r );
    
    rc_t recd_array_create( recd_array_t*&             recd_array_ref,    // 
                            const object_t*            top_level_fmt_cfg, // Same cfg used by recd_type_create() or null if there is fields in the top level
                            const recd_type_t* const * base_recd_typeA,   // Array of base types that will be present in the contained records
                            unsigned                   base_recd_typeN,    
                            unsigned                   allocRecdN = kDefaultAllocRecdCnt); // Records to immediately load into the new record array
    
    rc_t recd_array_destroy( recd_array_t*& recd_array_ref );

    inline rc_t recd_array_count( const recd_array_t* recd_array){ return recd_array->recdN; }

    // Set the record count to zero and initialize all previously set fields to their default values.
    rc_t     recd_array_empty( recd_array_t* recd_array );

    // Data must be a list of dictionaries of the form:
    // [ { <field_name>:<value>, <field_name>:<value> } ]
    // where each dictionary represents a record and each pair in the dictionary is a field labe/value pair.
    //
    // Ex: [ { x:3, c:"blue"},{ x:4, c:"red"}, { x:7, c:"green"} ]
    //
    // Note that this method ony works for record types without a base type.
    // The data array must therefore have been created with a possible incoming recd_type
    // with a 'null' base type.
    rc_t     recd_array_append_from_cfg( recd_array_t* recd_array, const object_t* data_cfg );
    

    // Given the base class id of a type known to this recd_array_t return the assocated recd_type_link_t.
    rc_t recd_array_type_link( const recd_array_t* recd_array, unsigned base_type_class_id, const recd_type_link_t*& link_ref );

    // Append a record to a recd_array where the recd_array is 'passing through' the source record
    // without adding any top level fields.  This is commonly done for record routing and merging operations.
    // This operation is only valid on a recd_array that does not define any top-level fields.
    rc_t recd_array_append_pass_through( recd_array_t* recd_array, const recd_t* src_recd );
    
    rc_t recd_array_print( const recd_array_t* recd_array);
    void recd_array_print_info( const recd_array_t* recd_array );

    const char* recd_array_field_index_to_label( const recd_array_t* recd_array, unsigned com_field_idx );
    unsigned recd_array_field_index( const recd_array_t* recd_array, const char* field_label );
    rc_t     recd_array_field_index( const recd_array_t* recd_array, const char* field_label, unsigned& field_idx_ref );
    
    inline rc_t recd_array_field_index( const recd_array_t* ) { return kOkRC; }
    
    template< typename... ARGS >
    rc_t recd_array_field_index( const recd_array_t* recd_array, const char* field_label, unsigned& com_field_idx_ref, ARGS&&... args )
    {
      rc_t rc = kOkRC;
      
      if((rc = recd_array_field_index(recd_array, field_label, com_field_idx_ref)) != kOkRC )
        return rc;
      
      return recd_array_field_index(recd_array, std::forward<ARGS>(args)...);
    }
    
    template< typename T >
    rc_t recd_get( const recd_t* r, unsigned com_field_idx, T& val_ref )
    {
      rc_t               rc        = kOkRC;      
      unsigned           level_cnt = r->link->fieldLocA[ com_field_idx ].level_cnt;
      unsigned           value_idx = r->link->fieldLocA[ com_field_idx ].value_idx;
      const recd_type_t* rt        = r->link->recd_type;
      
      for(unsigned i=0; i<level_cnt; ++i)
      {
        assert( r->base != nullptr );
        assert( rt->base_type != nullptr );
        r=r->base;
        rt=rt->base_type;
      }

      assert( value_idx < rt->fieldDescN );
      
      if((rc = value_get(r->valA + value_idx, val_ref)) != kOkRC )
      {
        rc = cwLogError(kInvalidStateRC,"Value conversion failed."); 
      }

      return rc;      
    }

    inline rc_t recd_get(const recd_t* r) { return kOkRC; }

    template< typename T, typename... ARGS >
    rc_t recd_get( const recd_t* r, unsigned com_field_idx, T& val_ref, ARGS&&... args )
    {
      rc_t rc = kOkRC;
      
      if((rc = recd_get(r,com_field_idx,val_ref)) != kOkRC )
        goto errLabel;
      
      rc = recd_get(r, std::forward<ARGS>(args)...);

    errLabel:
      return rc;
    }
        
    inline rc_t _recd_set( recd_t* recd ) { return kOkRC; }

    template< typename T, typename... ARGS >
    rc_t _recd_set( recd_t* recd, unsigned com_field_idx, const T& val, ARGS&&... args )
    {
      rc_t rc = kOkRC;

      if( com_field_idx >= recd->link->fieldLocN )
        return cwLogError(kInvalidArgRC,"Fields in the inherited record may not be set.");

      if( recd->link->fieldLocA[ com_field_idx ].level_cnt != 0 )
        return cwLogError(kInvalidArgRC,"Only top level fields may be written.");
      
      if((rc = value_set( recd->valA + recd->link->fieldLocA[ com_field_idx ].value_idx, val)) != kOkRC )
      {
        rc = cwLogError(rc,"Field set failed on '%s'.", cwStringNullGuard(recd_type_field_index_to_label( recd->link->recd_type, com_field_idx )));
        goto errLabel;
      }
      
      return _recd_set(recd,std::forward<ARGS>(args)...);

    errLabel:
      return rc;
      
    }

    template< typename T, typename... ARGS >
    rc_t recd_set( const recd_type_link_t* link,
                   recd_t*                 recd,
                   const recd_t*           base,
                   unsigned                com_field_idx,
                   const T&                val,
                   ARGS&&...               args )
    {
      recd->link = link;
      recd->base = base;
      return _recd_set( recd,com_field_idx,val,args...);
    }

    template< typename T, typename... ARGS >
    rc_t recd_append( recd_array_t* recd_array,
                      const recd_t* base,
                      unsigned      com_field_idx,
                      const T&      val,
                      ARGS&&...     args )
    {
      rc_t rc = kOkRC;
      const recd_type_link_t* link = nullptr;

      // verify that there is space in the recd_array
      if( recd_array->recdN >= recd_array->allocRecdN )
      {
        rc = cwLogError(kBufTooSmallRC,"The recd_array is full.");
        goto errLabel;
      }

      // determine the 'link' pointer based on the base class_id
      if( base == nullptr )
      {        
        if( recd_array->typeN != 1 )
        {
          rc = cwLogError(kInvalidArgRC,"No base record was given but the recd_array has multiple base types registered.");
          goto errLabel;
        }
        
        link = recd_array->typeLinkA;
      }
      else
      {
        if( base->link == nullptr || base->link->recd_type == nullptr )
        {
          rc = cwLogError(kInvalidStateRC,"The supplied 'base' record is not valid.");
          goto errLabel;
        }
        
        if((rc = recd_array_type_link( recd_array, base->link->recd_type->class_id, link )) != kOkRC )
        {
          goto errLabel;
        }
      }

      assert(link != nullptr );
      
      if((rc = recd_set( link, recd_array->recdA + recd_array->recdN, base, com_field_idx, val, args...)) != kOkRC )
      {
        goto errLabel;
      }

      recd_array->recdN += 1;
      
    errLabel:
      if( rc != kOkRC )
        rc = cwLogError(rc,"Record append failed.");
      
      return rc;
    }
    
    
  }
}
