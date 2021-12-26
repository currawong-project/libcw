#ifndef cwObject_H
#define cwObject_H


namespace cw
{
  
  enum
  {
   kInvalidTId = 0x00000000,
   kNullTId    = 0x00000001,
   kErrorTId   = 0x00000002,
   kCharTId    = 0x00000004,
   kInt8TId    = 0x00000008,
   kUInt8TId   = 0x00000010,
   kInt16TId   = 0x00000020,
   kUInt16TId  = 0x00000040,
   kInt32TId   = 0x00000080,
   kUInt32TId  = 0x00000100,
   kInt64TId   = 0x00000200,
   kUInt64TId  = 0x00000400,
   kFloatTId   = 0x00000800,
   kDoubleTId  = 0x00001000,
   kBoolTId    = 0x00002000,
   kStringTId  = 0x00004000,
   kCStringTId = 0x00008000,  // static string (don't delete)
   kVectTId    = 0x00010000,
   kPairTId    = 0x00020000,
   kListTId    = 0x00040000,
   kDictTId    = 0x00080000,
   kRootTId    = 0x00100000,

   kHexFl      = 0x10000000,
   kIdentFl    = 0x20000000
   
  };

  typedef unsigned objTypeId_t;
  
  enum
  {
    // Value containers children are leaf-nodes: root,pair, or list are the only legal value containers.
    // Nnote that a dictionary is not a value container because it's children are pairs.
    kValueContainerFl = 0x01,
    
    // This node contains other nodes
    kContainerFl      = 0x02,  
  };
  
  enum
  {
   kRecurseFl      = 0x01,
   kOptionalFl     = 0x02
  };

  struct object_str;
  struct vect_str;

  typedef struct print_ctx_str
  {
    unsigned indent          = 0;
    bool     listOnOneLineFl = true;
  } print_ctx_t;
 
  typedef struct type_str
  {
    objTypeId_t id;
    const char* label;
    unsigned    flags;

    // Deallocate the the object body and value.
    void (*free)( struct object_str* o );

    // Deallocate the object value but not the object body
    void (*free_value)( struct object_str* o );

    rc_t (*value)( const struct object_str* o, unsigned tid, void* dst );

    // Print the object.
    void (*print)( const struct object_str* o, print_ctx_t& c );

    // Convert the object to a string and return the length of the string.
    unsigned( *to_string)( const struct object_str* o, char* buf, unsigned bufByteN );

    // Duplicate 'src'.
    struct object_str* (*duplicate)( const struct object_str* src, struct object_str* parent );
    
  } objType_t;


  typedef struct object_str
  {
    objType_t*         type    = nullptr;
    struct object_str* parent  = nullptr;
    struct object_str* sibling = nullptr;
    union
    {
      char             c;
      
      std::int8_t      i8;
      std::uint8_t     u8;
      std::int16_t     i16;
      std::uint16_t    u16;
      std::int32_t     i32;
      std::uint32_t    u32;
      long long        i64;
      unsigned long long u64;

      bool             b;
        
      float            f;
      double           d;
      
      char*            str;
      struct vect_str* vect;
      
      struct object_str* children; // 'children' is valid when is_container()==true
    } u;


    // Unlink this node from it's parents and siblings.
    void unlink();

    // free all resource associated with this object.
    void free();

    // Append the child node to this objects child list.
    rc_t append_child( struct object_str* child );
    
    unsigned child_count() const;

    // Value containers are parents of leaf nodes. (A dictionary is not a value container because it's children are pairs with are not leaf nodes.)
    inline bool is_value_container() const { return type != nullptr && cwIsFlag(type->flags,kValueContainerFl); }

    // Containers have children and use the object.u.children pointer.
    inline bool is_container() const { return type != nullptr && cwIsFlag(type->flags,kContainerFl); }
    inline bool is_pair()      const { return type != nullptr && type->id == kPairTId; }
    inline bool is_dict()      const { return type != nullptr && type->id == kDictTId; }
    inline bool is_list()      const { return type != nullptr && type->id == kListTId; }
    inline bool is_string()    const { return type != nullptr && (type->id == kStringTId || type->id == kCStringTId); }
    inline bool is_type( unsigned tid ) const { return type != nullptr && type->id == tid; }

    rc_t value( void* dst, unsigned dstTypeId );
    rc_t value( char& v ) const;
    rc_t value( int8_t&  v ) const;
    rc_t value( uint8_t& v ) const;
    rc_t value( int16_t&  v ) const;
    rc_t value( uint16_t& v ) const;
    rc_t value( int32_t&  v ) const;
    rc_t value( uint32_t& v ) const;
    rc_t value( int64_t&  v ) const;
    rc_t value( uint64_t& v ) const;
    rc_t value( float&  v ) const;
    rc_t value( double& v ) const;
    rc_t value( bool& v ) const;
    rc_t value( char*& v ) const;
    rc_t value( const char*& v ) const;
    rc_t value( const struct object_str*& v) const {v=this; return kOkRC; }


    // Note that these setters will change the type of the object to match the value 'v'.
    // They do not convert 'v' to the current type of the object.
    rc_t set_value( char v );
    rc_t set_value( int8_t  v );
    rc_t set_value( uint8_t v );
    rc_t set_value( int16_t  v );
    rc_t set_value( uint16_t v );
    rc_t set_value( int32_t  v );
    rc_t set_value( uint32_t v );
    rc_t set_value( int64_t  v );
    rc_t set_value( uint64_t v );
    rc_t set_value( float  v );
    rc_t set_value( double v );
    rc_t set_value( bool v );
    rc_t set_value( char* v );
    rc_t set_value( const char* v );
    
    const char* pair_label() const;
    
    const struct object_str* pair_value() const;
    struct       object_str* pair_value();

    // Search for the pair label 'label'.
    // Return a pointer to the pair value associated with a given pair label.
    // Set flags to kRecurseFl to recurse into the object in search of the label.
    const struct object_str* find( const char* label, unsigned flags=0 ) const;
    struct       object_str* find( const char* label, unsigned flags=0 );
    
    const struct object_str* find_child( const char* label ) const { return find(label); }
    struct       object_str* find_child( const char* label )       { return find(label); }
    
    const struct object_str* child_ele( unsigned idx ) const;
    struct       object_str* child_ele( unsigned idx );

    // Set 'ele' to nullptr to return first child.  Returns nullptr when 'ele' is last child.
    const struct object_str* next_child_ele( const struct object_str* ele) const;
    struct       object_str* next_child_ele(       struct object_str* ele);

    
    // Set flag  'kRecurseFl' to recurse into the object in search of the value.
    // Set flag  'kOptionalFl' if the label is optional and may not exist.
    template< typename T >
      rc_t get( const char* label, T& v, unsigned flags=0  ) const
    {
      const struct object_str* o;
      if((o = find(label, flags)) == nullptr )
      {
        if( cwIsNotFlag(flags, kOptionalFl) )
          return cwLogError(kInvalidIdRC,"The pair label '%s' could not be found.",cwStringNullGuard(label));
        
        return kLabelNotFoundRC;
        
      }
      return o->value(v);
    }
    
    rc_t _getv(unsigned flags) const { return kOkRC; } 

    // getv("label0",v0,"label1",v1, ... )
    template< typename T0, typename T1, typename... ARGS >
      rc_t _getv( unsigned flags, T0 label, T1& valRef, ARGS&&... args ) const
    {
      rc_t rc = get(label,valRef,flags);

      // if no error occurred ....
      if( rc == kOkRC || (rc == kLabelNotFoundRC && cwIsFlag(flags,kOptionalFl)))
        rc =  _getv(flags, std::forward<ARGS>(args)...); // ... recurse to find next label/value pair
      else
        rc = cwLogError(rc,"Object parse failed for the pair label:'%s'.",cwStringNullGuard(label));

      return rc;
    }

    // getv("label0",v0,"label1",v1, ... )
    template< typename T0, typename T1, typename... ARGS >
      rc_t getv( T0 label, T1& valRef, ARGS&&... args ) const
    { return _getv(0,label,valRef,args...); }

    // getv("label0",v0,"label1",v1, ... ) where all values are optional
    template< typename T0, typename T1, typename... ARGS >
      rc_t getv_opt( T0 label, T1& valRef, ARGS&&... args ) const
    { return _getv(kOptionalFl,label,valRef,args...); }
    
    template< typename T >
      struct object_str* insert_pair( const char* label, const T& v )
    {  return newPairObject(label, v, this); }


    template< typename T>
    rc_t set( const char* label, const T& value )
    {
      struct object_str* pair_value;
      if((pair_value = find_child(label)) == nullptr )
        return cwLogError(kInvalidIdRC,"Set failed the object dictionary label '%s' could not be found.",label);
      
      return pair_value->set_value( value );
    }

    
    // convert this object to a string
    unsigned to_string( char* buf, unsigned bufByteN ) const;

    // print this object
    void print(const print_ctx_t* c=NULL) const;

    // duplicate this object
    struct object_str* duplicate() const;

  } object_t;

  object_t* newObject( std::uint8_t  v, object_t* parent=nullptr);
  object_t* newObject( std::int8_t   v, object_t* parent=nullptr);
  object_t* newObject( std::int16_t  v, object_t* parent=nullptr);
  object_t* newObject( std::uint16_t v, object_t* parent=nullptr);
  object_t* newObject( std::int32_t  v, object_t* parent=nullptr);
  object_t* newObject( std::uint32_t v, object_t* parent=nullptr);
  object_t* newObject( std::int64_t  v, object_t* parent=nullptr);
  object_t* newObject( std::uint64_t v, object_t* parent=nullptr);
  object_t* newObject( bool          v, object_t* parent=nullptr);
  object_t* newObject( float         v, object_t* parent=nullptr);
  object_t* newObject( double        v, object_t* parent=nullptr);      
  object_t* newObject( char*         v, object_t* parent=nullptr);
  object_t* newObject( const char*   v, object_t* parent=nullptr);
  object_t* newDictObject( object_t* parent=nullptr );
  object_t* newListObject( object_t* parent=nullptr );

  // Return a pointer to the value node.
  object_t* newPairObject( const char* label, object_t*       v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::uint8_t    v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::int8_t     v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::int16_t    v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::uint16_t   v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::int32_t    v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::uint32_t   v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::int64_t    v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::uint64_t   v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, bool            v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, float           v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, double          v, object_t* parent=nullptr);      
  object_t* newPairObject( const char* label, char*           v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, const char*     v, object_t* parent=nullptr);
  
  rc_t objectFromString( const char* s, object_t*& objRef );
  rc_t objectFromFile( const char* fn, object_t*& objRef );
  void objectPrintTypes( object_t* o );


}

#endif
