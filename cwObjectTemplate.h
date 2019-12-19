#ifndef cwObjectTemplate_H
#define cwObjectTemplate_H


namespace cw
{
  objType_t*   _objIdToType( objTypeId_t tid );
  object_t* _objAllocate( objTypeId_t tid=kInvalidTId, object_t* parent=NULL );
  object_t* _objCreateConainerNode( lexH_t lexH, object_t* parent, objTypeId_t tid );
  object_t* _objAppendLeftMostNode( object_t* parent, object_t* newNode );

  template< typename T >
    object_t* _objSetLeafValue( object_t* obj,  T value )
  {
    return NULL;
  }
  
  template<> object_t* _objSetLeafValue<int8_t>( object_t* obj,  int8_t value )
  {
    if( obj != NULL )
    {  
      obj->u.i8 = value;
      obj->type = _objIdToType(kInt8TId);
    }
    return obj;
  }

  template<> object_t* _objSetLeafValue<int32_t>( object_t* obj,  int32_t value )
  {
    if( obj != NULL )
    {  
      obj->u.i32 = value;
      obj->type = _objIdToType(kInt32TId);
    }
    return obj;
  }
  
  template<> object_t* _objSetLeafValue<double>( object_t* obj,  double value )
  {
    if( obj != NULL )
    {  
      obj->u.d = value;
      obj->type = _objIdToType(kDoubleTId);
    }
    return obj;
  }
  
  template<> object_t* _objSetLeafValue< char*>( object_t* obj,  char* value )
  {
    if( obj != NULL )
    {  
      obj->u.str = value;
      obj->type = _objIdToType(kStringTId);
    }
    return obj;
  }
  
  template< typename T >
    object_t*_objCreateValueNode( object_t* obj,  T value, const char* msg, unsigned flags=0 )
  {
    return NULL;
  }
    
  template<> object_t* _objCreateValueNode<int8_t>( object_t* parent,  int8_t value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objSetLeafValue( _objAllocate(), value ) ); }

  template<> object_t* _objCreateValueNode<int32_t>( object_t* parent,  int32_t value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objSetLeafValue( _objAllocate(), value ) ); }
  
  template<> object_t* _objCreateValueNode<double>( object_t* parent,  double value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objSetLeafValue( _objAllocate(), value ) ); }

  template<> object_t* _objCreateValueNode< char*>( object_t* parent, char* value, const char* msg, unsigned flags )
  { return _objAppendLeftMostNode( parent, _objSetLeafValue( _objAllocate(), value ) ); }


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
