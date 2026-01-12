//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include <type_traits>

#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMem.h"
#include "cwFile.h"
#include "cwTest.h"
#include "cwLex.h"
#include "cwText.h"
#include "cwNumericConvert.h"
#include "cwObject.h"
#include "cwObjectTemplate.h"


namespace cw
{
  enum
  {
   kLCurlyLexTId = cw::lex::kUserLexTId+1, 
   kRCurlyLexTId,
   kLHardLexTId,
   kRHardLexTId,
   kColonLexTId,
   kCommaLexTId,
   kTrueLexTId,
   kFalseLexTId,
   kNullLexTId,
   kSegmentedIdLexTId,  // id with embedded periods
  };

  idLabelPair_t _objTokenArray[] =
  {
   { kLCurlyLexTId, "{" },
   { kRCurlyLexTId, "}" },
   { kLHardLexTId,  "[" },
   { kRHardLexTId,  "]" },
   { kColonLexTId,  ":" },
   { kCommaLexTId,  "," },
   { kTrueLexTId,   "true"},
   { kFalseLexTId,  "false"},
   { kNullLexTId,   "null" },
   { lex::kErrorLexTId,""}  
  };

  unsigned _udiff( unsigned n, unsigned i )
  {
    return i>=n ? 0 : n-i;
  }

  unsigned _lexSegmentedIdMatcher( const char* cp, unsigned cn )
  {
      unsigned i = 0;
      if( isalpha(cp[0]) || (cp[0]== '_'))
      {
        i = 1;
        for(; i<cn; ++i)
          if( !isalnum(cp[i]) && (cp[i] != '_') && (cp[i] != '.') )
            break;
      }
      return i;    
  }

  
  void _objTypeFree( object_t* o )
  {
    o->type->free_value(o);
    mem::release(o);
  }
  
  
  void _objTypeFreeValue( object_t* o )
  {}

  void _objTypeFreeValueString( object_t* o )
  {
    mem::release( o->u.str );
  }


  
  const char* _objTypeIdToLabel( objTypeId_t tid );

  
  rc_t _objTypeValueFromChar(     const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.c,tid,  dst,o->type->label); }
  rc_t _objTypeValueFromInt8(     const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.i8,tid, dst,o->type->label); }
  rc_t _objTypeValueFromUInt8(    const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.u8,tid, dst,o->type->label); }
  rc_t _objTypeValueFromInt16(    const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.i16,tid,dst,o->type->label); }
  rc_t _objTypeValueFromUInt16(   const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.u16,tid,dst,o->type->label); }
  rc_t _objTypeValueFromInt32(    const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.i32,tid,dst,o->type->label); }
  rc_t _objTypeValueFromUInt32(   const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.u32,tid,dst,o->type->label); }
  rc_t _objTypeValueFromInt64(    const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.i64,tid,dst,o->type->label); }
  rc_t _objTypeValueFromUInt64(   const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.u64,tid,dst,o->type->label); }
  rc_t _objTypeValueFromFloat(    const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.f,tid,  dst,o->type->label); }
  rc_t _objTypeValueFromDouble(   const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.d,tid,  dst,o->type->label); }
  rc_t _objTypeValueFromBool(     const object_t* o, unsigned tid, void* dst ) { return getObjectValue(o->u.b,tid,dst,o->type->label); }

  rc_t _objTypeValueFromNonValue( const object_t* o, unsigned tid, void* dst )
  {
    switch(tid)
    {
      case kCStringTId:
        *(const char**)dst = nullptr;
        return kOkRC;
        
      case kStringTId:
        *(char**)dst = nullptr;
        return kOkRC;
    }
    
    return cwLogError(kInvalidArgRC, "There is no conversion from '%s' to '%s'.", _objTypeIdToLabel(tid), o->type->label);
  }

  rc_t _objTypeValueFromCString(   const object_t* o, unsigned tid, void* dst )
  {
    if( tid == kCStringTId )
    {
      *(const char**)dst = o->u.str;
      return kOkRC;
    }

    return _objTypeValueFromNonValue(o,tid,dst);
  }
  
  rc_t _objTypeValueFromString(   const object_t* o, unsigned tid, void* dst )
  {
    // When objects are parsed all strings are non-const. therefore when a string is retrieved
    // from an object the string will always be non-const - but the type of the variable
    // to receive the value may be const - we detect this here and redirect to the 'const char*'
    // version of the function.
    if( tid == kCStringTId )
      return _objTypeValueFromCString(o,tid,dst);
    
    if( tid == kStringTId )
    {
      *(char**)dst = o->u.str;
      return kOkRC;
    }

    if( tid == kNullTId )
    {
      *(char**)dst = nullptr;
      return kOkRC;
    }
    
    return _objTypeValueFromNonValue(o,tid,dst);
  }

  
  rc_t _objTypeValueFromVect(     const object_t* o, unsigned tid, void* dst )
  { return _objTypeValueFromNonValue(o,tid,dst); }

  void _objTypePrintIndent( const char* text, unsigned indent, const char* indentStr=" " )
  {
    for(unsigned i=0; i<indent; ++i)
      cwLogPrint("%s",indentStr);
    cwLogPrint("%s",text); 
  }

  void _objTypePrintChild( const object_t* o, print_ctx_t& c, const char* eolStr=",\n", const char* indentStr=" " )
  {
    _objTypePrintIndent(" ",c.indent,indentStr);
    o->type->print(o,c);
    cwLogPrint("%s",eolStr);
  }
  
  void _objTypePrintNull(   const object_t* o, print_ctx_t& c ) { cwLogPrint("NULL "); }
  void _objTypePrintError(  const object_t* o, print_ctx_t& c ) { cwLogPrint("Error "); }
  void _objTypePrintChar(   const object_t* o, print_ctx_t& c ) { cwLogPrint("%c",o->u.c); }
  void _objTypePrintInt8(   const object_t* o, print_ctx_t& c ) { cwLogPrint("%i",o->u.i8); }
  void _objTypePrintUInt8(  const object_t* o, print_ctx_t& c ) { cwLogPrint("%i",o->u.u8); }
  void _objTypePrintInt16(  const object_t* o, print_ctx_t& c ) { cwLogPrint("%i",o->u.i16); }
  void _objTypePrintUInt16( const object_t* o, print_ctx_t& c ) { cwLogPrint("%i",o->u.u16); }
  void _objTypePrintInt32(  const object_t* o, print_ctx_t& c ) { cwLogPrint("%i",o->u.i32); }
  void _objTypePrintUInt32( const object_t* o, print_ctx_t& c ) { cwLogPrint("%i",o->u.u32); }
  void _objTypePrintInt64(  const object_t* o, print_ctx_t& c ) { cwLogPrint("%lli", o->u.i64); }
  void _objTypePrintUInt64( const object_t* o, print_ctx_t& c ) { cwLogPrint("%lli", o->u.u64); }
  void _objTypePrintBool(   const object_t* o, print_ctx_t& c ) { cwLogPrint("%s",o->u.b ? "true" : "false"); }
  void _objTypePrintFloat(  const object_t* o, print_ctx_t& c ) { cwLogPrint("%f",o->u.f); }
  void _objTypePrintDouble( const object_t* o, print_ctx_t& c ) { cwLogPrint("%f",o->u.d); }
  void _objTypePrintString( const object_t* o, print_ctx_t& c ) { cwLogPrint("%s",o->u.str); }
  void _objTypePrintVect(   const object_t* o, print_ctx_t& c ) { cwLogPrint("<vect>"); }
  void _objTypePrintPair(   const object_t* o, print_ctx_t& c )
  {
    o->u.children->type->print(o->u.children,c);
    cwLogPrint(": ");
    o->u.children->sibling->type->print(o->u.children->sibling,c);    
  }
  
  void _objTypePrintList( const object_t* o, print_ctx_t& c )
  {
    const char* indentStr  = c.listOnOneLineFl ? ""    : " ";

    char bracketStr[] = { '[','\0','\0' };
    char eoValStr[]   = { ',','\0','\0' };

    if(!c.listOnOneLineFl)
    {
      bracketStr[1] = '\n';
      eoValStr[1] = '\n';
    }
      
    
    _objTypePrintIndent(bracketStr,0);
    c.indent += 2;
    
    for(const object_t* ch=o->u.children; ch!=nullptr; ch=ch->sibling)
    {
      if( ch->sibling == nullptr )
        eoValStr[0] = ' ';
      
      _objTypePrintChild(ch,c,eoValStr,indentStr);
    }
    
    c.indent -= 2;
    _objTypePrintIndent("]",c.listOnOneLineFl ? 0 : c.indent);
  }
  
  void _objTypePrintDict( const object_t* o, print_ctx_t& c )
  {
    _objTypePrintIndent("{\n",0);
    c.indent += 2;
    
    for(const object_t* ch=o->u.children; ch!=nullptr; ch=ch->sibling)
      _objTypePrintChild(ch,c);

    c.indent -= 2;
    _objTypePrintIndent("}",c.indent);
  }
  
  void _objTypePrintRoot( const object_t* o, print_ctx_t& c )
  {
    _objTypePrintDict(o,c);
  }

  
  unsigned _objTypeToStringNull(   const object_t* o, char* buf, unsigned n ) { return snprintf(buf,n,"NULL "); }
  unsigned _objTypeToStringError(  const object_t* o, char* buf, unsigned n ) { return snprintf(buf,n,"Error "); }
  unsigned _objTypeToStringChar(   const object_t* o, char* buf, unsigned n ) { return toText(buf,n,o->u.c); }
  unsigned _objTypeToStringInt8(   const object_t* o, char* buf, unsigned n ) { return toText(buf,n,o->u.i8); }
  unsigned _objTypeToStringUInt8(  const object_t* o, char* buf, unsigned n ) { return toText(buf,n,o->u.u8); }
  unsigned _objTypeToStringInt16(  const object_t* o, char* buf, unsigned n ) { return toText(buf,n,o->u.i16); }
  unsigned _objTypeToStringUInt16( const object_t* o, char* buf, unsigned n ) { return toText(buf,n,o->u.u16); }
  unsigned _objTypeToStringInt32(  const object_t* o, char* buf, unsigned n ) { return toText(buf,n,o->u.i32); }
  unsigned _objTypeToStringUInt32( const object_t* o, char* buf, unsigned n ) { return toText(buf,n,o->u.u32); }
  unsigned _objTypeToStringInt64(  const object_t* o, char* buf, unsigned n ) { return toText(buf,n,o->u.i64); }
  unsigned _objTypeToStringUInt64( const object_t* o, char* buf, unsigned n ) { return toText(buf,n,o->u.u64); }
  unsigned _objTypeToStringBool(   const object_t* o, char* buf, unsigned n ) { return toText(buf,n,o->u.b); }
  unsigned _objTypeToStringFloat(  const object_t* o, char* buf, unsigned n ) { return toText(buf,n,o->u.f); }
  unsigned _objTypeToStringDouble( const object_t* o, char* buf, unsigned n ) { return toText(buf,n,o->u.d); }
  unsigned _objTypeToStringVect(   const object_t* o, char* buf, unsigned n ) { return snprintf(buf,n,"<vect>"); }

  unsigned _objTypeToStringString( const object_t* o, char* buf, unsigned n )
  {
    unsigned i = snprintf(buf,n,"\"");
    const char* str = o->u.str;

    if( str == nullptr )
    {
      cwLogWarning("Unexpected empty string while generating text output.");
    }
    
    i += toText(buf+i,_udiff(n,i),str);
    
    return i + snprintf(buf+i,_udiff(n,i),"\"");
  }
  
  unsigned _objTypeToStringPair(   const object_t* o, char* buf, unsigned n )
  {
    unsigned i = o->u.children->type->to_string(o->u.children,buf,n);
    i += snprintf(buf+i,_udiff(n,i)," : ");
    return i + o->u.children->sibling->type->to_string(o->u.children->sibling,buf+i,_udiff(n,i));    
  }
  
  unsigned _objTypeToStringList( const object_t* o, char* buf, unsigned n )
  {
    unsigned i = snprintf(buf,n," [ ");
    
    for(const object_t* ch=o->u.children; ch!=nullptr; ch=ch->sibling)
    {

      i += ch->type->to_string(ch,buf+i,_udiff(n,i));

      if( ch->sibling != nullptr )
        i += snprintf(buf+i,_udiff(n,i),", ");
      
    }

    i += snprintf(buf+i,_udiff(n,i)," ] ");

    return i;
  }
  
  unsigned _objTypeToStringDict( const object_t* o, char* buf, unsigned n )
  {

    unsigned i = snprintf(buf,n," { " );
    
    for(const object_t* ch=o->u.children; ch!=nullptr; ch=ch->sibling)
    {      
      i += ch->type->to_string(ch,buf+i,_udiff(n,i));
      
      if( ch->sibling != nullptr )
        i += snprintf(buf+i,_udiff(n,i),", ");
    }
    
    i += snprintf(buf+i,_udiff(n,i)," } ");

    return i;
  }
  
  unsigned _objTypeToStringRoot( const object_t* o, char* buf, unsigned n )
  {
    return _objTypeToStringDict(o,buf,n);
  }



  object_t* _objTypeDuplContainer( const struct object_str* src, struct object_str* parent )
  {
    object_t* o = _objAppendRightMostNode( parent, _objAllocate( src->type->id, parent ));
    for(object_t* ch=src->u.children; ch!=nullptr; ch=ch->sibling)
      ch->type->duplicate(ch,o);

    return o;
  }
  
  object_t* _objTypeDuplNull(   const struct object_str* src, struct object_str* parent ) { return _objAllocate( src->type->id, parent); }
  object_t* _objTypeDuplError(  const struct object_str* src, struct object_str* parent ) { return _objAllocate( src->type->id, parent); }
  object_t* _objTypeDuplChar(   const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<char>(parent,src->u.c); }
  object_t* _objTypeDuplInt8(   const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<int8_t>(parent,src->u.i8); }
  object_t* _objTypeDuplUInt8(  const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<uint8_t>(parent,src->u.u8); }
  object_t* _objTypeDuplInt16(  const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<int16_t>(parent,src->u.i16); }
  object_t* _objTypeDuplUInt16( const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<int16_t>(parent,src->u.u16); }
  object_t* _objTypeDuplInt32(  const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<int32_t>(parent,src->u.i32); }
  object_t* _objTypeDuplUInt32( const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<int32_t>(parent,src->u.u32); }
  object_t* _objTypeDuplInt64(  const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<int64_t>(parent,src->u.i64); }
  object_t* _objTypeDuplUInt64( const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<uint64_t>(parent,src->u.u64); }
  object_t* _objTypeDuplBool(   const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<bool   >(parent,src->u.b  ); }
  object_t* _objTypeDuplFloat(  const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<float  >(parent,src->u.f  ); }
  object_t* _objTypeDuplDouble( const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<double >(parent,src->u.d  ); }
  object_t* _objTypeDuplString( const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<char*  >(parent,src->u.str); }
  object_t* _objTypeDuplCString(const struct object_str* src, struct object_str* parent ) { return _objCreateValueNode<const char*>(parent,src->u.str);}    
  object_t* _objTypeDuplVect(   const struct object_str* src, struct object_str* parent ) { assert(0); return nullptr; }
  object_t* _objTypeDuplPair(   const struct object_str* src, struct object_str* parent ) {  return _objTypeDuplContainer(src,parent); }
  object_t* _objTypeDuplList(   const struct object_str* src, struct object_str* parent ) {  return _objTypeDuplContainer(src,parent); }
  object_t* _objTypeDuplDict(   const struct object_str* src, struct object_str* parent ) {  return _objTypeDuplContainer(src,parent); }
  object_t* _objTypeDuplRoot(   const struct object_str* src, struct object_str* parent ) {  return _objTypeDuplContainer(src,parent); }
  
  
  objType_t _objTypeArray[] =
  {
   { kNullTId,    "null",      0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromNonValue, _objTypePrintNull,   _objTypeToStringNull,   _objTypeDuplNull },
   { kErrorTId,   "error",     0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromNonValue, _objTypePrintError,  _objTypeToStringError,  _objTypeDuplError },
   { kCharTId,    "char",      0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromChar,     _objTypePrintChar,   _objTypeToStringChar,   _objTypeDuplChar },
   { kInt8TId,    "int8",      0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromInt8,     _objTypePrintInt8,   _objTypeToStringInt8,   _objTypeDuplInt8 },
   { kUInt8TId,   "uint8",     0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromUInt8,    _objTypePrintUInt8,  _objTypeToStringUInt8,  _objTypeDuplUInt8 },
   { kInt16TId,   "int16",     0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromInt16,    _objTypePrintInt16,  _objTypeToStringInt16,  _objTypeDuplInt16 },
   { kUInt16TId,  "uint16",    0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromUInt16,   _objTypePrintUInt16, _objTypeToStringUInt16, _objTypeDuplUInt16 },
   { kInt32TId,   "int32",     0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromInt32,    _objTypePrintInt32,  _objTypeToStringInt32,  _objTypeDuplInt32 },
   { kUInt32TId,  "uint32",    0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromUInt32,   _objTypePrintUInt32, _objTypeToStringUInt32, _objTypeDuplUInt32 },
   { kInt64TId,   "int64",     0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromInt64,    _objTypePrintInt64,  _objTypeToStringInt64,  _objTypeDuplInt64 },
   { kUInt64TId,  "uint64",    0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromUInt64,   _objTypePrintUInt64, _objTypeToStringUInt64, _objTypeDuplUInt64 },
   { kBoolTId,    "bool",      0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromBool,     _objTypePrintBool,   _objTypeToStringBool,   _objTypeDuplBool },
   { kFloatTId,   "float",     0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromFloat,    _objTypePrintFloat,  _objTypeToStringFloat,  _objTypeDuplFloat },
   { kDoubleTId,  "double",    0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromDouble,   _objTypePrintDouble, _objTypeToStringDouble, _objTypeDuplDouble },
   { kStringTId,  "string",    0,                                _objTypeFree,  _objTypeFreeValueString, _objTypeValueFromString,   _objTypePrintString, _objTypeToStringString, _objTypeDuplString },
   { kCStringTId, "cstring",   0,                                _objTypeFree,  _objTypeFreeValueString, _objTypeValueFromCString,  _objTypePrintString, _objTypeToStringString, _objTypeDuplCString },
   { kVectTId,    "vect",      0,                                _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromVect,     _objTypePrintVect,   _objTypeToStringVect,   _objTypeDuplVect },
   { kPairTId,    "pair",      kContainerFl | kValueContainerFl, _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromNonValue, _objTypePrintPair,   _objTypeToStringPair,   _objTypeDuplPair },
   { kListTId,    "list",      kContainerFl | kValueContainerFl, _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromNonValue, _objTypePrintList,   _objTypeToStringList,   _objTypeDuplList },
   { kDictTId,    "dict",      kContainerFl,                     _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromNonValue, _objTypePrintDict,   _objTypeToStringDict,   _objTypeDuplDict },
   { kRootTId,    "root",      kContainerFl | kValueContainerFl, _objTypeFree,  _objTypeFreeValue,       _objTypeValueFromNonValue, _objTypePrintRoot,   _objTypeToStringRoot,   _objTypeDuplRoot },
   { kInvalidTId, "<invalid>", 0,                                nullptr,       nullptr,                 nullptr,                   nullptr,             nullptr,              nullptr }   
  };



  objType_t* _objIdToType( objTypeId_t tid )
  {
    unsigned i;
    for(i=0; _objTypeArray[i].id != kInvalidTId; ++i)
      if( _objTypeArray[i].id == tid )
        return _objTypeArray + i;

    cwLogError(kInvalidIdRC,"The object type id %i is not valid.",tid);
    return nullptr;
  }

  const char* _objTypeIdToLabel( objTypeId_t tid )
  {
    const objType_t* type;
    if((type = _objIdToType(tid)) == nullptr )
      return "<invalid>";
    
    return type->label;
  }

  
  object_t* _objAllocate( objTypeId_t tid, object_t* parent )
  {
    objType_t* type = nullptr;
    if( tid != kInvalidTId )
    {
      if((type = _objIdToType(tid)) == nullptr )
      {
        cwLogError(kObjAllocFailRC,"Object allocation failed.");
        return nullptr;
      }
    }
    
    object_t* o =  mem::allocZ<object_t>();
    o->type   = type;
    o->parent = parent;
    
    return o;
  }

  rc_t _objSyntaxError( lex::handle_t lexH, const char* fmt, ... )
  {
    va_list vl;
    va_start(vl,fmt);

    cwLogVError( kSyntaxErrorRC, fmt, vl );
    cwLogError(  kSyntaxErrorRC, "Error on line: %i.", lex::currentLineNumber(lexH));
    va_end(vl);
    return kSyntaxErrorRC;
  }
 
  
  rc_t _objVerifyParentIsValueContainer( lex::handle_t lexH, const object_t* parent, const char* msg )
  {
    if( parent == nullptr )
      return _objSyntaxError(lexH,"The parent node must always be valid.");
    
    // it is legal for a parent of a value to be null when the value is the root element.
    if( !(parent->is_value_container()))
      return _objSyntaxError(lexH,"Value nodes of type '%s' must be contained by 'root', 'pair' or 'array' node.",msg);
    
    return kOkRC;
  }
  
  object_t* _objAppendRightMostNode( object_t* parent, object_t* newNode )
  {
    if( newNode == nullptr )
      return nullptr;

    if( parent != nullptr )
    {
      assert( parent->is_container() );
      
      object_t* child = parent->u.children;

      if( parent->u.children == nullptr )
        parent->u.children = newNode;
      else
      {
        while( child->sibling != nullptr )
          child = child->sibling;

        child->sibling = newNode;
      }
    }
      
    newNode->parent = parent;
    return newNode;
  }

  object_t* _objCreateConainerNode( lex::handle_t lexH, object_t* parent, objTypeId_t tid )
  {
    if( _objVerifyParentIsValueContainer(lexH,parent,_objTypeIdToLabel(tid)) == kOkRC )
      return _objAppendRightMostNode( parent, _objAllocate( tid, parent ));
    
    return nullptr;            
  }

  template< typename T >
  rc_t _objSetValue( object_t* obj, const T& v )
  {
    cw::rc_t rc = kOkRC;
    if(_objCallSetLeafValue( obj, v ) == nullptr)
      rc = cwLogError(kOpFailRC,"Object value assignment failed.");
    return rc;
  }

  void _escape_string( char* s )
  {
    unsigned ii = 0;
    unsigned oi = 0;
    bool esc_fl = false;
    
    for(; s[ii] != 0; ++ii)
    {
      if( !esc_fl && s[ii] == '\\' )
      {
        esc_fl = true;
        continue;
      }

      if( esc_fl )
      {
        switch(s[ii])
        {
          case 'b': s[oi] = '\b'; break;
          case 'f': s[oi] = '\f'; break;
          case 'n': s[oi] = '\n'; break;
          case 'r': s[oi] = '\r'; break;
          case 't': s[oi] = '\t'; break;
          default:
            s[oi++] = '\\';
            s[oi]   = s[ii];
        }
      }
      else
      {
        s[oi] = s[ii];
      }
      
      ++oi;
      esc_fl = false;
      
        
    }

    assert( oi <= ii );
    s[oi] = 0;
  }
  
}


void cw::object_t::unlink()
{
  // if this node has no parent then there it is not part of a tree
  // and therefore cannot be unlinked
  if( parent == nullptr )
    return;
  
  object_t* c0 = nullptr;
  object_t* c = parent->u.children;
  for(; c!=nullptr; c=c->sibling)
  {
    if( c == this )
    {
      if( c0 == nullptr )
        parent->u.children = c->sibling;
      else
        c0->sibling = c->sibling;

      c->parent  = nullptr;
      c->sibling = nullptr;
      return;
    }
    c0 = c;
  }

  // if a child has a parent then it must be in that parent's child list
  cwAssert(0);  
}

void cw::object_t::free()
{
  unlink();
  
  if( is_container() )
  {
    object_t* o1 = nullptr;
    for(object_t* o = u.children; o != nullptr; o=o1 )
    {
      o1 = o->sibling;
      o->free();
    }
  }
  
  type->free(this);
}

cw::rc_t cw::object_t::append_child( struct object_str* child )
{
  if( !is_container() )
    return cwLogError(kInvalidDataTypeRC,"The parent of a child object node must be a 'container'.");
  
  _objAppendRightMostNode( this, child );
  return kOkRC;
}


unsigned cw::object_t::child_count() const
{
  unsigned n = 0;
  if( is_container() && u.children != nullptr)
  {
    object_t* o = u.children;
    
    for(n=1; o->sibling != nullptr; o=o->sibling)
      ++n;
  }
  return n;
}

cw::rc_t cw::object_t::value( void* dst, unsigned dstTypeId ) { return type->value(this,dstTypeId,dst); }
cw::rc_t cw::object_t::value( char& v )     const { return type->value(this,kCharTId,&v); }
cw::rc_t cw::object_t::value( int8_t&  v )  const { return type->value(this,kInt8TId,&v); }
cw::rc_t cw::object_t::value( uint8_t& v )  const { return type->value(this,kUInt8TId,&v); }
cw::rc_t cw::object_t::value( int16_t&  v ) const { return type->value(this,kInt16TId,&v); }
cw::rc_t cw::object_t::value( uint16_t& v ) const { return type->value(this,kUInt16TId,&v); }
cw::rc_t cw::object_t::value( int32_t&  v ) const { return type->value(this,kInt32TId,&v); }
cw::rc_t cw::object_t::value( uint32_t& v ) const { return type->value(this,kUInt32TId,&v); }
cw::rc_t cw::object_t::value( int64_t&  v ) const { return type->value(this,kInt64TId,&v); }
cw::rc_t cw::object_t::value( uint64_t& v ) const { return type->value(this,kUInt64TId,&v); }
cw::rc_t cw::object_t::value( float&  v )   const { return type->value(this,kFloatTId,&v); }
cw::rc_t cw::object_t::value( double& v )   const { return type->value(this,kDoubleTId,&v); }
cw::rc_t cw::object_t::value( bool& v )     const { return type->value(this,kBoolTId,&v); }
cw::rc_t cw::object_t::value( char*& v )    const { return type->value(this,kStringTId,&v); }
cw::rc_t cw::object_t::value( const char*& v ) const { return type->value(this,kCStringTId,&v); }



cw::rc_t cw::object_t::set_value( char v )     { return _objSetValue(this,v); }
cw::rc_t cw::object_t::set_value( int8_t  v )  { return _objSetValue(this,v); }
cw::rc_t cw::object_t::set_value( uint8_t v )  { return _objSetValue(this,v); }
cw::rc_t cw::object_t::set_value( int16_t  v ) { return _objSetValue(this,v); }
cw::rc_t cw::object_t::set_value( uint16_t v ) { return _objSetValue(this,v); }
cw::rc_t cw::object_t::set_value( int32_t  v ) { return _objSetValue(this,v); }
cw::rc_t cw::object_t::set_value( uint32_t v ) { return _objSetValue(this,v); }
cw::rc_t cw::object_t::set_value( int64_t  v ) { return _objSetValue(this,v); }
cw::rc_t cw::object_t::set_value( uint64_t v ) { return _objSetValue(this,v); }
cw::rc_t cw::object_t::set_value( float  v )   { return _objSetValue(this,v); }
cw::rc_t cw::object_t::set_value( double v )   { return _objSetValue(this,v); }
cw::rc_t cw::object_t::set_value( bool v )     { return _objSetValue(this,v); }
cw::rc_t cw::object_t::set_value( char* v )    { return _objSetValue(this,v); }
cw::rc_t cw::object_t::set_value( const char* v ) { return _objSetValue(this,v); }

  

const char* cw::object_t::pair_label() const
{
  cwAssert( is_pair() );
  if( is_pair() )
    return u.children->u.str;
  return nullptr;
}

const struct cw::object_str* cw::object_t::pair_value() const
{
  cwAssert( is_pair() );
  if( is_pair() )
    return u.children->sibling;
  return nullptr;
}

struct cw::object_str* cw::object_t::pair_value() 
{
  cwAssert( is_pair() );
  if( is_pair() )
    return u.children->sibling;
  return nullptr;
}


const struct cw::object_str* cw::object_t::find( const char* label, unsigned flags ) const
{
  if( is_container() )
  {
    for(object_t* o=u.children; o!=nullptr; o=o->sibling)
    {
      if( o->is_pair() && textCompare(o->pair_label(),label) == 0 )
        return o->pair_value();

      const object_t* ch; 
      if( cwIsFlag(flags,kRecurseFl) )
        if((ch = o->find(label)) != nullptr )
          return ch;
    }     
  }  
  return nullptr;
}

struct cw::object_str* cw::object_t::find( const char* label, unsigned flags ) 
{
  return const_cast<struct object_str*>(((const object_t*)this)->find(label,flags));
}

const struct cw::object_str* cw::object_t::child_ele( unsigned idx ) const
{
  if( is_container() )
  {
    unsigned i = 0;
    for(object_t* o=u.children; o!=nullptr; o=o->sibling,++i)
      if( i == idx )
        return o;            
  }
  return nullptr;
}

struct cw::object_str* cw::object_t::child_ele( unsigned idx )
{
  return const_cast<struct object_str*>(((const object_t*)this)->child_ele(idx));
}

const struct cw::object_str* cw::object_t::next_child_ele( const struct object_str* ele ) const
{
  if( is_container() )
  {
    if( ele == nullptr )
      return u.children;

    return ele->sibling;
  }
  return nullptr;
}

struct cw::object_str* cw::object_t::next_child_ele( struct object_str* ele  )
{
  return const_cast<struct object_str*>(((const object_t*)this)->next_child_ele(ele));
}

unsigned cw::object_t::to_string( char* buf, unsigned bufByteN ) const
{
  return type->to_string(this,buf,bufByteN );
}

char* cw::object_t::to_string() const
{
  unsigned n = 1024;
  char* buf = mem::alloc<char>(n);
  do
  {
    unsigned actual_bytes = to_string(buf,n);
    if( actual_bytes < n )
      break;

    n *= 2;
    buf = mem::resize(buf,n);
    
  }while(1);

  return buf;
}


void cw::object_t::print(const print_ctx_t* c) const
{
  print_ctx_t ctx;
  if( c != nullptr )
    ctx = *c;
  type->print(this,ctx); 
}

cw::object_t* cw::object_t::duplicate() const
{ return type->duplicate(this,nullptr);  }

cw::object_t* cw::newObject( std::uint8_t v, object_t* parent)
{ return _objCreateValueNode<uint8_t>( parent, v ); }

cw::object_t* cw::newObject( std::int8_t v, object_t* parent)
{ return _objCreateValueNode<int8_t>( parent, v ); }

cw::object_t* cw::newObject( std::uint16_t v, object_t* parent)
{ return _objCreateValueNode<uint16_t>( parent, v ); }

cw::object_t* cw::newObject( std::int16_t v, object_t* parent)
{ return _objCreateValueNode<int16_t>( parent, v ); }

cw::object_t* cw::newObject( std::uint32_t v, object_t* parent)
{ return _objCreateValueNode<uint32_t>( parent, v ); }

cw::object_t* cw::newObject( std::int32_t v, object_t* parent)
{ return _objCreateValueNode<int32_t>( parent, v ); }

cw::object_t* cw::newObject( std::uint64_t v, object_t* parent)
{ return _objCreateValueNode<uint64_t>( parent, v ); }

cw::object_t* cw::newObject( std::int64_t v, object_t* parent)
{ return _objCreateValueNode<uint64_t>( parent, v ); }

cw::object_t* cw::newObject( bool v, object_t* parent)
{ return _objCreateValueNode<bool>( parent, v ); }

cw::object_t* cw::newObject( float v, object_t* parent)
{ return _objCreateValueNode<float>( parent, v ); }

cw::object_t* cw::newObject( double v, object_t* parent)
{ return _objCreateValueNode<double>( parent, v ); }
 
cw::object_t* cw::newObject( char* v, object_t* parent)
{ return _objCreateValueNode<const char*>( parent, v ); }

cw::object_t* cw::newObject( const char* v, object_t* parent)
{ return _objCreateValueNode<const char*>( parent, v ); }

cw::object_t* cw::newDictObject( object_t* parent )
{ return _objAppendRightMostNode(parent,  _objAllocate( kDictTId, parent) ); }
    
cw::object_t* cw::newListObject( object_t* parent )
{ return _objAppendRightMostNode(parent,  _objAllocate( kListTId, parent) ); }

cw::object_t* cw::newPairObject( const char* label, object_t* value, object_t* parent)
{
  object_t* pair = _objAppendRightMostNode(parent, _objAllocate( kPairTId, parent) );

  _objCreateValueNode<const char*>( pair, label );

  pair->append_child(value);

  return value;
}

cw::object_t* cw::newPairObject( const char* label, std::uint8_t v, object_t* parent)
{ return _objCreatePairNode<uint8_t>( parent, label, v ); }

cw::object_t* cw::newPairObject( const char* label, std::int8_t v, object_t* parent)
{ return _objCreatePairNode<int8_t>( parent, label, v ); }

cw::object_t* cw::newPairObject( const char* label, std::uint16_t v, object_t* parent)
{ return _objCreatePairNode<uint16_t>( parent, label, v ); }

cw::object_t* cw::newPairObject( const char* label, std::int16_t v, object_t* parent)
{ return _objCreatePairNode<int16_t>( parent, label, v ); }

cw::object_t* cw::newPairObject( const char* label, std::uint32_t v, object_t* parent)
{ return _objCreatePairNode<uint32_t>( parent, label, v ); }

cw::object_t* cw::newPairObject( const char* label, std::int32_t v, object_t* parent)
{ return _objCreatePairNode<int32_t>( parent, label, v ); }

cw::object_t* cw::newPairObject( const char* label, std::uint64_t v, object_t* parent)
{ return _objCreatePairNode<uint64_t>( parent, label, v ); }

cw::object_t* cw::newPairObject( const char* label, std::int64_t v, object_t* parent)
{ return _objCreatePairNode<uint64_t>( parent, label, v ); }

cw::object_t* cw::newPairObject( const char* label, bool v, object_t* parent)
{ return _objCreatePairNode<bool>( parent, label, v ); }

cw::object_t* cw::newPairObject( const char* label, float v, object_t* parent)
{ return _objCreatePairNode<float>( parent, label, v ); }

cw::object_t* cw::newPairObject( const char* label, double v, object_t* parent)
{ return _objCreatePairNode<double>( parent, label, v ); }
 
cw::object_t* cw::newPairObject( const char* label, char* v, object_t* parent)
{ return _objCreatePairNode<const char*>( parent, label, v ); }

cw::object_t* cw::newPairObject( const char* label, const char* v, object_t* parent)
{ return _objCreatePairNode<const char*>( parent, label, v ); }


cw::rc_t cw::objectFromString( const char* s, object_t*& objRef )
{
  lex::handle_t lexH;
  rc_t          rc;
  unsigned      lexFlags = 0;
  unsigned      lexId    = lex::kErrorLexTId;
  object_t*     cnp      = _objAllocate(kRootTId,nullptr);
  object_t*     root     = cnp;
  objRef = nullptr;

  if((rc = lex::create(lexH,s,textLength(s), lexFlags )) != kOkRC )
    return rc;

  // setup the lexer with additional tokens
  for(unsigned i=0; _objTokenArray[i].id != lex::kErrorLexTId; ++i)
    if((rc = lex::registerToken( lexH, _objTokenArray[i].id, _objTokenArray[i].label )) != kOkRC )
    {
      rc = cwLogError(rc,"Object lexer token registration failed on token id:%i : '%s'",_objTokenArray[i].id, _objTokenArray[i].label);
      goto errLabel;
    }

  // register the segmented token matcher
  if((rc = lex::registerMatcher( lexH, kSegmentedIdLexTId, _lexSegmentedIdMatcher)) != kOkRC )
  {
      rc = cwLogError(rc,"Object lexer token matcher registration failed");
      goto errLabel;
  }

  // main parser loop
  while((lexId = lex::getNextToken(lexH)) != lex::kErrorLexTId && (lexId != lex::kEofLexTId) && (rc == kOkRC))
  {
    
    switch( lexId )
    {
      case kLCurlyLexTId:
        cnp = _objCreateConainerNode( lexH, cnp, kDictTId );
        break;

      case kRCurlyLexTId:
        if( cnp == nullptr )
          _objSyntaxError(lexH,"An end of 'object' was encountered without an associated 'object' start.");
        else
          cnp = cnp->parent;
        break;

      case kLHardLexTId:        
        cnp = _objCreateConainerNode( lexH, cnp, kListTId );
        break;

      case kRHardLexTId:
        if( cnp == nullptr )
          rc = _objSyntaxError(lexH,"An end of 'array' was encountered without an associated 'array' start.");
        else
          cnp = cnp->parent;
        break;

      case kColonLexTId:
        if( cnp == nullptr || !cnp->is_pair() )
          rc = _objSyntaxError(lexH,"A colon was encountered outside a 'pair' node.");
        break;

      case kCommaLexTId:
        if( cnp == nullptr || (!cnp->is_list() && !cnp->is_dict()) )
          rc = _objSyntaxError(lexH,"Unexpected comma outside of 'array' or 'object'.");
        break;
        
      case lex::kRealLexTId:
        if( tokenIsSinglePrecision(lexH) )
          _objCreateValueNode( cnp, lex::tokenFloat(lexH),"float" );
         else
          _objCreateValueNode( cnp, lex::tokenDouble(lexH), "double" );
        break;
        
      case lex::kIntLexTId:
        if( tokenIsUnsigned(lexH) )
          _objCreateValueNode( cnp, lex::tokenUInt(lexH), "uint" );
        else
          _objCreateValueNode( cnp, lex::tokenInt(lexH), "int" );
        break;
        
      case lex::kHexLexTId:
        _objCreateValueNode( cnp, lex::tokenInt(lexH), "int", kHexFl );
        break;
        
      case kTrueLexTId:
        _objCreateValueNode( cnp, true, "true" );        
        break;

      case kFalseLexTId:
        _objCreateValueNode( cnp, false, "false" );        
        break;

      case kNullLexTId:
        _objAppendRightMostNode( cnp, _objAllocate( kNullTId, cnp ));
        break;        

      case kSegmentedIdLexTId:
      case lex::kIdentLexTId:
      case lex::kQStrLexTId:
        {
          
          // if the parent is an object then this string must be a pair label
          if( cnp->is_dict() )
            cnp = _objAppendRightMostNode( cnp, _objAllocate( kPairTId, cnp ));

          unsigned n = lex::tokenCharCount(lexH);
          char s[ n + 1 ];
          memcpy(s,lex::tokenText(lexH),n);
          s[n] = 0;

          unsigned identFl = 0;
          if( lexId == lex::kQStrLexTId )
            _escape_string(s);
          else
            identFl = kIdentFl;
          
          _objCreateValueNode<char*>( cnp, s, "string", identFl );
        }
        break;

      case lex::kEofLexTId:
        break;

      default:
        _objSyntaxError(lexH,"Unknown token type (%i) in text.", int(lexId) );
    }

    if( cnp == nullptr )
    {
      rc = _objSyntaxError( lexH, "Node parse failed." );
      goto errLabel;
    }

    // if this is a pair node and it now has both values 
    // then make the parent 'object' the current node
    if( cnp->is_pair() && cnp->child_count()==2 )
      cnp = cnp->parent;
  }

  if( lexId == lex::kErrorLexTId )
  {
    rc = cwLogError(kSyntaxErrorRC,"A lexical element was not recognized.");
    goto errLabel;    
  }
  
  // if the root has only one child then make the child the root
  if( root != nullptr && root->child_count() == 1 )
  {
    cnp = root->u.children;
    cnp->unlink();
    root->free();
    root = cnp;
  }
  
  objRef = root;
  
 errLabel:
  rc_t rc0 = lex::destroy(lexH);

  return rc != kOkRC ? rc : rc0;
  
}

cw::rc_t cw::objectFromFile( const char* fn, object_t*& objRef )
{
  rc_t     rc         = kOkRC;
  unsigned bufByteCnt = 0;
  char*    buf        = NULL;

  objRef = nullptr;
  
  if(( buf = file::fnToStr(fn, &bufByteCnt)) == NULL )
    rc = cwLogError(kOpFailRC,"File to text buffer conversion failed on '%s'.",cwStringNullGuard(fn));
  else
  {
    rc = objectFromString( buf, objRef );
    mem::release(buf);
  }

  return rc;
}


void cw::objectPrintTypes( object_t* o0 )
{
  if( o0->is_container() )
    for(object_t* o = o0->u.children; o!=nullptr; o=o->sibling)
      objectPrintTypes(o);

  printf("%s ",o0->type->label);
}




cw::rc_t cw::objectToFile( const char* fn, const object_t* obj )
{
  rc_t rc = kOkRC;
  
  char* buf;
  if((buf = obj->to_string()) == nullptr )
  {
    rc = cwLogError(kOpFailRC,"Unable to convert object to string.");
    goto errLabel;
  }

  if((rc = file::fnWrite(fn,buf,textLength(buf))) != kOkRC )
  {
    rc = cwLogError(rc,"Object write failed on write to '%s'.",cwStringNullGuard(fn));
    goto errLabel;
  }
 errLabel:

  mem::release(buf);
  
  return rc;
}

namespace cw
{
  rc_t _object_test_basic( const test::test_args_t& args )
  {
    rc_t          rc   = kOkRC;  
    cw::object_t* o    = nullptr;
    const char    s [] = "{ a:1, b:2, c:[ 1.23, 4.56 ], d:true, e:false, f:true }";

    int a = 0;
    int b = 0;
    const cw::object_t* c = nullptr;
    bool d,e,f;

    const unsigned bufN = 128;
    char buf[bufN];

    unsigned i = 0;

    cw::object_t* oo = nullptr;
  
    if((rc = cw::objectFromString(s,o)) != kOkRC )
      goto errLabel;

    int v;
    if((rc = o->get("b",v)) != kOkRC )
      goto errLabel;
  
    cwLogPrint("value:%i\n",v);
  
    o->print();

  
    if((rc = o->getv("a",a,"b",b)) != kOkRC )
      goto errLabel;
  
    cwLogPrint("G: %i %i\n",a,b);


    if((rc = o->readv("a",0,a,
                      "b",0,b,
                      "c",cw::kOptFl | cw::kListTId,c,
                      "d",0,d,
                      "e",0,e,
                      "f",0,f)) != kOkRC )
    {
      goto errLabel;
    }
  
    cwLogPrint("R: %i %i : %i %i %i\n",a,b,d,e,f);

  
    i = o->to_string(buf,bufN);
    cwLogPrint("%i : %s\n",i, buf);
  
    oo = o->duplicate();

    oo->print();
  
    oo->free();
  
    
    o->free();

  errLabel:
    return rc;
  }

  rc_t _object_test_to_json( const test::test_args_t& args )
  {
    double   v0[] = {1.23,2.34,3.45};
    unsigned v0N  = sizeof(v0)/sizeof(v0[0]);
    int      v1[] = {-1,0,1,2,3,4};
    unsigned v1N  = sizeof(v1)/sizeof(v1[0]);
  
    cw::object_t* d = cw::newDictObject();

    d->putv("A","Abc","B",1.234);
    d->put_numeric_list("v0",v0,v0N);
    d->put_numeric_list("v1",v1,v1N);

    char* s = d->to_string();
    cwLogPrint("%s\n",s);
    cw::mem::release(s);

    d->free();

    return kOkRC;
  
  }

}


cw::rc_t cw::object_test( const test::test_args_t& args )
{
  rc_t rc = kOkRC;

  if( textIsEqual(args.test_label,"basic") )
  {
    rc = _object_test_basic(args);
    goto errLabel;
  }
  
  if( textIsEqual(args.test_label,"to_json") )
  {
    rc = _object_test_to_json(args);
    goto errLabel;
  }

  rc = cwLogError(kInvalidArgRC,"Unknown test case module:%s test:%s.",args.module_label,args.test_label);
  
errLabel:
  return rc;
}

