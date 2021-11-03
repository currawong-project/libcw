#ifndef cwObjectTemplate_H
#define cwObjectTemplate_H


namespace cw
{
  objType_t* _objIdToType( objTypeId_t tid );
  object_t*  _objAllocate( objTypeId_t tid=kInvalidTId, object_t* parent=NULL );
  object_t*  _objCreateConainerNode( lex::handle_t lexH, object_t* parent, objTypeId_t tid );
  object_t*  _objAppendLeftMostNode( object_t* parent, object_t* newNode );

  template< typename T >
    object_t* _objSetLeafValue( object_t* obj,  T value )
  {
    cwLogError(kAssertFailRC,"Unhandled object type at leaf node.");
    return nullptr;
  }
  
  template<> object_t* _objSetLeafValue<int8_t>( object_t* obj,  int8_t value )
  {
    obj->u.i8 = value;
    obj->type = _objIdToType(kInt8TId);
    return obj;
  }

  template<> object_t* _objSetLeafValue<uint8_t>( object_t* obj,  uint8_t value )
  {
    obj->u.u8 = value;
    obj->type = _objIdToType(kUInt8TId);
    return obj;
  }


  template<> object_t* _objSetLeafValue<int16_t>( object_t* obj,  int16_t value )
  {
    obj->u.i16 = value;
    obj->type = _objIdToType(kInt16TId);
    return obj;
  }

  template<> object_t* _objSetLeafValue<uint16_t>( object_t* obj,  uint16_t value )
  {
    obj->u.u16 = value;
    obj->type = _objIdToType(kUInt16TId);
    return obj;
  }


  template<> object_t* _objSetLeafValue<int32_t>( object_t* obj,  int32_t value )
  {
    obj->u.i32 = value;
    obj->type = _objIdToType(kInt32TId);
    return obj;
  }

  template<> object_t* _objSetLeafValue<uint32_t>( object_t* obj,  uint32_t value )
  {
    obj->u.u32 = value;
    obj->type = _objIdToType(kUInt32TId);
    return obj;
  }
  
  template<> object_t* _objSetLeafValue<int64_t>( object_t* obj,  int64_t value )
  {
    obj->u.i64 = value;
    obj->type = _objIdToType(kInt64TId);
    return obj;
  }

  template<> object_t* _objSetLeafValue<uint64_t>( object_t* obj,  uint64_t value )
  {
    obj->u.u64 = value;
    obj->type = _objIdToType(kUInt64TId);
    return obj;
  }

  
  template<> object_t* _objSetLeafValue<double>( object_t* obj,  double value )
  {
    obj->u.d  = value;
    obj->type = _objIdToType(kDoubleTId);
    return obj;
  }

  template<> object_t* _objSetLeafValue<bool>( object_t* obj,  bool value )
  {
    obj->u.b  = value;
    obj->type = _objIdToType(kBoolTId);
    return obj;
  }

  template<> object_t* _objSetLeafValue< char*>( object_t* obj,  char* value )
  {
      obj->u.str = value == nullptr ? nullptr : mem::duplStr(value);
      obj->type  = _objIdToType(kStringTId);
      return obj;
  }

  template<> object_t* _objSetLeafValue<const char*>( object_t* obj,  const char* value )
  {
    // cast 'const char*'  to 'char*'
    return _objSetLeafValue<char*>(obj,(char*)value);
  }

  template< typename T >
  object_t* _objCallSetLeafValue( object_t* obj, const T& v )
  {
    if( obj != nullptr )
    {
      // The object value is about to be overwritten so be sure it is deallocated first.
      obj->type->free_value(obj);

      // Set the object value and type id.
      obj = _objSetLeafValue(obj,v);
    }
    
    return obj;
  }

                              
  template< typename T >
    object_t*_objCreateValueNode( object_t* obj,  T value, const char* msg=nullptr, unsigned flags=0 )
  {
    cwLogError(kObjAllocFailRC,"Unhandled type at value node.");
    return NULL;
  }

  template<> object_t* _objCreateValueNode<uint8_t>( object_t* parent,  uint8_t value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objCallSetLeafValue( _objAllocate(), value ) ); }
  
  template<> object_t* _objCreateValueNode<int8_t>( object_t* parent,  int8_t value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objCallSetLeafValue( _objAllocate(), value ) ); }

  template<> object_t* _objCreateValueNode<uint16_t>( object_t* parent,  uint16_t value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objCallSetLeafValue( _objAllocate(), value ) ); }
  
  template<> object_t* _objCreateValueNode<int16_t>( object_t* parent,  int16_t value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objCallSetLeafValue( _objAllocate(), value ) ); }

  template<> object_t* _objCreateValueNode<uint32_t>( object_t* parent,  uint32_t value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objCallSetLeafValue( _objAllocate(), value ) ); }
  
  template<> object_t* _objCreateValueNode<int32_t>( object_t* parent,  int32_t value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objCallSetLeafValue( _objAllocate(), value ) ); }

  template<> object_t* _objCreateValueNode<uint64_t>( object_t* parent,  uint64_t value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objCallSetLeafValue( _objAllocate(), value ) ); }
  
  template<> object_t* _objCreateValueNode<int64_t>( object_t* parent,  int64_t value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objCallSetLeafValue( _objAllocate(), value ) ); }
  
  template<> object_t* _objCreateValueNode<float>( object_t* parent,  float value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objCallSetLeafValue( _objAllocate(), value ) ); }
  
  template<> object_t* _objCreateValueNode<double>( object_t* parent,  double value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objCallSetLeafValue( _objAllocate(), value ) ); }

  template<> object_t* _objCreateValueNode< char*>( object_t* parent, char* value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objCallSetLeafValue( _objAllocate(), value ) ); }

  template<> object_t* _objCreateValueNode<const char*>( object_t* parent, const char* value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objCallSetLeafValue( _objAllocate(), value ) ); }
  
  template<> object_t* _objCreateValueNode<bool>( object_t* parent, bool value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objCallSetLeafValue( _objAllocate(), value ) ); }


  template< typename T >
    object_t*_objCreatePairNode( object_t* parentObj,  const char* label, const T& value, const char* msg=nullptr, unsigned flags=0 )
  {
    object_t* pair = _objAppendLeftMostNode(parentObj, _objAllocate( kPairTId, parentObj) );

    _objCreateValueNode<const char*>( pair, label, msg, flags );
    return _objCreateValueNode<T>( pair, value, msg, flags );

  }

  
  template< typename T >
  rc_t getObjectValue( const T& src, unsigned tid, void* dst, const char* typeLabel )
  {
    rc_t rc = kOkRC;
    switch( tid )
    {
      case kCharTId:   rc = numeric_convert( src, *static_cast<char*>(dst) ); break;
      case kInt8TId:   rc = numeric_convert( src, *static_cast<int8_t*>(dst) ); break;
      case kUInt8TId:  rc = numeric_convert( src, *static_cast<uint8_t*>(dst) );break;
      case kInt16TId:  rc = numeric_convert( src, *static_cast<int16_t*>(dst) );break;
      case kUInt16TId: rc = numeric_convert( src, *static_cast<uint16_t*>(dst) );break;
      case kInt32TId:  rc = numeric_convert( src, *static_cast<int32_t*>(dst) );break;
      case kUInt32TId: rc = numeric_convert( src, *static_cast<uint32_t*>(dst) );break;
      case kInt64TId:  rc = numeric_convert( src, *static_cast<int64_t*>(dst) );break;
      case kUInt64TId: rc = numeric_convert( src, *static_cast<uint64_t*>(dst) );break;
      case kFloatTId:  rc = numeric_convert( src, *static_cast<float*>(dst) );break; 
      case kDoubleTId: rc = numeric_convert( src, *static_cast<double*>(dst) );break;
      case kBoolTId:   rc = numeric_convert( src, *static_cast<bool*>(dst) );break;
      default:
        cwAssert(0);
        rc = cwLogError(kInvalidArgRC,"Invalid destination type id: %i in conversion from '%s'.", tid, typeLabel );
    }
    return rc;
  }

  
}

#endif
