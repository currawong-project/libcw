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
   kValueContainerFl = 0x01,  // root,pair, or list are the only legal value containers
   kContainerFl      = 0x02,
  };
  
  enum
  {
   kNoRecurseFl    = 0x01,
   kOptionalFl     = 0x02
  };

  struct object_str;
  struct vect_str;

  typedef struct print_ctx_str
  {
    unsigned indent = 0;
    bool listOnOneLineFl = true;
  } print_ctx_t;
 
  typedef struct type_str
  {
    objTypeId_t id;
    const char* label;
    unsigned    flags;

    void (*free)( struct object_str* o );
    rc_t (*value)( const struct object_str* o, unsigned tid, void* dst );
    void (*print)( const struct object_str* o, print_ctx_t& c );
    unsigned( *to_string)( const struct object_str* o, char* buf, unsigned bufByteN );
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
      std::int64_t     i64;
      std::uint64_t    u64;

      bool             b;
        
      float            f;
      double           d;
      
      char*            str;
      struct vect_str* vect;
      
      struct object_str* children; // 'children' is valid when is_container()==true
    } u;


    void unlink();
    void free();
    unsigned child_count() const;

    // Value containers are parents of leaf nodes. (A dictionary is not a value container because it's children are pairs with are not leaf nodes.)
    inline bool is_value_container() const { return type != nullptr && cwIsFlag(type->flags,kValueContainerFl); }

    // Containers have children and use the object.u.children pointer.
    inline bool is_container() const { return type != nullptr && cwIsFlag(type->flags,kContainerFl); }
    inline bool is_pair()      const { return type != nullptr && type->id == kPairTId; }
    inline bool is_dict()      const { return type != nullptr && type->id == kDictTId; }
    inline bool is_list()      const { return type != nullptr && type->id == kListTId; }

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

    const char* pair_label() const;
    
    const struct object_str* pair_value() const;
    struct       object_str* pair_value();

    // Search for the pair label 'label'.
    // Return a pointer to the pair value associated with a given pair label.
    // Set flags to kNoRecurseFl to not recurse into the object in search of the label.
    const struct object_str* find( const char* label, unsigned flags=0 ) const;
    struct       object_str* find( const char* label, unsigned flags=0 );
    
    const struct object_str* child_ele( unsigned idx ) const;
    struct       object_str* child_ele( unsigned idx );

    // Set flag  'kNoRecurseFl' to no recurse into the object in search of the value.
    // Set flag  'kOptional' if the label is optional and may not exist.
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
    
    rc_t getv() const { return kOkRC; } 

    // getv("label0",v0,"label1",v1, ... )
    template< typename T0, typename T1, typename... ARGS >
      rc_t getv( T0 label, T1& valRef, ARGS&&... args ) const
    {
      rc_t rc;

      if((rc = get(label,valRef)) == kOkRC )
        if((rc = getv(std::forward<ARGS>(args)...)) != kOkRC )
          cwLogError(rc,"getv() failed for the pair label:'%s'.",cwStringNullGuard(label));
      
      return rc;
    }

    template< typename T >
      struct object_str* insertPair( const char* label, const T& v )
    {  return newPairObject(label, v, this); }
    
    unsigned to_string( char* buf, unsigned bufByteN ) const;
    void print(const print_ctx_t* c=NULL) const;
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

  // Return a pointer to the value node.
  object_t* newPairObject( const char* label, std::uint8_t  v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::int8_t   v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::int16_t  v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::uint16_t v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::int32_t  v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::uint32_t v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::int64_t  v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, std::uint64_t v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, bool          v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, float         v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, double        v, object_t* parent=nullptr);      
  object_t* newPairObject( const char* label, char*         v, object_t* parent=nullptr);
  object_t* newPairObject( const char* label, const char*   v, object_t* parent=nullptr);
  
  rc_t objectFromString( const char* s, object_t*& objRef );
  rc_t objectFromFile( const char* fn, object_t*& objRef );
  void objectPrintTypes( object_t* o );

}

#endif
